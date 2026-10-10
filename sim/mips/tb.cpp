// Co-simulation: the RTL MIPS core (rtl/mips/mips_core.sv) against the
// reference model (sim/n64/cpu.c), both running the same ROM.
//
// Two machines, each with its own memory and RCP devices (sim/n64): the RTL's
// served through the core's memory port, the model's stepped directly. After
// every instruction the RTL retires, the model executes one and the two must
// agree on the pc, the instruction and the register written. When the RTL
// takes an interrupt, the model is told to take it at the same point (after
// checking that one was pending). Devices advance one CPU cycle per executed
// instruction on both sides, so their state stays in step.
//
//   tb rom.z64 [-n instructions] [-s syms] [-l latency] [-v] [-p pad script]
//
// -p: controller 1, as n64sim's: "from-to:buttons[:x:y],..." in VI frames,
// given to both machines as the model's VI frame count changes.
//
// The core has no 64-bit instructions: the ROM is one tools/n64patch.py
// wrote, and the model makes any 64-bit instruction left fatal.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>

#include "Vmips_core.h"
#include "verilated.h"

extern "C" {
#include "n64.h"
extern uint64_t n64_tlb_hits;
}

static n64_t A, B;                    // A: the RTL's machine, B: the model
static Vmips_core *top;
static uint64_t cycles, retired;

// ---- symbols, for reports
static struct { uint32_t a; char name[64]; } *syms;
static int nsyms;
static void load_syms(const char *p)
{
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[512], name[400], t;
    unsigned a;
    int cap = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%x %c %399s", &a, &t, name) != 3 || (t != 'T' && t != 't')) continue;
        if (nsyms == cap) syms = (decltype(syms))realloc(syms, sizeof *syms * (cap = cap ? cap * 2 : 4096));
        syms[nsyms].a = a; snprintf(syms[nsyms].name, 64, "%s", name); nsyms++;
    }
    fclose(f);
}
static const char *sym(uint32_t a)
{
    static char buf[96];
    int lo = 0, hi = nsyms - 1, best = -1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (syms[m].a <= a) { best = m; lo = m + 1; } else hi = m - 1; }
    if (best < 0) return "?";
    snprintf(buf, sizeof buf, "%s+%x", syms[best].name, a - syms[best].a);
    return buf;
}

// ---- the RSP's tasks: done at once (no rendering here), both machines alike
static void sp_task(n64_t *n, uint32_t t)
{
    if (rd32(n->mem.spmem, t) == 1) n64_raise(n, MI_INTR_DP);
}

// ---- the controller script
static struct { uint64_t from, to; uint32_t buttons; int x, y; } pads[64];
static int npads;
static void parse_pad(const char *e)
{
    while (e && *e && npads < 64) {
        unsigned long long a, b; unsigned bt; int x = 0, y = 0;
        if (sscanf(e, "%llu-%llu:%x:%d:%d", &a, &b, &bt, &x, &y) >= 3) {
            pads[npads].from = a; pads[npads].to = b; pads[npads].buttons = bt; pads[npads].x = x; pads[npads].y = y; npads++;
        }
        e = strchr(e, ',');
        if (e) e++;
    }
}
static uint32_t pad_at(uint64_t frame)
{
    uint32_t bt = 0; int x = 0, y = 0;
    for (int i = 0; i < npads; i++)
        if (frame >= pads[i].from && frame <= pads[i].to) { bt |= pads[i].buttons; x += pads[i].x; y += pads[i].y; }
    return bt << 16 | (uint32_t)(uint8_t)(int8_t)x << 8 | (uint32_t)(uint8_t)(int8_t)y;
}

// ---- the memory port
static int lat = 3;
static struct { int busy, words, left, delay; uint32_t addr; bool line; } rq;

