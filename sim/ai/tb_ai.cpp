// Bench of rtl/soc/rcp_ai.sv: the console's AI semantics (the interrupt at the start of an idle AI and at
// the promotion of the queued buffer, none when the queue empties; STATUS bits 31/30/25/24/20/0; two buffers, a third dropped),
// the reader's lead over the Pocket's FIFO, and an HVQM2-style driver (Nintendo's timekeeper: one buffer in the AI at a time,
// the next queued from the AI interrupt) against a 48 kHz FIFO model: no silent word may fall inside the music.
//   tb_ai [handler latency us] [start interrupt 0/1: the bench gives the driver one at the start itself (for the pre-2026-10-09 RTL)]
// Started from ~/n64-fidelity/vi_ai_tb_ai.cpp (the audit's bench).
#include "Vrcp_ai.h"
#include "verilated.h"
#include "Vrcp_ai___024root.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static Vrcp_ai *t;
static long cyc;
static int fails;
static long irq_n, irq_last = -1;                            // every AI interrupt pulse, counted in tick() (the register writes tick too)
static void tick() { t->clk = 0; t->eval(); t->clk = 1; t->eval(); cyc++; if (t->irq) { irq_n++; irq_last = cyc; } }
static void wr(int a, unsigned v) { t->a = a; t->wd = v; t->wr_s = 1; tick(); t->wr_s = 0; tick(); }
static unsigned rdreg(int a) { t->a = a; t->eval(); return t->rd; }
static unsigned rdst() { return rdreg(3); }
static void check(const char *what, long got, long want)
{
    if (got != want) { printf("FAIL %s: %ld, want %ld\n", what, got, want); fails++; }
    else printf("ok   %s\n", what);
}
// the FIFO model: one word out every 1302 sys clocks (48 kHz at 62.5 MHz); the bridge answers a read at once
struct Fifo { int fill = 0; long next_pop = 0; int maxfill = 0; } F;
static void step_cycle(bool *fw_sil)
{
    const double per = 62.5e6 / 48000.0;
    if (cyc >= F.next_pop) { if (F.fill > 0) F.fill--; F.next_pop += (long)per; }
    t->fill = F.fill > 1023 ? 1023 : F.fill;
    t->ack = t->req;
    t->rdata = 0x10001000u + (cyc & 0xFF) * 0x00010001u;
    tick();
    if (t->fw) { F.fill++; if (F.fill > F.maxfill) F.maxfill = F.fill; if (fw_sil) *fw_sil = t->rootp->rcp_ai__DOT__sil; }
}
static void run(long n) { for (long i = 0; i < n; i++) step_cycle(nullptr); }

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    t = new Vrcp_ai;
    int latency_us = argc > 1 ? atoi(argv[1]) : 1000;
    int fire_start = argc > 2 ? atoi(argv[2]) : 0;
    t->rst = 1; for (int i = 0; i < 10; i++) tick(); t->rst = 0; tick();
    t->step_we = 1; t->step_wd = 43700; tick(); t->step_we = 0;   // 32,006 Hz
    const int BUF = 0x600;                                   // bytes: 384 frames, 12 ms at 32 kHz

    // ---- 1. the registers and the interrupt, by themselves (the reader is off: DMA disabled)
    irq_n = 0;
    check("STATUS after reset: bits 24 and 20 only", rdst(), 0x01100000u);
    wr(2, 0);
    wr(0, 0x1000); wr(1, BUF); run(8);
    check("a buffer started on an idle AI: one interrupt", irq_n, 1);
    check("... STATUS busy (30) and bits 24, 20", rdst(), 0x41100000u);
    wr(0, 0x2000); wr(1, BUF); run(8);
    check("a second queued: no interrupt", irq_n, 1);
    check("... STATUS full (31, 0)", rdst(), 0xC1100001u);
    wr(0, 0x3000); wr(1, BUF); run(8);
    check("a third is dropped: no interrupt, queue as it was", rdst(), 0xC1100001u);
    wr(3, 0);
    wr(2, 1);
    check("CONTROL DMA on: STATUS bit 25", rdst(), 0xC3100001u);
    check("AI_LEN reads the bytes left of the current", rdreg(1) & 0x3FFFF, BUF);

    // ---- 2. play: the first buffer's end promotes the second (one interrupt), the second's end is silent
    long irq_at[8]; int ni = 0;
    long t0 = cyc, drained_first = -1;
    F.next_pop = cyc; F.fill = 0; F.maxfill = 0;
    long end = cyc + (long)(62.5e6 * 0.06);
    long first_irq_at = -1, second_busy_off = -1;
    while (cyc < end) {
        const long i0 = irq_n; step_cycle(nullptr);
        if (irq_n != i0 && ni < 8) { irq_at[ni++] = cyc; if (first_irq_at < 0) first_irq_at = cyc; }
        if (second_busy_off < 0 && !(rdst() & 0x40000000u) && ni >= 1) second_busy_off = cyc;
        (void)drained_first; (void)irq_last;
    }
    check("two buffers played: one interrupt (the promotion), none when the queue emptied", ni, 1);
    check("... then STATUS idle (busy, full clear)", rdst() & 0xC0000001u, 0);
    printf("     the promotion's interrupt %.2f ms after the start (a 12.0 ms buffer; the reader leads the audio by %d words = %.1f ms), the queue empty at %.2f ms\n",
           (first_irq_at - t0) / 62500.0, F.maxfill, F.maxfill / 48.0, (second_busy_off - t0) / 62500.0);
    check("the FIFO's lead (the most words in it) is the 256 target", F.maxfill >= 256 && F.maxfill <= 260, 1);
    wr(3, 0);

    // ---- 3. an HVQM2-style driver: one buffer at a time, the next from the interrupt
    delete t; t = new Vrcp_ai; cyc = 0; F = Fifo(); irq_n = 0;
    t->rst = 1; for (int i = 0; i < 10; i++) tick(); t->rst = 0; tick();
    t->step_we = 1; t->step_wd = 43700; tick(); t->step_we = 0;
    wr(2, 1);
    int queued = 0, nbuf = 6, nirq = 0; long pending_at = -1; long irq_t[16];
    auto queue = [&]() { wr(0, 0x1000 * (queued + 1)); wr(1, BUF); queued++; };
    long seen = 0;
    queue();
    if (fire_start) pending_at = cyc + (long)(latency_us * 62.5);
    end = cyc + (long)(62.5e6 * 0.25);
    long words = 0, gapwords = 0; bool started = false; long tail = 0;
    if (!fire_start) pending_at = -1;
    while (cyc < end) {
        bool sil = false;
        step_cycle(&sil);
        if (t->fw) { words++; if (!sil) { started = true; gapwords += tail; tail = 0; } else if (started) tail++; }   // (silence counts when music follows it)
        if (irq_n != seen) { seen = irq_n; if (nirq < 16) irq_t[nirq++] = cyc; pending_at = cyc + (long)(latency_us * 62.5); }
        if (pending_at >= 0 && cyc >= pending_at) { pending_at = -1; wr(3, 1); if (queued < nbuf) queue(); }
    }
    printf("     HVQM2-style driver, handler latency %d us: interrupts %d, buffers queued %d, words written %ld, silent words inside the music %ld\n",
           latency_us, nirq, queued, words, gapwords);
    for (int i = 0; i < nirq; i++) printf("       irq %d at %.2f ms\n", i, irq_t[i] / 62500.0);
    check("HVQM2 driver: no silence inside the music", gapwords, 0);
    check("HVQM2 driver: all the buffers queued", queued, nbuf);

    // ---- 4. the DAC's rate under each TV system: the step the runtime computes (n64rt_rsp.c n64rt_osAiSetFrequency, with the console's
    // osViClock: NTSC 48,681,812, PAL 49,656,530, MPAL 48,628,316 Hz) makes the reader consume source frames at the DAC's rate. The
    // Pocket's FIFO is drained at exactly 48 kHz (a double-valued pop time), the rate is read off the frames fetched in 0.5 s.
    {
        struct { const char *tv; unsigned clk; } tvs[3] = { { "NTSC", 48681812u }, { "PAL", 49656530u }, { "MPAL", 48628316u } };
        const unsigned freqs[3] = { 11025u, 32000u, 44100u };
        for (auto &tv : tvs) for (unsigned f : freqs) {
            unsigned dac = (tv.clk + f / 2) / f;                       // libultra: the divisor, rounded
            unsigned real = tv.clk / dac;                               // the DAC's actual rate
            unsigned step = (unsigned)(((unsigned long long)real << 16) / 48000u);
            delete t; t = new Vrcp_ai; cyc = 0; irq_n = 0; F = Fifo();
            t->rst = 1; for (int i = 0; i < 10; i++) tick(); t->rst = 0; tick();
            t->step_we = 1; t->step_wd = step; tick(); t->step_we = 0;
            wr(0, 0x1000); wr(1, 0x3FFF8); wr(2, 1);                   // one buffer of 65,535 frames: more than 0.7 s of 44.1 kHz
            double next_pop = (double)cyc, per = 62.5e6 / 48000.0;
            long frames = 0, c0 = -1; double cstart = 0;
            const double span = 62.5e6 * 0.5;
            long fill = 0;
            for (long i = 0; ; i++) {
                if (cyc >= next_pop) { if (fill > 0) fill--; next_pop += per; }
                t->fill = fill > 1023 ? 1023 : fill;
                t->ack = t->req;
                t->rdata = 0x10001000u;
                if (t->req && c0 >= 0) frames++;
                tick();
                if (t->fw) fill++;
                if (c0 < 0 && cyc > 62.5e6 * 0.05) { c0 = cyc; cstart = (double)cyc; frames = 0; }      // (after the FIFO is primed)
                if (c0 >= 0 && cyc - cstart >= span) break;
            }
            // each frame takes ~20 cycles of the reader: count the frames (req held until ack: one ack a frame), not the cycles
            double hz = frames / (span / 62.5e6);
            double want = real;                                        // frames the DAC would consume a second
            // (frames counts req cycles: the reader holds req 1 cycle a word here since ack = req)
            printf("     %-4s %5u Hz: DACRATE+1 = %u, DAC %u Hz, step %u: %.1f frames/s fetched (want %u)\n", tv.tv, f, dac, real, step, hz, real);
            check("DAC rate consumed within 100 ppm + 2 frames", fabs(hz - want) <= want * 1e-4 + 2.0 ? 1 : 0, 1);
        }
    }
    printf(fails ? "tb_ai: FAIL (%d)\n" : "tb_ai: PASS\n", fails);
    return fails != 0;
}
