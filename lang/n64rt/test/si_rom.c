/* si_test's ROM: libultra's own SI code -- osContInit, osContStartReadData,
 * osEeprom*, __osContRamRead / Write, osPfsIsPlug, osMotorInit, the registers
 * as a game that polls them finds them and, on top of all that, pak_test.c's
 * Controller Pak and Transfer Pak battery -- as an N64
 * program, run by n64rt like any game's. Built by the runtime's default the
 * SI's functions are replaced; with SI_GAME=1 they run as they are, on the
 * SI's registers and the PIF as the runtime emulates them, each access a
 * trap (n64rt_io.c). The second half times both: a register's read and
 * write, a replaced function's call, a frame's controller read, an EEPROM
 * block, an accessory's block -- warm, and with nothing of the path left in
 * the caches.
 *
 * What it prints goes to the runtime's log through a message queue the port
 * watches (si_main.c: an N64 program cannot reach the UART). */
#include <stdarg.h>
#include <stdio.h>
#include "PR/os_internal.h"
#include "PR/rcp.h"
#include "controller.h"
#include "PR/os_gbpak.h"
#include "PR/os_motor.h"

u64 osClockRate = OS_CLOCK_RATE;            /* (libultra's osInitialize sets it: replaced) */
OSMesgQueue si_rom_out, si_rom_ctl;         /* never created: the port takes what is sent to them */
volatile u32 si_rom_reply;
unsigned pak_test_kind;                     /* the accessory's setting (the runtime reads it here) */
extern OSMesgQueue pak_test_mq;
int pak_test_main(void);
void si_rom_sled(void);
enum { NONE, CPAK, SLOT };
enum { CTL_BOOT = 1, CTL_PORT, CTL_END };

int si_rom_printf(const char *fmt, ...)
{
    static char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    osSendMesg(&si_rom_out, (OSMesg)line, OS_MESG_NOBLOCK);
    return n;
}
#define printf si_rom_printf
void pak_test_boot(void) { osSendMesg(&si_rom_ctl, (OSMesg)CTL_BOOT, OS_MESG_NOBLOCK); }
int pak_test_slot_port(void) { osSendMesg(&si_rom_ctl, (OSMesg)CTL_PORT, OS_MESG_NOBLOCK); return (int)si_rom_reply; }

static void fill(void *p, int v, unsigned n) { u8 *b = p; while (n--) *b++ = (u8)v; }
static int fails;
static void check(const char *what, s32 got, s32 want)
{
    if (got == want) return;
    fails++;
    printf("FAIL %s: got %ld want %ld\n", what, (long)got, (long)want);
}

static OSMesg s_si_msg[1];
static OSThread s_idle;
static u8 s_idle_stack[1024] __attribute__((aligned(16)));
static void idle(void *arg) { (void)arg; for (;;) ; }

