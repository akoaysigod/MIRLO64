/* The AI in hardware (the SoC built with MIRLO_MON, MIRLO_MI, MIRLO_AI; sim/mips_soc; rtl/soc/rcp_ai.sv).
 *
 * The "game" is C code copied to the N64's RDRAM (0x8000_1000, so mips_core takes it for the game's) with its own
 * exception handler at 0x8000_0180, run with MCTL.GAME and the pass-through on (as mi_test, vi_test): the AI's registers
 * (0xA450_0000) are real bus accesses, no address error, no entry into the monitor. The monitor's level (main) reaches
 * them as plain accesses too, and plays the runtime: it writes the resampler's step (MONITOR.AIRATE).
 *
 * Checked, phase by phase:
 *   1  (the game's accesses, the DMA off: CONTROL 0) a LEN of 0 or of less than 8 bytes is ignored; the first buffer
 *      starts at once (STATUS busy, AI_LEN its length), the second is queued (STATUS full: bit 31 and bit 0), a third is
 *      dropped; DRAM_ADDR, CONTROL, DACRATE and BITRATE read as AI_LEN; a write of DACRATE is seen by the monitor
 *      (PEND bit 3 with MONITOR.CTL's AI bit) and read back through MONITOR.AIRATE, 14 bits
 *   2  the DMA on, the Pocket's FIFO not played: the AI primes it with silence up to 256 words and goes no further
 *      (nothing is read from the buffers: AI_LEN as it was); then played: the buffers are read, AI_LEN counts down to 0
 *      and on to the second buffer's length at the first's end, the AI interrupt (MI_INTR's AI bit, Cause.IP2) is the console's: one when a buffer starts on an idle AI (phase 1: the first LEN, none for a length that is
 *      ignored or for the queued one) and one when the queued buffer takes over (the handler: it clears it with a write to
 *      AI_STATUS, at once), none when the queue empties. STATUS full goes at the first's end and busy at the second's.
 *      STATUS' bits 24 and 20 read 1 and bit 25 is the DMA enable
 *   3  CONTROL 0 stops the reader where it is, 1 goes on
 *   5  the PI's engine copies 4096 words while the AI reads: the copy is right
 *   4  three more steps (the bench's AILOG has what is read and what is written, tools/ai_check.py the algorithm):
 *      32 kHz (43690 = 0.667 frames per sample), 1.5 and 2.625 frames per sample
 * The buffers hold a pseudo-random pattern (lcg, below: tools/ai_check.py has the same). */
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include <system.h>

static void putc_(char c) { while (uart_txfull_read()); uart_rxtx_write((uint8_t)c); }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void puthex(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15]); }
static int fails;
static void check(const char *what, uint32_t got, uint32_t want)
{
    if (got == want) return;
    fails++;
    puts_("FAIL "); puts_(what); puts_(": got "); puthex(got); puts_(" want "); puthex(want); puts_("\n");
}
static void check_range(const char *what, uint32_t got, uint32_t lo, uint32_t hi)
{
    if (got >= lo && got <= hi) return;
    fails++;
    puts_("FAIL "); puts_(what); puts_(": got "); puthex(got); puts_(" want "); puthex(lo); puts_(".."); puthex(hi); puts_("\n");
}

#define RES ((volatile uint32_t *)0xA0000400u)       /* the game's results, RDRAM 0x400 (KSEG1) */
#define AI  ((volatile uint32_t *)0xA4500000u)
#define MI  ((volatile uint32_t *)0xA4300000u)

/* the monitor: its RAM (0xFFFF_8000) counts [2] entries; any entry is unexpected here */
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
"    sw    $k0, -0x7FF8($zero)\n"                    /* entries */
"    mfc0  $k0, $7\n"
"    srl   $k1, $k0, 3\n"
"    andi  $k1, $k1, 31\n"
"    sw    $k1, -0x7FEC($zero)\n"                    /* the cause of the last */
"    mfc0  $k0, $30\n"
"    addiu $k0, $k0, 4\n"
"    mtc0  $k0, $30\n"
"    lw    $k0, -0x8000($zero)\n"
"    lw    $k1, -0x7FFC($zero)\n"
"    eret\n"
"    nop\n"
);

