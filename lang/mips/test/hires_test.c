/* The scan-out's hi-res sizes (rtl/soc/n64_video.sv, VIDEO_FRAMEBUFFER_WIDTH
 * bit 17 and _WINDOW): in turn -- every one goes out in the one 320 x 240 frame
 *   1  640 x 480 interlaced: the first field alone (the even rows), pixel pairs averaged
 *   2  640 x 240 (non-interlaced): pixel pairs averaged
 *   3  640 x 480i with a 474-row window (237 a field, a black line above)
 *   4  320 x 240
 *   5  292 x 216 inside 320 x 240 (a framebuffer smaller than its frame)
 * The picture: a white frame at its edge, a diagonal, a vertical line every 16
 * pixels, a tick at each row that is a multiple of 8 on the left edge (long:
 * 64), odd rows with their blue lowered -- a weave of the two fields shows as
 * stripes if rows are taken in the wrong order (size 1 must show none: the even
 * rows only). sim/mips_soc's VGA=1 writes what the pads send (v*.ppm). */
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include <system.h>

static void putc_(char c) { while (uart_txfull_read()); uart_rxtx_write((uint8_t)c); }
static void puts_(const char *s) { while (*s) putc_(*s++); }
void trap_handler(void) { puts_("TRAP\n"); for (;;) ; }

#define FB ((volatile uint16_t *)0x40C00000u)
#define HOLD 4u                          /* twentieths of a second a size stays */
static const struct { uint16_t w, h, win_rows, tall; } s_m[] = {
    { 640, 480, 0, 1 }, { 640, 240, 0, 0 }, { 640, 480, 474, 1 }, { 320, 240, 0, 0 }, { 292, 216, 216, 0 },
};

int main(void)
{
    puts_("hires_test\n");
    video_framebuffer_vtg_mode_write(1);
    video_framebuffer_dma_base_write(0x40C00000u);
    video_framebuffer_vtg_enable_write(1);
    video_framebuffer_dma_enable_write(1);
    for (;;) for (unsigned i = 0; i < sizeof s_m / sizeof s_m[0]; i++) {
        const uint32_t w = s_m[i].w, h = s_m[i].h;
        for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
            uint16_t c = (uint16_t)((x * 31u / w) << 11 | (y * 31u / h) << 6 | ((y & 1u) ? 0x0003u : 0x001Fu) << 1 | 1u);
            if (x == 0 || y == 0 || x == w - 1u || y == h - 1u) c = 0xFFFFu;
            else if (x * h == y * w || (x & 15u) == 0u) c = 0xFFC1u;
            else if (x < ((y & 63u) == 0u ? 64u : (y & 7u) == 0u ? 16u : 0u)) c = 0xFFFFu;
            FB[y * w + x] = c;                      /* (big-endian: pixel x is the word's high half when x is even) */
        }
        flush_cpu_dcache();
        if (w >= 512u) {
            /* (an interlaced picture's window is a field's: half its rows) */
            const uint32_t r = s_m[i].tall ? s_m[i].win_rows / 2u : s_m[i].win_rows;
            video_framebuffer_window_write(r ? ((240u - r) / 2u) << 8 | r << 16 : 0u);
            video_framebuffer_width_write(w | (s_m[i].tall ? 1u << 17 : 0u));
        } else {
            video_framebuffer_window_write(w < 320u ? (320u - w) / 2u | ((240u - h) / 2u) << 8 | (h < 240u ? h : 0u) << 16 : 0u);
            video_framebuffer_width_write(w < 320u ? w : 0u);
        }
        puts_("size "); putc_('1' + (char)i); puts_("\n");
        timer0_uptime_latch_write(1);
        uint64_t t0 = timer0_uptime_cycles_read();
        for (;;) { timer0_uptime_latch_write(1); if (timer0_uptime_cycles_read() - t0 > (uint64_t)CONFIG_CLOCK_FREQUENCY / 20u * HOLD) break; }
    }
}
