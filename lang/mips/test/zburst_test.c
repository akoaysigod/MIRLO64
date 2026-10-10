/* The RDP's depth memory in the PSRAM (MIRLO_Z_PSRAM), with the RDP's own
 * bursts: psram_test reaches die 0 a word a burst, from the game CPU; a
 * span's depth is read and written many words a burst, and that is what a
 * game's picture depends on. Here the RDP draws, with every latency
 * adjustment in turn:
 *   1. the screen green in stripes five pixels wide (then two: a word of
 *      the memory is two pixels), their depth written (no compare): near
 *      and far in turn;
 *   2. the whole screen red at a depth between the two, compared and not
 *      written: it must pass on the far stripes and nowhere else;
 *   3. the same again in yellow.
 * So a far stripe ends yellow and a near one green. A block is shown yellow
 * where a pixel did the same wrong thing both times (what was written is
 * wrong or in the wrong place, or every read of it is), red where the two
 * compares disagree (a read that went wrong once), white where the colour is
 * none of the three.
 *
 * On the screen, for a Pocket with no cable on it: nine pictures, a third
 * the size, a write's adjustment across and a read's down as psram_test's
 * squares (the middle one is the datasheet's), a block of 3 x 3 pixels in
 * the colour of the worst pixel in it; under each a bar, green when no pixel
 * was wrong. The game CPU reads and writes all over the SDRAM meanwhile, and
 * the datasheet's setting is tried several times. sim/mips_soc prints the
 * counts. */
#include <stdint.h>
#include <generated/csr.h>
#include <system.h>

#define STATE (*(volatile uint32_t *)0x51000000u)
#define RDRAM_K1 0xA0000000u                /* the N64's memory, uncached */
#define RSP_BASE   0x20000000u
#define SP_REG(o)  (*(volatile uint32_t *)(uintptr_t)(RSP_BASE + (o)))
#define SP_MEM_ADDR SP_REG(0x00)
#define SP_DRAM_ADDR SP_REG(0x04)
#define SP_RD_LEN  SP_REG(0x08)
#define SP_STATUS  SP_REG(0x10)
#define SP_DMA_BUSY SP_REG(0x18)
#define SP_PC      SP_REG(0x20)

#define UCODE 0x00001000u                   /* in the N64's memory: the microcode, */
#define CMDS  0x00002000u                   /* the RDP's commands, */
#define CIMG  0x00100000u                   /* the colour image (320 x 240, 16 bits) */
#define ZIMG  0x00200000u                   /* and the depth image's address */

static void putc_(char c) { while (uart_txfull_read()); uart_rxtx_write((uint8_t)c); }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void putu(uint32_t v)
{
    char b[12]; int n = 0;
    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) putc_(b[--n]);
}
void trap_handler(void) { puts_("TRAP\n"); for (;;) ; }

#define FB ((volatile uint16_t *)0x40C00000u)
#define GREEN  0x07C1u
#define RED    0xF801u
#define YELLOW 0xFFC1u
#define BLUE   0x003Fu
#define GREY   0x4211u
static void rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) FB[y * 320 + x] = c;
}