static inline u32 count(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
static OSContPad s_pad[MAXCONTROLLERS];
static OSContStatus s_st[MAXCONTROLLERS];
static void read_pads(void)
{
    osContStartReadData(&pak_test_mq);
    osRecvMesg(&pak_test_mq, NULL, OS_MESG_BLOCK);
    osContGetReadData(s_pad);
}

static void controllers(void)
{
    u8 bits = 0;
    check("osContInit", osContInit(&pak_test_mq, &bits, s_st), 0);
    check("controllers there", bits, 1);
    check("controller 1's type", s_st[0].type, CONT_TYPE_NORMAL);
    check("controller 1 answers", s_st[0].errno, 0);
    check("controller 1 has an accessory", s_st[0].status & CONT_CARD_ON, CONT_CARD_ON);
    check("controller 2 does not answer", s_st[1].errno, CONT_NO_RESPONSE_ERROR);
    pak_test_kind = NONE;
    check("osContStartQuery", osContStartQuery(&pak_test_mq), 0);
    osRecvMesg(&pak_test_mq, NULL, OS_MESG_BLOCK);
    osContGetQuery(s_st);
    check("controller 1 has none now", s_st[0].status & CONT_CARD_ON, 0);
    pak_test_kind = CPAK;
    /* the pad: nothing held, then (the simulation's PAD) A and right, read
     * for up to five seconds of frames */
    read_pads();
    check("controller 1 read", s_pad[0].errno, 0);
    check("controller 2 not read", s_pad[1].errno, CONT_NO_RESPONSE_ERROR);
    printf("  pad at the start: buttons %04x stick %d %d\n", s_pad[0].button, s_pad[0].stick_x, s_pad[0].stick_y);
    OSTime t0 = osGetTime();
    while (!(s_pad[0].button & CONT_A) && osGetTime() - t0 < OS_USEC_TO_CYCLES(5000000)) read_pads();
    printf("  pad after %lu ms: buttons %04x stick %d %d\n", (unsigned long)(OS_CYCLES_TO_USEC(osGetTime() - t0) / 1000),
           s_pad[0].button, s_pad[0].stick_x, s_pad[0].stick_y);
    check("A held", s_pad[0].button & CONT_A, CONT_A);
    check("the stick to the right", s_pad[0].stick_x > 60 && s_pad[0].stick_y == 0, 1);
}

/* The dock's other pads come and go while the game runs: the simulation's PAD2 (a dock pad with analog, A held) is there for a
 * span of fields only. Every poll -- osContStartQuery / osContGetQuery, osContStartReadData / osContGetReadData -- gets the
 * pad as it is NOW: no answer before, the first poll after the pad shows its keys, no answer again after; and the accessory
 * of "Controller 1 pak" is controller 1's alone. A pad that is never there (controller 3) never answers. */
static void hotplug(void)
{
    u8 pattern = 0xFF;
    int state = 0, polls = 0, first_keys = -1, read_on_first = -1, read_after_off = -1, cpak_there = -1, third = 0, start_there = -1;
    OSTime t0 = osGetTime(), t_on = 0, t_off = 0;
    pak_test_kind = CPAK;
    /* (a poll is a query and then a read, so the pad may come or go between the two: the query decides, and what the read
     * after a query that saw the pad must show is the pad too) */
    while (state < 2 && osGetTime() - t0 < OS_USEC_TO_CYCLES(8000000)) {
        osContStartQuery(&pak_test_mq);
        osRecvMesg(&pak_test_mq, NULL, OS_MESG_BLOCK);
        osContGetQuery(s_st);
        read_pads();
        const int q = s_st[1].errno == 0;
        if (polls++ == 0) start_there = q;
        if (s_st[2].errno == 0 || s_pad[2].errno == 0) third++;
        if (state == 0 && q) {
            state = 1; t_on = osGetTime();
            read_on_first = s_pad[1].errno;
            first_keys = s_pad[1].button;
            check("the new pad's type", s_st[1].type, CONT_TYPE_NORMAL);
            check("the new pad has no accessory", s_st[1].status & CONT_CARD_ON, 0);
            osPfsIsPlug(&pak_test_mq, &pattern);
            cpak_there = pattern;
        } else if (state == 1 && !q) {
            state = 2; t_off = osGetTime();
            read_after_off = s_pad[1].errno;
        }
    }
    printf("  hot-plug: started at %lu ms, controller 2 on after %lu ms, off after %lu ms (%d polls in %lu ms)\n",
           (unsigned long)(OS_CYCLES_TO_USEC(t0) / 1000), (unsigned long)(OS_CYCLES_TO_USEC(t_on - t0) / 1000),
           (unsigned long)(OS_CYCLES_TO_USEC(t_off - t0) / 1000), polls, (unsigned long)(OS_CYCLES_TO_USEC(osGetTime() - t0) / 1000));
    check("controller 2 is not there at the start of the hot-plug test (the bench's PAD2 window not yet open)", start_there, 0);
    check("controller 2 appeared and went again", state, 2);
    check("the read after the first query that saw it answers", read_on_first, 0);
    check("the first poll after it appeared has its keys (A)", first_keys & CONT_A, CONT_A);
    check("the read after the first query that missed it does not answer", read_after_off, CONT_NO_RESPONSE_ERROR);
    check("the accessory is controller 1's alone", cpak_there, 1);
    check("controller 3 never answers", third, 0);
    check("controller 2 gone: osContGetQuery", s_st[1].errno, CONT_NO_RESPONSE_ERROR);
    check("controller 2 gone: osContGetReadData", s_pad[1].errno, CONT_NO_RESPONSE_ERROR);
}

/* a game that sets the SI's interrupt no message and looks at the registers:
 * the interrupt's bit in SI_STATUS and in MI_INTR after a transfer, until
 * SI_STATUS is written; and PIF RAM a word at a time */
static void polled(void)
{
    static OSPifRam ram;
    u8 *b = (u8 *)&ram;
    u32 v = 0;
    osSetEventMesg(OS_EVENT_SI, NULL, NULL);
    IO_WRITE(SI_STATUS_REG, 0);
    check("no SI interrupt yet", IO_READ(SI_STATUS_REG) & SI_STATUS_INTERRUPT, 0);
    fill(b, 0, 64);                                 /* controller 1's buttons, the other channels skipped */
    b[0] = 0xFF; b[1] = 1; b[2] = 4; b[3] = CONT_CMD_READ_BUTTON; b[8] = CONT_CMD_END; b[63] = CONT_CMD_EXE;
    check("the block written", __osSiRawStartDma(OS_WRITE, &ram), 0);
    check("SI_STATUS has the interrupt", IO_READ(SI_STATUS_REG) & SI_STATUS_INTERRUPT, SI_STATUS_INTERRUPT);
    check("MI_INTR has it", IO_READ(MI_INTR_REG) & MI_INTR_SI, MI_INTR_SI);
    IO_WRITE(SI_STATUS_REG, 0);
    check("SI_STATUS written: none", IO_READ(SI_STATUS_REG) & SI_STATUS_INTERRUPT, 0);
    check("MI_INTR: none", IO_READ(MI_INTR_REG) & MI_INTR_SI, 0);
    fill(b, 0, 64);
    check("the block read", __osSiRawStartDma(OS_READ, &ram), 0);
    check("the interrupt again", IO_READ(SI_STATUS_REG) & SI_STATUS_INTERRUPT, SI_STATUS_INTERRUPT);
    IO_WRITE(SI_STATUS_REG, 0);
    check("the command back", b[3], CONT_CMD_READ_BUTTON);
    check("A in the block", b[4] & (CONT_A >> 8), CONT_A >> 8);
    check("__osSiRawWriteIo", __osSiRawWriteIo(PIF_RAM_START + 0x38, 0x4D49524Cu), 0);
    check("__osSiRawReadIo", __osSiRawReadIo(PIF_RAM_START + 0x38, &v), 0);
    check("the word of PIF RAM", (s32)v, 0x4D49524C);
    osSetEventMesg(OS_EVENT_SI, &pak_test_mq, (OSMesg)0);
}

static void eeprom(void)
{
    static u8 blk[8] = { 0x4D, 0x49, 0x52, 0x4C, 0x4F, 0x21, 0x00, 0xA5 }, back[32], many[24];
#ifdef SI_EEP16K                                    /* (the 16 Kbit chip, forced at build time: si_force.h) */
    check("osEepromProbe", osEepromProbe(&pak_test_mq), EEPROM_TYPE_16K);
#else
    check("osEepromProbe", osEepromProbe(&pak_test_mq), EEPROM_TYPE_4K);
#endif
    check("osEepromWrite", osEepromWrite(&pak_test_mq, 3, blk), 0);
    check("osEepromRead", osEepromRead(&pak_test_mq, 3, back), 0);
    check("the block read back", bcmp(blk, back, 8), 0);
    for (int i = 0; i < 24; i++) many[i] = (u8)(0xC0 + i);
    OSTime t0 = osGetTime();
    check("osEepromLongWrite", osEepromLongWrite(&pak_test_mq, 61, many, 24), 0);
    printf("  osEepromLongWrite of three blocks: %lu ms\n", (unsigned long)(OS_CYCLES_TO_USEC(osGetTime() - t0) / 1000));
    check("osEepromLongRead", osEepromLongRead(&pak_test_mq, 61, back, 24), 0);
    check("the blocks read back", bcmp(many, back, 24), 0);
#ifdef SI_EEP16K
    for (int i = 0; i < 24; i++) many[i] = (u8)(0x30 + i);
    check("osEepromLongWrite (blocks 253-255)", osEepromLongWrite(&pak_test_mq, 253, many, 24), 0);
    check("osEepromLongRead (blocks 253-255)", osEepromLongRead(&pak_test_mq, 253, back, 24), 0);
    check("the 16 Kbit blocks read back", bcmp(many, back, 24), 0);
    osEepromRead(&pak_test_mq, 3, back);
    check("block 3 kept", bcmp(blk, back, 8), 0);
#endif
}

static void accessories(void)
{
    static OSPfs pfs;
    u8 pattern = 0xFF;
    s32 r;
    pak_test_kind = CPAK;
    check("osPfsIsPlug", osPfsIsPlug(&pak_test_mq, &pattern), 0);
    check("an accessory in controller 1 only", pattern, 1);
    r = osMotorInit(&pak_test_mq, &pfs, 0);
    check("osMotorInit on a Controller Pak: no Rumble Pak", r == PFS_ERR_DEVICE || r == PFS_ERR_NOPACK, 1);
    pak_test_kind = NONE;
    check("osPfsIsPlug, nothing", osPfsIsPlug(&pak_test_mq, &pattern), 0);
    check("no accessory", pattern, 0);
    check("osMotorInit, nothing", osMotorInit(&pak_test_mq, &pfs, 0), PFS_ERR_NOPACK);
    pak_test_kind = CPAK;
}

/* ---- times: Count's ticks (two of the SoC's clocks each), the least of several runs */
static u8 s_wash[16384];
static void wash(void)                      /* nothing of before in either cache */
{
    volatile u8 *p = s_wash;
    si_rom_sled();
    for (unsigned i = 0; i < sizeof s_wash; i += 16) (void)p[i];
}
static u8 s_blk[32];
static volatile u32 s_sink;
static void op_none(void) { }
static void op_ram(void) { s_sink = *(volatile u32 *)0xA0000300u; }
static void op_trap_read(void) { s_sink = IO_READ(SI_STATUS_REG); }
static void op_trap_write(void) { IO_WRITE(SI_DRAM_ADDR_REG, 0x00100000u); }
static void op_replaced(void) { s_sink = osVirtualToPhysical(s_blk); }
static void op_pads(void) { read_pads(); }
static void op_eep_read(void) { osEepromRead(&pak_test_mq, 5, s_blk); }
static void op_eep_write(void) { osEepromWrite(&pak_test_mq, 5, s_blk); }
static void op_pak_read(void) { __osContRamRead(&pak_test_mq, 0, 0x300 >> 5, s_blk); }
static void op_pak_write(void) { __osContRamWrite(&pak_test_mq, 0, 0x7F00 >> 5, s_blk, 0); }
static void op_isplug(void) { u8 p; osPfsIsPlug(&pak_test_mq, &p); }
static u32 best(void (*op)(void), int n, int cold)
{
    u32 least = 0xFFFFFFFFu;
    for (int run = 0; run < 6; run++) {
        u32 t0 = count();
        for (int i = 0; i < n; i++) { if (cold) wash(); op(); }
        u32 t = count() - t0;
        if (t < least) least = t;
    }
    return least;
}
static void time_op(const char *what, void (*op)(void))
{
    const int n = 64;
    u32 w0 = best(op_none, n, 0), c0 = best(op_none, 4, 1);
    u32 w = best(op, n, 0), c = best(op, 4, 1);
    printf("  %-34s %6lu clocks warm, %6lu cold\n", what, (unsigned long)((w - w0) * 2u / (u32)n),
           (unsigned long)(c > c0 ? (c - c0) * 2u / 4u : 0u));
}
static void times(void)
{
    pak_test_kind = CPAK;
    pak_test_boot();
    printf("si_test: times (a frame is 1040000 clocks)\n");
    time_op("a word of RDRAM, uncached", op_ram);
    time_op("SI_STATUS read (a trap)", op_trap_read);
    time_op("SI_DRAM_ADDR written (a trap)", op_trap_write);
    time_op("osVirtualToPhysical (replaced)", op_replaced);
    time_op("a frame's controller read", op_pads);
    time_op("osEepromRead, a block", op_eep_read);
    time_op("osEepromWrite, a block", op_eep_write);
    time_op("__osContRamRead, a block", op_pak_read);
    time_op("__osContRamWrite, a block", op_pak_write);
    time_op("osPfsIsPlug", op_isplug);
}

void si_rom_main(void)
{
    osCreateThread(&s_idle, 1, idle, NULL, s_idle_stack + sizeof s_idle_stack, 0);
    osStartThread(&s_idle);
    osCreateMesgQueue(&pak_test_mq, s_si_msg, 1);
    osSetEventMesg(OS_EVENT_SI, &pak_test_mq, (OSMesg)0);
    printf("si_test\n");
    pak_test_kind = CPAK;
    pak_test_boot();
    controllers();
    hotplug();
#ifdef SI_HP_ONLY                                    /* (a quick run of this part: make HPONLY=1) */
    static volatile int hp_only = 1;
    if (hp_only) {
        printf(fails ? "si_test: FAILED\n" : "si_test: PASS\n");
        osSendMesg(&si_rom_ctl, (OSMesg)CTL_END, OS_MESG_NOBLOCK);
        for (;;) ;
    }
#endif
    polled();
    eeprom();
    accessories();
    fails += pak_test_main();
    times();
    printf(fails ? "si_test: FAILED\n" : "si_test: PASS\n");
    osSendMesg(&si_rom_ctl, (OSMesg)CTL_END, OS_MESG_NOBLOCK);
    for (;;) ;
}