static void do_write(uint32_t pa, uint32_t data, uint32_t strb)
{
    if (strb == 0xF) { n64_write_phys(&A, pa, 4, data); return; }
    if (strb == 0xC || strb == 0x3) { n64_write_phys(&A, pa + (strb == 0x3 ? 2 : 0), 2, strb == 0x3 ? data & 0xFFFF : data >> 16); return; }
    for (int l = 0; l < 4; l++)
        if (strb & (1u << l)) {
            if (pa < RDRAM_SIZE) A.mem.rdram[(pa & ~3u) + l] = (uint8_t)(data >> (8 * l));     // lane l, native
            else n64_write_phys(&A, (pa & ~3u) + (3 - l), 1, (data >> (8 * l)) & 0xFF);
        }
}

// (a core built with XWB: its stores by st_*. The buffer outside is full now
// and then; a device's store is done a few cycles on)
static int st_wait;
static uint32_t st_lfsr = 0xACE1u;
static void st_before_edge()
{
    top->st_done = 0;
    if (st_wait && --st_wait == 0) top->st_done = 1;
    if (top->st_valid) {
        if (top->st_full) { fprintf(stderr, "a store given while the buffer is full\n"); A.halted = 1; }
        do_write(top->st_addr, top->st_data, top->st_strb);
        if (top->st_dev) st_wait = 1 + (st_lfsr & 3);
    }
}
static void st_next()
{
    st_lfsr = st_lfsr >> 1 ^ (-(st_lfsr & 1u) & 0xB400u);
    top->st_full = (st_lfsr & 7u) == 0;
}

static void mem_before_edge()
{
    top->mem_ack = 0;
    top->mem_rvalid = 0;
    if (rq.busy) {
        if (rq.delay) { rq.delay--; return; }
        uint32_t a = rq.addr + 4 * (rq.words - rq.left), v = 0;
        if (n64_read_phys(&A, a, 4, &v)) {
            fprintf(stderr, "RTL read fault at %08x\n", a);
            A.halted = 1;
        }
        top->mem_rvalid = 1;
        top->mem_rdata = v;
        if (--rq.left == 0) rq.busy = 0;
        return;
    }
    if (top->mem_req) {
        top->mem_ack = 1;
        if (top->mem_we) do_write(top->mem_addr, top->mem_wdata, top->mem_wstrb);
        else { rq.busy = 1; rq.addr = top->mem_addr; rq.words = rq.left = top->mem_line ? 8 : 1; rq.delay = lat; }
    }
}

static void eval_comb()
{
    top->eval();
    top->irq_rcp = (A.rcp.mi_intr & A.rcp.mi_mask) != 0;
    top->eval();
}

