/* The VI's anti-alias and divot filters in the scan-out (rtl/soc/n64_video.sv, MIRLO_VI_AA) with the VI in hardware
 * (MIRLO_MON, MIRLO_MI, MIRLO_VI; MONITOR.CTL's VI bit: the scan-out's base, gamma and filters are the VI registers'),
 * on a 320 x 240 framebuffer (the filters are off for a hi-res one, shown at half its width). The game (MCTL.GAME, as vi_test) writes VI_STATUS
 * -- 16-bit, AA_MODE 0 (anti-alias), DIVOT_ENABLE, gamma off -- VI_ORIGIN and H_START; the picture: colour bands with
 * every pixel where (x ^ y) % 7 == 0 partial (its coverage (x + y) % 7: bit 0 and the hidden plane's two -- layout.h
 * LAYOUT_HID), the rest complete (7). sim/mips_soc's VGA=1 writes the pads'
 * frames; sim/vi_filter/viaa_check.py compares the last with vi_model.py's filters on the same picture. */
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include <system.h>

static void putc_(char c) { while (uart_txfull_read()); uart_rxtx_write((uint8_t)c); }
static void puts_(const char *s) { while (*s) putc_(*s++); }

#define RES ((volatile uint32_t *)0xA0000400u)       /* the game's arguments, RDRAM 0x400 (KSEG1) */
#define FB_PHYS 0x00100000u                          /* the framebuffer in the RDRAM */
#define W 320u
#define H 240u

/* the monitor: any entry is unexpected here (counted at 0xFFFF_8008) */
__asm__(
"    .set noreorder\n"
"    .set noat\n"
"    .set mips3\n"
"    .text\n"
"    .globl exception_entry\n"
"exception_entry:\n"
"    sw    $k0, -0x8000($zero)\n"
"    sw    $k1, -0x7FFC($zero)\n"
"    lw    $k0, -0x7FF8($zero)\n"
"    addiu $k0, $k0, 1\n"
"    sw    $k0, -0x7FF8($zero)\n"
"    mfc0  $k0, $30\n"
"    addiu $k0, $k0, 4\n"
"    mtc0  $k0, $30\n"
"    lw    $k0, -0x8000($zero)\n"
"    lw    $k1, -0x7FFC($zero)\n"
"    eret\n"
"    nop\n"
);

/* the game: the VI's registers */
__attribute__((section("migame"), noinline, used, optimize("O1", "no-jump-tables"))) void viaa_game(void)
{
    volatile uint32_t *res = (volatile uint32_t *)0xA0000400u;
    volatile uint32_t *vi  = (volatile uint32_t *)0xA4400000u;
    vi[2] = W;                                       /* WIDTH */
    vi[9] = 0x006C02ECu;                             /* H_START (not 0: not black) */
    vi[1] = res[2];                                  /* ORIGIN */
    vi[0] = res[1];                                  /* STATUS */
}

extern const uint32_t __start_migame[], __stop_migame[];
static void copy_to_rdram(uint32_t phys, const uint32_t *a, const uint32_t *b)
{
    volatile uint32_t *d = (volatile uint32_t *)(uintptr_t)(0xA0000000u + phys);
    while (a < b) *d++ = *a++;
}
typedef void (*game_t)(void);
static void game(uint32_t a1, uint32_t a2)
{
    RES[1] = a1; RES[2] = a2;
    flush_cpu_dcache();
    uint32_t m = 0x302u;
    __asm__ volatile(".set push\n.set mips3\n mtc0 %0, $7\n.set pop" :: "r"(m));
    ((game_t)0x80001000u)();
    __asm__ volatile(".set push\n.set mips3\n li $t0, 0x200\n mtc0 $t0, $7\n.set pop" ::: "t0");
}

/* the picture (viaa_check.py makes the same) */
static uint32_t cvg(uint32_t x, uint32_t y) { return ((x ^ y) % 7u) == 0u ? (x + y) % 7u : 7u; }
static uint16_t pixel(uint32_t x, uint32_t y)
{
    uint32_t r = (x / 40u) & 1u ? 31u : (x * 31u) / W, g = ((x + y) / 24u) & 1u ? 28u : 3u, b = (y * 31u) / H;
    return (uint16_t)(r << 11 | g << 6 | b << 1 | (cvg(x, y) >> 2));
}
#define HID 0x43700000u                              /* the hidden plane (layout.h LAYOUT_HID) */

int main(void)
{
    puts_("viaa_test\n");
    if ((monitor_id_read() & 0xAu) != 0xAu) { puts_("no VI in this SoC\nviaa_test: SKIPPED\n"); putc_(4); for (;;) ; }
    copy_to_rdram(0x1000u, __start_migame, __stop_migame);
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)(0xA0000000u + FB_PHYS);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x += 2) fb[(y * W + x) / 2] = (uint32_t)pixel(x, y) << 16 | pixel(x + 1, y);
    /* the hidden bits: the pixel at RDRAM byte A in bits [2k+1:2k] (k = A[4:1]) of the word at HID + (A >> 5) * 4 */
    volatile uint32_t *hp = (volatile uint32_t *)(uintptr_t)(HID + (FB_PHYS >> 5) * 4u);
    for (uint32_t g = 0; g < W * H / 16u; g++) {
        uint32_t w = 0;
        for (uint32_t k = 0; k < 16u; k++) { uint32_t p = g * 16u + k; w |= (cvg(p % W, p / W) & 3u) << (2u * k); }
        hp[g] = w;
    }
    flush_cpu_dcache();
    video_framebuffer_vtg_mode_write(1);
    video_framebuffer_window_write(0u);
    video_framebuffer_width_write(0u);               /* 320 x 240 */
    video_framebuffer_vtg_enable_write(1);
    video_framebuffer_dma_enable_write(1);
    volatile uint32_t *mr = (volatile uint32_t *)0xFFFF8000u;
    for (int i = 0; i < 8; i++) mr[i] = 0;
    monitor_pend_write(0xF);
    monitor_mask_write(0u);
    monitor_ctl_write(0xDu);                         /* OWN, IP2 the MI's, the VI the hardware's */
    monitor_miclr_write(0x3Eu);
    game(0x3012u, FB_PHYS);                          /* 16-bit, divot, AA_MODE 0, gamma off; the origin */
    puts_("viaa_test: set\n");
    for (;;) ;
}