/* the RSP: a microcode that hands the RDP the commands and stops */
static int rdp_run(uint32_t words)
{
    volatile uint32_t *u = (volatile uint32_t *)(uintptr_t)(RDRAM_K1 + UCODE);
    uint32_t end = CMDS + 4u * words;
    u[0] = 0x3C010000u | (CMDS >> 16);  u[1] = 0x34210000u | (CMDS & 0xFFFFu);     /* lui / ori $1: START */
    u[2] = 0x3C020000u | (end >> 16);   u[3] = 0x34420000u | (end & 0xFFFFu);      /* $2: END */
    u[4] = 0x34030015u;                 u[5] = 0x40835800u;                         /* DPC_STATUS: no XBUS, freeze, flush */
    u[6] = 0x40814000u;                 u[7] = 0x40824800u;                         /* mtc0 $1, START; mtc0 $2, END */
    u[8] = 0x0000000Du;                 u[9] = 0u;                                  /* break */
    for (int i = 10; i < 16; i++) u[i] = 0u;
    uint32_t sync0 = mrdp_sync_count_read();
    SP_STATUS = 0x2u;                                    /* halted */
    SP_MEM_ADDR = 0x1000u; SP_DRAM_ADDR = UCODE; SP_RD_LEN = 63u;
    while (SP_DMA_BUSY) ;
    SP_PC = 0;
    SP_STATUS = 0x100u | 0x20u | 0x4u | 0x1u | 0x8u;     /* clear single step, broke, halt, interrupt */
    /* (meanwhile the game CPU in the SDRAM as a game is: lines read all over
     * it, some written -- the pins beside the PSRAMs' busy) */
    static uint32_t x = 0x2545F491u;
    static volatile uint32_t sink;
    for (uint32_t i = 0; i < 6000000u; i++) {
        if ((SP_STATUS & 1u) && mrdp_sync_count_read() != sync0) return 1;
        for (int k = 0; k < 4; k++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            sink += *(volatile uint32_t *)(uintptr_t)(0x41000000u + (x & 0x01FFFFFCu));
            if ((x >> 30) == 0u) *(volatile uint32_t *)(uintptr_t)(0x43000000u + ((x >> 4) & 0x000FFFFCu)) = ~x;
        }
    }
    return 0;
}

/* a triangle over the whole screen (0,0) (960,0) (0,720), one colour, one depth */
static uint32_t *triangle(uint32_t *c, uint32_t rgba, uint32_t z, uint32_t modes)
{
    *c++ = 0xE7000000u; *c++ = 0u;                       /* sync pipe */
    *c++ = 0xEF0000F0u; *c++ = modes;                    /* other modes: one cycle, no dither */
    *c++ = 0xFA000000u; *c++ = rgba;                     /* primitive colour */
    *c++ = 0xEE000000u; *c++ = z << 16;                  /* primitive depth */
    *c++ = 0x09800B40u; *c++ = 0u;                       /* depth-buffered triangle, the long edge on the left; YL 720, YM = YH = 0 */
    *c++ = 0x03C00000u; *c++ = 0xFFFEAAABu;              /* XL 960, -4/3 a line */
    *c++ = 0u; *c++ = 0u;                                /* XH 0 */
    *c++ = 0u; *c++ = 0u;                                /* XM */
    *c++ = z << 16; *c++ = 0u; *c++ = 0u; *c++ = 0u;     /* Z, DzDx, DzDe, DzDy */
    return c;
}

