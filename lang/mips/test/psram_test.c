/* The Pocket's PSRAMs on MIRLO's MIPS SoC (rtl/soc/psram.sv; the SoC built
 * with MIRLO_Z_PSRAM and / or MIRLO_ROM64), from the game CPU:
 *   0x5100_0000  the block: 0x5052 in the upper half, bit 4 ready, bit 5 a
 *                read's data came with WAIT asserted (since the register was
 *                last written), bit 6 the depth memory
 *                is there (MIRLO_Z_PSRAM), bit 7 the bridge writes a file's
 *                end to die 1 (MIRLO_ROM64), bits 3-0 the bursts' latency
 *                adjustments (write, read; 1 each is the datasheet's)
 *   0x5000_0000  die 1, 16 MiB, a word at a time (asynchronous)
 *   0x5200_0000  die 0, the RDP's depth memory, a word a burst
 * Words of both dies written and read back: every data line, every address
 * line, the byte lanes. sim/mips_soc runs it against the chips' model; on the
 * Pocket it tells whether the chips answer (and with which adjustment). */
#include <stdint.h>
#include <generated/csr.h>
#include <system.h>

static void putc_(char c) { while (uart_txfull_read()); uart_rxtx_write((uint8_t)c); }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void puthex(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15]); }
static int fails;
static void check(const char *what, uint32_t at, uint32_t got, uint32_t want)
{
    if (got == want) return;
    if (fails++ < 12) { puts_("FAIL "); puts_(what); puts_(" "); puthex(at); puts_(": got "); puthex(got); puts_(" want "); puthex(want); puts_("\n"); }
}
#define STATE (*(volatile uint32_t *)0x51000000u)
static uint32_t pat(uint32_t i) { return i * 2654435761u ^ 0x5EED1234u; }

static void window(const char *name, volatile uint32_t *w, uint32_t words)
{
    /* data lines: a walking one, and its complement */
    for (uint32_t b = 0; b < 32; b++) { w[b] = 1u << b; w[32 + b] = ~(1u << b); }
    for (uint32_t b = 0; b < 32; b++) { check(name, b, w[b], 1u << b); check(name, 32 + b, w[32 + b], ~(1u << b)); }
    /* address lines: a word at every power of two (and the last), then all read */
    for (uint32_t a = 1; a < words; a <<= 1) w[a] = pat(a);
    w[words - 1] = pat(words - 1); w[0] = pat(0);
    for (uint32_t a = 1; a < words; a <<= 1) check(name, a, w[a], pat(a));
    check(name, words - 1, w[words - 1], pat(words - 1)); check(name, 0, w[0], pat(0));
    /* a row of the chips and the next (a burst may not cross it), consecutive words */
    for (uint32_t a = 120; a < 140; a++) w[a] = pat(a + 7);
    for (uint32_t a = 120; a < 140; a++) check(name, a, w[a], pat(a + 7));
    /* the byte lanes */
    w[200] = 0x11223344u;
    ((volatile uint8_t *)w)[800] = 0xAA; ((volatile uint8_t *)w)[803] = 0xDD;
    check(name, 200, w[200], 0xAA2233DDu);
    ((volatile uint16_t *)w)[401] = 0xBEEF;
    check(name, 200, w[200], 0xAA22BEEFu);
}

/* a quick look at the depth memory's bursts: a few words, far apart */
static int burst_ok(void)
{
    volatile uint32_t *w = (volatile uint32_t *)0x52000000u;
    static const uint32_t at[6] = { 0, 1, 127, 128, 0x12345, 0x3FFFFF };
    for (int i = 0; i < 6; i++) w[at[i]] = pat(at[i] + 99u);
    for (int i = 0; i < 6; i++) if (w[at[i]] != pat(at[i] + 99u)) return 0;
    return 1;
}

/* The result on the screen, for a Pocket with no cable on it (320 x 240,
 * RGBA5551): the upper band is die 1 (the game file's end), the lower one die
 * 0 (the depth memory) -- green: every word came back, red: not, grey: not in
 * this core. Between them nine squares, the depth memory's bursts with each
 * latency adjustment (a write's across, a read's down; the middle one is the
 * datasheet's): green where they work. A yellow bar on the left: a read's
 * data came with WAIT asserted. */
#define FB ((volatile uint16_t *)0x40C00000u)
static void rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) FB[y * 320 + x] = c;
}
#define GREEN 0x07C1u
#define RED   0xF801u
#define GREY  0x4211u
#define YELLOW 0xFFC1u

int main(void)
{
    puts_("psram_test\n");
    rect(0, 0, 320, 240, 0x0001u);
    video_framebuffer_vtg_mode_write(1);
    video_framebuffer_width_write(0);
    video_framebuffer_dma_base_write(0x40C00000u);
    video_framebuffer_vtg_enable_write(1);
    video_framebuffer_dma_enable_write(1);
    uint32_t s = STATE;
    if ((s >> 16) != 0x5052u) {
        rect(0, 0, 320, 240, GREY);
        puts_("no PSRAM block in this SoC (state "); puthex(s); puts_(")\npsram_test: FAILED\n"); putc_(4); for (;;) ;
    }
    for (int i = 0; i < 2000000 && !(STATE & 0x10u); i++) ;
    if (!(STATE & 0x10u)) { puts_("the PSRAMs never became ready\n"); fails++; }
    int f0 = fails;
    window("die 1 (asynchronous)", (volatile uint32_t *)0x50000000u, 1u << 22);
    rect(20, 10, 300, 80, fails == f0 ? GREEN : RED);
    if (STATE & 0x40u) {
        /* the bursts with every latency adjustment, then the whole test with the datasheet's */
        for (uint32_t r = 0; r < 3; r++) for (uint32_t w = 0; w < 3; w++) {
            STATE = r << 2 | w;
            int ok = burst_ok();
            rect(110 + 34 * (int)w, 90 + 20 * (int)r, 140 + 34 * (int)w, 106 + 20 * (int)r, ok ? GREEN : RED);
            puts_("bursts, latency adjustments write "); putc_('0' + (char)w); puts_(" read "); putc_('0' + (char)r); puts_(ok ? ": ok\n" : ": no\n");
        }
        STATE = 0x5u;
        f0 = fails;
        window("die 0 (bursts)", (volatile uint32_t *)0x52000000u, 1u << 22);
        rect(20, 160, 300, 230, fails == f0 ? GREEN : RED);
    } else rect(20, 160, 300, 230, GREY);
    s = STATE;
    puts_("state "); puthex(s); puts_((s & 0x20u) ? " (WAIT was asserted with a read's data)\n" : "\n");
    if (s & 0x20u) { rect(0, 0, 10, 240, YELLOW); fails++; }
    puts_(fails ? "psram_test: FAILED\n" : "psram_test: PASS\n");
    putc_(4);
    for (;;) ;
}