/* the game's handler, at 0x8000_0180: Cause (RES[32]), MI_INTR (RES[33]); with n the runs so far (RES[35]): AI_LEN
 * (RES[64+n]), AI_STATUS (RES[80+n]) as the handler found them, MI_INTR after it cleared the interrupt with a write to
 * AI_STATUS (RES[96+n]), and the same for Cause.IP2 (RES[112+n] bit 10) */
extern const uint32_t ghandler_start[], ghandler_end[];
__asm__(
"    .set noreorder\n"
"    .set noat\n"
"    .set mips3\n"
"    .text\n"
"    .align 2\n"
"ghandler_start:\n"
"    mfc0  $k0, $13\n"
"    lui   $k1, 0xA000\n"
"    sw    $k0, 0x480($k1)\n"                           /* RES[32]: Cause */
"    lui   $k0, 0xA430\n"
"    lw    $k0, 8($k0)\n"
"    nop\n"
"    sw    $k0, 0x484($k1)\n"                           /* RES[33]: MI_INTR */
"    lw    $k0, 0x48C($k1)\n"                           /* n: runs so far */
"    nop\n"
"    andi  $k0, $k0, 15\n"
"    sll   $k0, $k0, 2\n"
"    addu  $k1, $k1, $k0\n"                             /* the ring's slot */
"    lui   $k0, 0xA450\n"
"    lw    $k0, 4($k0)\n"                                /* AI_LEN */
"    nop\n"
"    sw    $k0, 0x500($k1)\n"                            /* RES[64 + n] */
"    lui   $k0, 0xA450\n"
"    lw    $k0, 0xC($k0)\n"                              /* AI_STATUS */
"    nop\n"
"    sw    $k0, 0x540($k1)\n"                            /* RES[80 + n] */
"    lui   $k0, 0xA450\n"
"    sw    $zero, 0xC($k0)\n"                            /* AI_STATUS: clears the interrupt (the hardware) */
"    lui   $k0, 0xA430\n"
"    lw    $k0, 8($k0)\n"
"    nop\n"
"    sw    $k0, 0x580($k1)\n"                            /* RES[96 + n]: MI_INTR after the clear */
"    mfc0  $k0, $13\n"
"    nop\n"
"    sw    $k0, 0x5C0($k1)\n"                            /* RES[112 + n]: Cause after the clear */
"    lui   $k1, 0xA000\n"
"    lw    $k0, 0x48C($k1)\n"
"    addiu $k0, $k0, 1\n"
"    sw    $k0, 0x48C($k1)\n"                           /* RES[35]: runs */
"    eret\n"
"    nop\n"
"ghandler_end:\n"
);

static inline uint32_t getcause(void) { uint32_t v; __asm__ volatile("mfc0 %0, $13" : "=r"(v)); return v; }

/* the game's phases (section "migame": __start_migame .. __stop_migame, copied to the RDRAM) */
__attribute__((section("migame"), noinline, used, optimize("O1", "no-jump-tables"))) void ai_game(void)
{
    volatile uint32_t *res = (volatile uint32_t *)0xA0000400u;
    volatile uint32_t *ai  = (volatile uint32_t *)0xA4500000u;
    volatile uint32_t *mi  = (volatile uint32_t *)0xA4300000u;
    uint32_t n, phase = res[0];
    if (phase == 1) {                               /* the registers, the queue (res[1], res[2]: the buffers' addresses; length res[3], res[4]) */
        res[8]  = ai[3];                            /* idle: STATUS 0 */
        res[9]  = ai[1];                            /* idle: AI_LEN 0 */
        ai[0] = res[1]; ai[1] = 4;                  /* a length of less than 8 bytes: ignored */
        res[10] = ai[3];
        ai[1] = 0;
        res[11] = ai[3];
        res[26] = mi[2];                            /* (no buffer has started: no interrupt) */
        ai[0] = res[1]; ai[1] = res[3];             /* the first: starts at once */
        res[12] = ai[3];
        res[13] = ai[1];
        res[23] = mi[2];                            /* ... and the AI interrupt: a buffer started on an idle AI */
        ai[3] = 0;                                  /* (the handler's acknowledge) */
        res[14] = ai[0]; res[15] = ai[2]; res[16] = ai[4]; res[17] = ai[5];     /* the others read as AI_LEN */
        ai[0] = res[2]; ai[1] = res[4];             /* the second: queued */
        res[27] = mi[2];                            /* ... no interrupt */
        res[18] = ai[3];
        res[19] = ai[1];
        ai[0] = res[2] + 0x1000u; ai[1] = 0x100u;   /* a third: dropped */
        res[20] = ai[3];
        res[21] = ai[1];
        ai[4] = 0xFFFF4321u;                        /* DACRATE: 14 bits */
        ai[5] = 0x5u;                               /* BITRATE: ignored */
        res[22] = ai[1];
        ai[3] = 0;
    } else if (phase == 2) {                        /* the DMA on; the interrupt on: wait for res[1] interrupts, or the loop's end */
        uint32_t st;
        mi[3] = 0x20;                               /* AI's mask on */
        ai[2] = 1;                                  /* CONTROL: DMA on */
        __asm__ volatile("mfc0 %0, $12" : "=r"(st));
        __asm__ volatile("mtc0 %0, $12" :: "r"(st | 0x401u));
        for (n = 0; n < 40000000u && res[35] < res[1]; n++) { }
        __asm__ volatile("mtc0 %0, $12" :: "r"(st));
        mi[3] = 0x10;
    }
}