static int check_fail(const char *what)
{
    fprintf(stderr, "MISMATCH after %" PRIu64 " instructions (%" PRIu64 " cycles): %s\n", retired, cycles, what);
    fprintf(stderr, "  model pc %08x (%s)\n", B.cpu.pc, sym(B.cpu.pc));
    fprintf(stderr, "  MI: RTL's devices intr %02x mask %02x, model's intr %02x mask %02x\n", A.rcp.mi_intr, A.rcp.mi_mask, B.rcp.mi_intr, B.rcp.mi_mask);
    return 1;
}

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: tb rom.z64 [-n N] [-s syms] [-l latency] [-v]\n"); return 1; }
    uint64_t limit = 10000000;
    int verbose = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) limit = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) load_syms(argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) lat = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) parse_pad(argv[++i]);
    }
    for (n64_t *n : { &A, &B }) {
        n->log = stderr;
        n->mem.rdram = (uint8_t *)calloc(RDRAM_SIZE, 1);
        n->mem.eeprom_size = 512;
        memset(n->mem.eeprom, 0xFF, sizeof n->mem.eeprom);
        if (n64_load_rom(n, argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
        n64_boot(n);
        n->sp_task = sp_task;
        memset(n->cpu.r, 0, sizeof n->cpu.r);             // the RTL starts with zeroed registers
    }
    B.irq_external = 1;
    { extern int n64_no64; n64_no64 = getenv("TB_NO64") != NULL; }

    if (getenv("TB_MODEL_ONLY")) {                      // the model alone (what the core leaves to a runtime included)
        { extern int n64_no64; n64_no64 = 0; }
        FILE *rt = getenv("TB_REGTRACE") ? fopen(getenv("TB_REGTRACE"), "w") : nullptr;   // every register write: pc r value
        for (uint64_t i = 0; i < limit && !B.halted; i++) {
            uint64_t before[32]; uint32_t pc0 = B.cpu.pc;
            if (rt) memcpy(before, B.cpu.r, sizeof before);
            n64_cpu_irq_pending(&B); n64_cpu_step(&B); n64_advance(&B, 1);
            if (rt) for (int r = 1; r < 32; r++) if (B.cpu.r[r] != before[r]) fprintf(rt, "%08x %d %016" PRIx64 "\n", pc0, r, (uint64_t)B.cpu.r[r]);
        }
        if (rt) fclose(rt);
        if (const char *d = getenv("TB_DUMP")) {
            uint32_t a = strtoul(d, 0, 0) & 0x7FFFFFu, k = strchr(d, ',') ? strtoul(strchr(d, ',') + 1, 0, 0) : 16;
            for (uint32_t i = 0; i < k; i++) { uint32_t v = 0; n64_read_phys(&B, a + 4 * i, 4, &v); printf("%08x\n", v); }
        }
        return 0;
    }
    top = new Vmips_core;
    top->reset_pc = A.cpu.pc;
    top->rst = 1; top->clk = 0;
    for (int i = 0; i < 4; i++) { top->clk = 1; eval_comb(); top->clk = 0; eval_comb(); }
    top->rst = 0;

    uint64_t last_progress = 0, last_frame = 0;
    int bad = 0;
    while (retired < limit && !bad && !A.halted && !B.halted) {
        eval_comb();
        mem_before_edge();
        eval_comb();
        st_before_edge();
        eval_comb();
        // what this cycle retires / takes (read before the edge)
        int ret = top->tr_retire, exc = top->tr_exc, exc_int = top->tr_exc_int;
        uint32_t tpc = top->tr_pc, tinsn = top->tr_insn, epc = top->tr_exc_pc, ecode = top->tr_exc_code;
        int twr = top->tr_wr, trd = top->tr_rd;
        uint64_t tval = top->tr_val;
        int fwe_e = top->tr_fwe_even, fwe_o = top->tr_fwe_odd, fk = top->tr_fwidx;
        uint32_t fwd_e = top->tr_fweven, fwd_o = top->tr_fwodd, fcr = top->tr_fcr31;
        top->clk = 1; eval_comb();
        top->clk = 0;
        st_next();
        cycles++;

        if (ret) {
            if (B.cpu.pc != tpc) { bad = check_fail("pc"); fprintf(stderr, "  RTL retired %08x %08x\n", tpc, tinsn); break; }
            uint32_t mi; n64_read(&B, B.cpu.pc, 4, &mi);
            n64_cpu_irq_pending(&B);                         // (Cause.IP2 follows the MI, as the RTL's)
            n64_cpu_step(&B);
            n64_advance(&B, 1);
            n64_advance(&A, 1);
            retired++;
            if (verbose && retired + 40 >= limit) fprintf(stderr, "%08x: %08x  %-28s %s%016" PRIx64 "\n", tpc, tinsn, sym(tpc), twr ? "-> " : "   ", twr ? tval : 0);
            if (getenv("TB_C0LOG") && (tinsn & 0xFC0007FF) == 0x40000000 && ((tinsn >> 21 & 31) == 0 || (tinsn >> 21 & 31) == 4) && ((tinsn >> 11) & 31) == (unsigned)atoi(getenv("TB_C0LOG")))
                fprintf(stderr, "[%" PRIu64 "] %08x %s %08x  val %016" PRIx64 "\n", retired, tpc, sym(tpc), tinsn, (tinsn >> 21 & 31) == 4 ? B.cpu.r[(tinsn >> 16) & 31] : tval);
            if (mi != tinsn) { bad = check_fail("instruction word"); fprintf(stderr, "  RTL %08x model %08x at %08x\n", tinsn, mi, tpc); break; }
            // the FPU's writes (the trace is registered with the write, so it is this retire's)
            {
                int k = fk;
                if ((fwe_e && fwd_e != B.cpu.f[2 * k]) || (fwe_o && fwd_o != B.cpu.f[2 * k + 1]) || fcr != B.cpu.fcr31) {
                    char w[160];
                    snprintf(w, sizeof w, "FPU: f%d %08x/%08x f%d %08x/%08x fcr31 %08x/%08x (RTL/model; %08x: %08x)",
                             2 * k, fwd_e, B.cpu.f[2 * k], 2 * k + 1, fwd_o, B.cpu.f[2 * k + 1],
                             fcr, B.cpu.fcr31, tpc, tinsn);
                    bad = check_fail(w);
                    break;
                }
            }
            if (twr && B.cpu.r[trd] != tval) {
                char w[128];
                snprintf(w, sizeof w, "r%d: RTL %016" PRIx64 " model %016" PRIx64 " (%08x: %08x)", trd, tval, B.cpu.r[trd], tpc, tinsn);
                bad = check_fail(w);
                fprintf(stderr, "  at %s\n", sym(tpc));
                break;
            }
        }
        if (exc) {
            if (B.cpu.pc != epc) {
                char w[128];
                snprintf(w, sizeof w, "exception %u at %08x, model at %08x", ecode, epc, B.cpu.pc);
                bad = check_fail(w);
                break;
            }
            if (exc_int) {
                if (!n64_cpu_irq_pending(&B)) { bad = check_fail("RTL took an interrupt the model has not pending"); break; }
                n64_cpu_interrupt(&B);
            } else {
                n64_cpu_step(&B);                            // the model faults the same instruction
                n64_advance(&B, 1);
                n64_advance(&A, 1);
                uint32_t mcode = (B.cpu.cop0[C0_CAUSE] >> 2) & 31;
                if (mcode != ecode) {
                    uint32_t wi = 0; n64_read(&B, epc, 4, &wi);
                    char w[128]; snprintf(w, sizeof w, "exception code RTL %u model %u at %08x (%08x), Status %08x", ecode, mcode, epc, wi, B.cpu.cop0[12]);
                    bad = check_fail(w); break;
                }
            }
            if (verbose) fprintf(stderr, "  exception %u (%s) at %08x\n", ecode, exc_int ? "interrupt" : "sync", epc);
        }
        if (B.vi_frames != last_frame) { last_frame = B.vi_frames; A.buttons = B.buttons = pad_at(last_frame); }
        if (retired - last_progress >= 10000000) {
            last_progress = retired;
            fprintf(stderr, "[%" PRIu64 " instructions, %" PRIu64 " cycles, IPC %.2f, frame %" PRIu64 "] pc %s\n", retired, cycles,
                    (double)retired / cycles, B.vi_frames, sym(B.cpu.pc));
        }
        if (cycles > retired * 50 + 1000000) { bad = check_fail("the RTL stopped retiring"); break; }
    }
    if (const char *d = getenv("TB_DUMP")) {           // TB_DUMP=addr,words: the model's RDRAM at the end
        uint32_t a = strtoul(d, 0, 0) & 0x7FFFFFu, k = strchr(d, ',') ? strtoul(strchr(d, ',') + 1, 0, 0) : 16;
        for (uint32_t i = 0; i < k; i++) { uint32_t v = 0; n64_read_phys(&B, a + 4 * i, 4, &v); printf("%08x\n", v); }
    }
    { extern uint64_t n64_tlb_hits; fprintf(stderr, "model: %" PRIu64 " mapped accesses\n", n64_tlb_hits); }
    fprintf(stderr, "%s: %" PRIu64 " instructions in %" PRIu64 " cycles (IPC %.3f)\n", bad ? "FAIL" : "PASS",
            retired, cycles, (double)retired / (cycles ? cycles : 1));
    delete top;
    return bad ? 1 : 0;
}