int main(void)
{
    puts_("zburst_test\n");
    rect(0, 0, 320, 240, 0x0001u);
    video_framebuffer_vtg_mode_write(1);
    video_framebuffer_width_write(0);
    video_framebuffer_dma_base_write(0x40C00000u);
    video_framebuffer_vtg_enable_write(1);
    video_framebuffer_dma_enable_write(1);
    uint32_t s = STATE;
    if ((s >> 16) != 0x5052u || !(s & 0x100u)) {
        rect(0, 0, 320, 240, GREY);
        puts_("the RDP's depth is not in the PSRAM in this SoC\nzburst_test: FAILED\n"); putc_(4); for (;;) ;
    }
    for (int i = 0; i < 2000000 && !(STATE & 0x10u); i++) ;
    volatile uint32_t *cmd = (volatile uint32_t *)(uintptr_t)(RDRAM_K1 + CMDS);
    volatile uint16_t *img = (volatile uint16_t *)(uintptr_t)(RDRAM_K1 + CIMG);
    uint32_t bad11 = 0;
    static uint8_t first[320 * 240 / 8], cls[320 * 240];
    for (uint32_t r = 0; r < 3; r++) for (uint32_t w = 0; w < 3; w++) {
        STATE = r << 2 | w;
        for (uint32_t i = 0; i < 320u * 240u; i++) cls[i] = 0;
        int ok = 1;
        /* stripes five pixels wide, then two (a word of the memory is two
         * pixels: every word then differs from the one before it); the
         * datasheet's setting several times over */
        const uint32_t rounds = (w == 1 && r == 1) ? 4u : 1u;
        for (uint32_t pass = 0; pass < 2u * rounds; pass++) {
            const uint32_t sw = (pass & 1u) ? 2u : 5u, ns = 320u / sw;
            for (uint32_t i = 0; i < 320u * 240u; i++) img[i] = BLUE;
            uint32_t *c = (uint32_t *)cmd;
            *c++ = 0xFF10013Fu; *c++ = CIMG;                 /* colour image: 16 bits, 320 wide */
            *c++ = 0xFE000000u; *c++ = ZIMG;                 /* depth image */
            *c++ = 0xFC887F10u; *c++ = 0x88FDF6FBu;          /* combine: the primitive colour */
            for (uint32_t k = 0; k < ns; k++) {              /* green stripes: depth written, near and far in turn */
                *c++ = 0xED000000u | (4u * sw * k) << 12; *c++ = (4u * sw * (k + 1u)) << 12 | 0x3C0u;
                c = triangle(c, 0x00FF00FFu, (k & 1u) ? 0x5000u : 0x3000u, 0x24u);
            }
            *c++ = 0xED000000u; *c++ = 0x005003C0u;
            c = triangle(c, 0xFF0000FFu, 0x4000u, 0x14u);    /* red, between the two: compared only */
            *c++ = 0xE9000000u; *c++ = 0u;                   /* sync full */
            ok &= rdp_run((uint32_t)(c - (uint32_t *)cmd));
            for (uint32_t i = 0; i < 320u * 240u; i++) {
                if ((i & 7u) == 0u) first[i >> 3] = 0;
                if ((img[i] & 0xFFFEu) != (GREEN & 0xFFFEu)) first[i >> 3] |= (uint8_t)(1u << (i & 7u));
            }
            c = (uint32_t *)cmd;
            c = triangle(c, 0xFFFF00FFu, 0x4000u, 0x14u);    /* yellow, the same */
            *c++ = 0xE9000000u; *c++ = 0u;
            ok &= rdp_run((uint32_t)(c - (uint32_t *)cmd));
            for (uint32_t i = 0; i < 320u * 240u; i++) {
                uint32_t v = img[i] & 0xFFFEu;
                int far = (int)((i % 320u) / sw) & 1, p1 = !((first[i >> 3] >> (i & 7u)) & 1u);     /* (p1: green after step 2) */
                uint8_t k = v == (GREEN & 0xFFFEu) ? (far ? (p1 ? 3 : 2) : (p1 ? 0 : 2))
                          : v == (YELLOW & 0xFFFEu) ? (far ? (p1 ? 2 : 0) : (p1 ? 2 : 3)) : 1;
                if (k > cls[i]) cls[i] = k;
            }
        }
        /* the picture, a third the size: a block's worst pixel */
        uint32_t n_red = 0, n_yel = 0, n_oth = 0;
        int ox = 107 * (int)w, oy = 80 * (int)r;
        for (uint32_t i = 0; i < 320u * 240u; i++) { if (cls[i] == 2) n_red++; else if (cls[i] == 3) n_yel++; else if (cls[i] == 1) n_oth++; }
        for (int by = 0; by < 72; by++) for (int bx = 0; bx < 106; bx++) {
            int worst = 0;
            for (int y = by * 3; y < by * 3 + 3; y++) for (int x = bx * 3; x < bx * 3 + 3 && x < 320; x++)
                if (cls[y * 320 + x] > worst) worst = cls[y * 320 + x];
            FB[(oy + by) * 320 + ox + bx] = worst == 0 ? 0x0301u : worst == 1 ? 0xFFFFu : worst == 2 ? RED : YELLOW;
        }
        uint32_t bad = n_red + n_yel + n_oth;
        rect(ox, oy + 73, ox + 106, oy + 78, !ok ? GREY : bad ? RED : GREEN);
        puts_("write "); putc_('0' + (char)w); puts_(" read "); putc_('0' + (char)r);
        puts_(ok ? ": " : " (the RDP did not end): "); putu(n_red); puts_(" red, "); putu(n_yel); puts_(" yellow, "); putu(n_oth); puts_(" other\n");
        if (w == 1 && r == 1) bad11 = bad + (ok ? 0u : 1u);
    }
    STATE = 0x5u;
    puts_(bad11 ? "zburst_test: FAILED\n" : "zburst_test: PASS\n");
    putc_(4);
    for (;;) ;
}