extern const uint32_t __start_migame[], __stop_migame[];
static void copy_to_rdram(uint32_t phys, const uint32_t *a, const uint32_t *b)
{
    volatile uint32_t *d = (volatile uint32_t *)(uintptr_t)(0xA0000000u + phys);
    while (a < b) *d++ = *a++;
}

typedef void (*game_t)(void);
static void game(uint32_t phase, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    RES[0] = phase; RES[1] = a1; RES[2] = a2; RES[3] = a3; RES[4] = a4;
    flush_cpu_dcache();
    uint32_t m = 0x302u;
    __asm__ volatile(".set push\n.set mips3\n mtc0 %0, $7\n.set pop" :: "r"(m));
    ((game_t)0x80001000u)();
    __asm__ volatile(".set push\n.set mips3\n li $t0, 0x300\n mtc0 $t0, $7\n.set pop" ::: "t0");
}

static uint32_t now(void) { timer0_uptime_latch_write(1); return (uint32_t)timer0_uptime_cycles_read(); }
static void wait_cycles(uint32_t n) { uint32_t t0 = now(); while (now() - t0 < n) { } }

/* the pattern (tools/ai_check.py): a 32-bit LCG, a word of it a frame, from the seed on */
static uint32_t lcg_s;
static uint32_t lcg(void) { lcg_s = lcg_s * 1664525u + 1013904223u; return lcg_s ^ (lcg_s >> 15); }
static void fill_buf(uint32_t phys, uint32_t bytes)
{
    volatile uint32_t *d = (volatile uint32_t *)(uintptr_t)(0xA0000000u + phys);
    for (uint32_t i = 0; i < bytes / 4; i++) d[i] = lcg();
}

#define A1 0x00200000u
#define A2 0x00201000u
#define L1 0x400u                                    /* 256 frames */
#define L2 0x200u                                    /* 128 */

/* plays [phys, bytes) in two halves (two buffers) with the step, and waits for the two interrupts: the game phase 2 */
static void play_two(uint32_t phys, uint32_t bytes, uint32_t step)
{
    monitor_airate_write(step);
    AI[3] = 0;
    fill_buf(phys, bytes);
    AI[0] = phys; AI[1] = bytes / 2;
    AI[0] = phys + bytes / 2; AI[1] = bytes / 2;
    RES[35] = 0;
    game(2, 2, 0, 0, 0);                             /* the interrupts: the first buffer's start, the second's */
    for (uint32_t n = 0; n < 2000000u && AI[1] != 0u; n++) { }      /* (and the second plays to its end) */
    wait_cycles(20000);
}

