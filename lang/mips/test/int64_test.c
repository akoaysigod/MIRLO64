/* The game CPU's 64-bit integer levels, on the SoC (sim/mips_soc, or the Pocket): dmult, dmultu,
 * the d-shifts, dadd / dsub and the 64-bit logic, each on the corner values and random ones, against a
 * table computed off-line (Python, exact integers), and how long a dmultu / dmult followed by mflo and
 * mfhi takes. The levels a CPU has are COP0 register 7's top bits (mips_core: W64_ALU, W64_SH, W64_MD,
 * W64_DIV); the test runs what the CPU has and says which it skipped. No runtime, so a level the CPU does not
 * have is skipped, not emulated. ddiv(u) is not tried (the SoC leaves it to the runtime).
 * Prints PASS or the failures on the UART, then a 0x04 (the bench stops). In sim/mips_soc the bench loads a program
 * only beside a ROM: ROM=<any file> obj/tb_soc build/int64_test.bin -c 40. */
#include <stdint.h>
#include <generated/csr.h>
#include <system.h>

static void putc_(char c) { while (uart_txfull_read()); uart_rxtx_write((uint8_t)c); }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void puthex(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15]); }
static void putdec(uint32_t v) { char b[11]; int n = 0; do { b[n++] = '0' + v % 10; v /= 10; } while (v); while (n) putc_(b[--n]); }
typedef struct { uint32_t h, l; } u64s;          /* a 64-bit value as the CPU keeps it in memory (big-endian) */
static int fails;
static void check64(const char *what, int i, u64s got, u64s want)
{
    if (got.h == want.h && got.l == want.l) return;
    if (++fails > 20) return;
    puts_("FAIL "); puts_(what); puts_(" #"); putdec(i); puts_(": got "); puthex(got.h); puthex(got.l);
    puts_(" want "); puthex(want.h); puthex(want.l); puts_("\n");
}

/* a, b; then dmultu's HI, LO; dmult's HI, LO; dsllv, dsrlv, dsrav by b & 63; dadd, dsub; slt, sltu; and, or, xor, nor */
#include "int64_vec.h"

/* the instructions of the table's row, in one block of assembler, results in out[] (11 u64s) */
static void run_mdu(const u64s *in, u64s *out)
{
    __asm__ volatile(
        ".set push\n.set mips3\n.set noreorder\n.set noat\n"
        "ld    $8, 0(%0)\n ld $9, 8(%0)\n"
        "dmultu $8, $9\n mfhi $10\n mflo $11\n sd $10, 0(%1)\n sd $11, 8(%1)\n"
        "dmult  $8, $9\n mfhi $10\n mflo $11\n sd $10, 16(%1)\n sd $11, 24(%1)\n"
        ".set pop\n"
        : : "r"(in), "r"(out) : "$8", "$9", "$10", "$11", "hi", "lo", "memory");
}
static void run_log(const u64s *in, u64s *out)
{
    __asm__ volatile(
        ".set push\n.set mips3\n.set noreorder\n.set noat\n"
        "ld    $8, 0(%0)\n ld $9, 8(%0)\n"
        "daddu $10, $8, $9\n sd $10, 0(%1)\n"
        "dsubu $10, $8, $9\n sd $10, 8(%1)\n"
        "slt   $10, $8, $9\n sd $10, 16(%1)\n"
        "sltu  $10, $8, $9\n sd $10, 24(%1)\n"
        "and   $10, $8, $9\n sd $10, 32(%1)\n"
        "or    $10, $8, $9\n sd $10, 40(%1)\n"
        "xor   $10, $8, $9\n sd $10, 48(%1)\n"
        "nor   $10, $8, $9\n sd $10, 56(%1)\n"
        ".set pop\n"
        : : "r"(in), "r"(out) : "$8", "$9", "$10", "memory");
}
/* the shifts by b & 63, with the count in $11 */
static void run_sh(const u64s *in, u64s *out)
{
    __asm__ volatile(
        ".set push\n.set mips3\n.set noreorder\n.set noat\n"
        "ld    $8, 0(%0)\n ld $9, 8(%0)\n"
        "lw    $11, 12(%0)\n"                              /* b's low word: the count */
        "dsllv $10, $9, $11\n sd $10, 0(%1)\n"
        "dsrlv $10, $9, $11\n sd $10, 8(%1)\n"
        "dsrav $10, $9, $11\n sd $10, 16(%1)\n"
        ".set pop\n"
        : : "r"(in), "r"(out) : "$8", "$9", "$10", "$11", "memory");
}
static uint32_t count(void) { uint32_t c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }

static u64s io[2] __attribute__((aligned(8)));
static u64s res[8] __attribute__((aligned(8)));

int main(void)
{
    puts_("int64_test\n");
    uint32_t m; __asm__ volatile("mfc0 %0, $7" : "=r"(m));
    int alu = m >> 31 & 1, sh = m >> 30 & 1, md = m >> 29 & 1, dv = m >> 28 & 1;
    puts_("levels: alu "); putdec(alu); puts_(" shifts "); putdec(sh); puts_(" dmult "); putdec(md); puts_(" ddiv "); putdec(dv); puts_("\n");
    for (int i = 0; i < NVEC; i++) {
        io[0] = vec[i].v[0]; io[1] = vec[i].v[1];
        if (md) {
            run_mdu(io, res);
            check64("dmultu hi", i, res[0], vec[i].v[2]); check64("dmultu lo", i, res[1], vec[i].v[3]);
            check64("dmult hi", i, res[2], vec[i].v[4]); check64("dmult lo", i, res[3], vec[i].v[5]);
        }
        if (sh) {
            run_sh(io, res);
            check64("dsllv", i, res[0], vec[i].v[6]); check64("dsrlv", i, res[1], vec[i].v[7]); check64("dsrav", i, res[2], vec[i].v[8]);
        }
        if (alu) {
            run_log(io, res);
            check64("daddu", i, res[0], vec[i].v[9]); check64("dsubu", i, res[1], vec[i].v[10]);
            check64("slt", i, res[2], vec[i].v[11]); check64("sltu", i, res[3], vec[i].v[12]);
            check64("and", i, res[4], vec[i].v[13]); check64("or", i, res[5], vec[i].v[14]);
            check64("xor", i, res[6], vec[i].v[15]); check64("nor", i, res[7], vec[i].v[16]);
        }
    }
    if (md) {
        /* the time of dmultu + mfhi + mflo, back to back (Count: one tick per two CPU clocks) */
        uint32_t t0 = count();
        for (int i = 0; i < 64; i++) run_mdu(io, res);
        uint32_t t1 = count();
        puts_("dmultu+dmult+4 moves+2 stores: Count ticks per run "); putdec((t1 - t0) / 64); puts_("\n");
    }
    puts_(fails ? "int64_test: FAILED\n" : "int64_test: PASS\n");
    putc_(4);
    for (;;);
}