int main(void)
{
    puts_("ai_test\n");
    uint32_t id = monitor_id_read();
    if ((id & 0x12u) != 0x12u) { puts_("no AI in this SoC\nai_test: SKIPPED\n"); putc_(4); for (;;) ; }
    copy_to_rdram(0x1000u, __start_migame, __stop_migame);
    copy_to_rdram(0x0180u, ghandler_start, ghandler_end);
    for (int i = 0; i < 128; i++) RES[i] = 0;
    volatile uint32_t *mr = (volatile uint32_t *)0xFFFF8000u;
    for (int i = 0; i < 8; i++) mr[i] = 0;
    apf_audio_playback_en_write(0);
    apf_audio_buffer_flush_write(1);
    wait_cycles(1000);
    monitor_pend_write(0xF);
    monitor_mask_write(0u);                              /* nothing wakes the monitor */
    monitor_ctl_write(0x15u);                            /* OWN, IP2 is the MI's line, the AI is the hardware's */
    monitor_miclr_write(0x3Eu);
    check("MONITOR.CTL reads back OWN, MI, AI", monitor_ctl_read(), 0x15u);
    __asm__ volatile(".set push\n.set mips3\n li $t0, 0x300\n mtc0 $t0, $7\n.set pop" ::: "t0");   /* the monitor's accesses too */

    /* 1: registers and the queue */
    lcg_s = 1;
    fill_buf(A1, L1); fill_buf(A2, L2);
    game(1, A1, A2, L1, L2);
    check("idle: STATUS, bits 24 and 20 only (the DMA is off)", RES[8], 0x01100000u);
    check("idle: AI_LEN 0", RES[9], 0u);
    check("a length of 4 bytes: ignored (STATUS idle)", RES[10], 0x01100000u);
    check("a length of 0: ignored (STATUS idle)", RES[11], 0x01100000u);
    check("no interrupt for a length that is ignored", RES[26] & 4u, 0u);
    check("the first buffer: STATUS busy, not full", RES[12], 0x41100000u);
    check("the AI interrupt for a buffer started on an idle AI", RES[23] & 4u, 4u);
    check("the first buffer: AI_LEN", RES[13], L1);
    check("DRAM_ADDR reads as AI_LEN", RES[14], L1);
    check("CONTROL reads as AI_LEN", RES[15], L1);
    check("DACRATE reads as AI_LEN", RES[16], L1);
    check("BITRATE reads as AI_LEN", RES[17], L1);
    check("no interrupt for the queued buffer", RES[27] & 4u, 0u);
    check("the second: STATUS busy and full (bits 31, 30, 0)", RES[18], 0xC1100001u);
    check("the second: AI_LEN is still the first's", RES[19], L1);
    check("a third: dropped, STATUS as it was", RES[20], 0xC1100001u);
    check("a third: AI_LEN as it was", RES[21], L1);
    check("... and after DACRATE and BITRATE", RES[22], L1);
    check("PEND bit 3: DACRATE written", monitor_pend_read() & 8u, 8u);
    check("MONITOR.AIRATE reads DACRATE, 14 bits", monitor_airate_read(), 0x0321u);
    monitor_pend_write(0xF);
    check("PEND bit 3 cleared", monitor_pend_read() & 8u, 0u);

    /* 2: the DMA on, the FIFO not played */
    monitor_airate_write(0x10000u);                      /* 1 frame a sample */
    AI[2] = 1;
    wait_cycles(20000);
    check("the FIFO is primed with silence up to 256 words", apf_audio_buffer_fill_read(), 256u);
    check("nothing is read from the buffers yet: AI_LEN", AI[1], L1);
    check("STATUS (the DMA on: bit 25)", AI[3], 0xC3100001u);
    AI[2] = 0;                                           /* (the game phase 2 turns it on again) */
    apf_audio_playback_en_write(1);                      /* played */
    game(2, 1, 0, 0, 0);
    check("the game's handler ran once: the queued buffer took over", RES[35], 1u);
    check("handler 1: Cause code 0, an interrupt", (RES[32] >> 2) & 31u, 0u);
    check("handler: Cause.IP2", (RES[32] >> 10) & 1u, 1u);
    check("handler 1: MI_INTR has AI", RES[33] & 4u, 4u);
    check_range("handler 1: AI_LEN is the second buffer's, just started", RES[64], L2 - 64u, L2);
    check("handler 1: STATUS busy, not full", RES[80], 0x43100000u);
    check("handler 1: MI_INTR cleared at once by the write to AI_STATUS", RES[96] & 4u, 0u);
    check("handler 1: Cause.IP2 down at once", (RES[112] >> 10) & 1u, 0u);
    for (uint32_t n = 0; n < 2000000u && AI[1] != 0u; n++) { }      /* (the second plays to its end) */
    wait_cycles(20000);
    check("no interrupt when the queue empties: the handler ran once", RES[35], 1u);
    check("MI_INTR has no AI left", MI[2] & 4u, 0u);
    check("STATUS idle (the DMA on)", AI[3], 0x03100000u);
    check("AI_LEN 0", AI[1], 0u);

    /* 3: CONTROL stops the reader; a long buffer, played */
    apf_audio_buffer_flush_write(1);
    wait_cycles(1000);
    AI[2] = 1;
    fill_buf(0x00210000u, 0x800u);
    AI[0] = 0x00210000u; AI[1] = 0x800u;
    wait_cycles(100000);                                 /* 1.6 ms: priming over, 76 samples (4 bytes each) read */
    uint32_t l0 = AI[1];
    check_range("the reader is on: AI_LEN counts down", l0, 0x800u - 4u * 120u, 0x800u - 4u * 20u);
    AI[2] = 0;
    wait_cycles(1000);
    uint32_t l1 = AI[1];
    wait_cycles(200000);                                 /* 3.2 ms */
    check("CONTROL 0: the reader stops where it is", AI[1], l1);
    AI[2] = 1;
    wait_cycles(200000);
    check_range("CONTROL 1: and goes on", AI[1], 0u, l1 - 4u * 20u);
    puts_("AI_LEN "); puthex(0x800u); puts_(" -> "); puthex(l0); puts_(" -> "); puthex(l1); puts_(" -> "); puthex(AI[1]); puts_("\n");

    for (uint32_t n = 0; n < 2000000u && AI[1] != 0u; n++) { }       /* (it plays to its end, and is gone from) */
    wait_cycles(20000);
    AI[3] = 0;

    /* 4: the other steps: AILOG has the frames and the words, tools/ai_check.py the algorithm */
    uint32_t steps[3] = { 43690u, 0x18000u, 0x2A000u };
    for (int k = 0; k < 3; k++) {
        apf_audio_buffer_flush_write(1);
        wait_cycles(2000);
        play_two(0x00220000u + (uint32_t)k * 0x4000u, 0x800u, steps[k]);
        puts_("step "); puthex(steps[k]); puts_(": interrupts "); puthex(RES[35]); puts_("\n");
        check("the two interrupts", RES[35] >= 2u, 1u);
    }

    /* 5: the PI's engine copying while the AI reads: the engine's word is not the AI's (they share the port's data register) */
    {
        apf_audio_buffer_flush_write(1);
        wait_cycles(2000);
        monitor_airate_write(0x10000u);
        AI[3] = 0;
        fill_buf(0x00250000u, 0x4000u);                  /* a buffer the AI plays meanwhile; and the source: 4096 words */
        fill_buf(0x00230000u, 0x4000u);
        volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)(0xA0000000u + 0x00240000u);
        for (int i = 0; i < 4096; i++) dst[i] = 0;
        AI[2] = 1;
        AI[0] = 0x00250000u; AI[1] = 0x4000u;
        wait_cycles(60000);                              /* (primed, reading) */
        monitor_pidst_write(0x00240000u >> 2);
        monitor_pisrc_write((0x03800000u + 0x00230000u) >> 2);
        monitor_pilen_write(4096u);
        for (uint32_t n = 0; n < 4000000u && (monitor_pilen_read() & 0x80000000u); n++) { }
        uint32_t bad = 0;
        volatile uint32_t *src = (volatile uint32_t *)(uintptr_t)(0xA0000000u + 0x00230000u);
        for (int i = 0; i < 4096; i++) if (dst[i] != src[i]) bad++;
        check("the PI's copy while the AI reads: every word", bad, 0u);
        check_range("... and the AI was reading", AI[1], 0u, 0x4000u - 4u * 20u);
        AI[2] = 0;
    }

    puts_("entries "); puthex(mr[2]); puts_(" (the monitor was not to be entered by the AI), last cause "); puthex(mr[4]); puts_("\n");
    check("no entry into the monitor for an AI access", mr[2], 0u);
    monitor_ctl_write(0u);
    puts_(fails ? "ai_test: FAILED\n" : "ai_test: PASS\n");
    putc_(4);
    for (;;);
}
