/* layout_test's game: N64 code, run by the runtime as any game's -- a ROM of
 * 64 MiB (make_layout_rom.py: this program, then words that say where they
 * are), all 8 MiB of RDRAM, an EEPROM and a Controller Pak. It reads its ROM
 * through libultra's PI calls, where the SDRAM's part ends and the PSRAM's
 * begins and at its very end, and uses the saves through libultra's own
 * osPfs* (ULTRALIB's sources, as pak_test). What it finds goes into a report, which it leaves
 * in the EEPROM and hands to the port's program.
 *
 * It runs twice, the second time on what the Pocket would have kept of the
 * first (the three saves read back through the bridge, loaded again): the
 * report in the EEPROM is then the first run's, and the cartridge's RAM and
 * the Controller Pak's file must hold what that run wrote. */
#include "PR/os_internal.h"
#include "controller.h"
#include "PR/os_gbpak.h"
#include "layout_game.h"

s32 osPiRawStartDma(s32 dir, u32 dev, void *dram, u32 size);
s32 osPiRawReadIo(u32 dev, u32 *data);

#define SETTING (*(volatile u32 *)LAYOUT_SETTING)
enum { NONE = 0, CPAK = 1 };                /* the setting's values */

static layout_report_t s_rep;
static void check(u32 which, u32 got, u32 want)
{
    if (got == want) return;
    if (s_rep.fails++ == 0) { s_rep.first[0] = which; s_rep.first[1] = got; s_rep.first[2] = want; }
}

static OSMesgQueue s_mq;
static OSMesg s_msg;
static OSPfs s_pfs;
static u8 s_buf[0x1000] __attribute__((aligned(16)));
static u8 s_eep[512];

/* ---- the ROM ---------------------------------------------------------------------- */
static u8 rom_byte(u32 off) { return (u8)(LAYOUT_ROM_WORD(off & ~3u) >> (24u - 8u * (off & 3u))); }
static void rom_dma(u32 which, u32 off, u32 len)
{
    for (u32 i = 0; i < len; i++) s_buf[i] = 0;
    check(which, (u32)osPiRawStartDma(OS_READ, off, s_buf, len), 0);
    for (u32 i = 0; i < len; i++) if (s_buf[i] != rom_byte(off + i)) { check(which | 0x100u, off + i, s_buf[i]); break; }
    s_rep.rom_words += len / 4u;
}
static void rom(void)
{
    /* (offsets in the ROM: the Pocket loads it at 1 MiB, so the SDRAM's part
     * ends at 0x0300_0000; 0x0370_0000 and 0x03F0_0000 are the bridge's 56
     * and 64 MiB, where its map changes) */
    static const u32 at[8] = { 0x00020000u, 0x01234000u, 0x02FFF800u, 0x03000000u, 0x036FF800u, 0x03EFF800u, 0x03FFE000u, 0x03FFF000u };
    for (u32 i = 0; i < 8u; i++) rom_dma(0x10u + i, at[i], 0x1000u);
    rom_dma(0x18u, 0x02FFFFFDu, 0x107u);                     /* bytes, from the SDRAM's last on */
    /* a word a megabyte, and those either side of the PSRAM's start and the last */
    for (u32 off = LAYOUT_ROM_FROM + 0x345u * 4u; off < 0x04000000u; off += 0x00100000u) {
        u32 w = 0;
        check(0x20u, (u32)osPiRawReadIo(off, &w), 0);
        check(0x21u, w, LAYOUT_ROM_WORD(off));
        s_rep.rom_words++;
    }
    static const u32 one[4] = { 0x02FFFFFCu, 0x03000000u, 0x03000004u, 0x03FFFFFCu };
    for (u32 i = 0; i < 4u; i++) {
        u32 w = 0;
        check(0x22u, (u32)osPiRawReadIo(one[i], &w), 0);
        check(0x23u + i, w, LAYOUT_ROM_WORD(one[i]));
        s_rep.rom_words++;
    }
}

/* ---- the saves ---------------------------------------------------------------------- */
static u8 cpak_byte(u32 gen, u32 i)    { return (u8)(gen * 17u + i * 3u); }

static void eeprom_in(void)
{
    check(0x30u, (u32)osEepromProbe(&s_mq), 1);
    check(0x31u, (u32)osEepromLongRead(&s_mq, 0, s_eep, 512), 0);
    for (u32 i = 0; i < 512u; i++) s_rep.eep_sum += s_eep[i];
    const layout_report_t *was = (const layout_report_t *)s_eep;
    s_rep.gen = was->magic == LAYOUT_MAGIC ? was->gen + 1u : 1u;
    if (s_rep.gen > 1u) {
        check(0x32u, was->fails, 0);
        for (u32 i = 64; i < 512u; i++) if (s_eep[i] != (u8)(i * 5u + was->gen)) { check(0x33u, i, s_eep[i]); break; }
    }
}
static void eeprom_out(void)
{
    for (u32 i = 0; i < 64u; i++) s_eep[i] = ((const u8 *)&s_rep)[i];
    for (u32 i = 64; i < 512u; i++) s_eep[i] = (u8)(i * 5u + s_rep.gen);
    check(0x38u, (u32)osEepromLongWrite(&s_mq, 0, s_eep, 512), 0);
    for (u32 i = 0; i < 512u; i++) s_buf[i] = 0;
    check(0x39u, (u32)osEepromLongRead(&s_mq, 0, s_buf, 512), 0);
    for (u32 i = 64; i < 512u; i++) if (s_buf[i] != s_eep[i]) { check(0x3Au, i, s_buf[i]); break; }
}

static void controller_pak(void)
{
    static u8 name[16] = { 0x26, 0x1A, 0x2B, 0x22, 0x28 }, ext[4] = { 0x1A };
    s32 file = -1, bytes = 0;
    const u32 gen = s_rep.gen;

    SETTING = CPAK;
    check(0x60u, (u32)osPfsInitPak(&s_mq, &s_pfs, 0), 0);
    s32 r = osPfsFindFile(&s_pfs, 0x3031, 0x4D49524C, name, ext, &file);
    if (gen > 1u) {
        /* the file the run before left */
        check(0x61u, (u32)r, 0);
        check(0x62u, (u32)osPfsReadWriteFile(&s_pfs, file, PFS_READ, 0, 256, s_buf), 0);
        s_rep.cpak_gen = (u32)(u8)(s_buf[0] * 241u);         /* (17 * 241 = 1 mod 256) */
        for (u32 i = 0; i < 256u; i++) if (s_buf[i] != cpak_byte(gen - 1u, i)) { check(0x63u, i, s_buf[i]); break; }
    } else {
        check(0x64u, r != 0, 1);
        check(0x65u, (u32)osPfsAllocateFile(&s_pfs, 0x3031, 0x4D49524C, name, ext, 256, &file), 0);
    }
    for (u32 i = 0; i < 256u; i++) s_buf[i] = cpak_byte(gen, i);
    check(0x66u, (u32)osPfsReadWriteFile(&s_pfs, file, PFS_WRITE, 0, 256, s_buf), 0);
    check(0x67u, (u32)osPfsFreeBlocks(&s_pfs, &bytes), 0);
    s_rep.cpak_free = (u32)bytes;
    check(0x68u, (u32)bytes, 123u * 256u - 256u);
    /* it is not a Transfer Pak, and with the other setting it is not there */
    check(0x69u, (u32)osGbpakInit(&s_mq, &s_pfs, 0), (u32)PFS_ERR_DEVICE);
    SETTING = NONE;
    check(0x6Au, osPfsInitPak(&s_mq, &s_pfs, 0) != 0, 1);
}

/* ---- the threads -------------------------------------------------------------------- */
static OSThread s_idle, s_main;
static u64 s_idle_stack[128], s_main_stack[4096];

static void idle(void *arg) { (void)arg; for (;;) ; }
static void test(void *arg)
{
    (void)arg;
    osCreateMesgQueue(&s_mq, &s_msg, 1);
    s_rep.magic = LAYOUT_MAGIC;
    /* all 8 MiB of RDRAM are the game's: its last megabyte, cached and not */
    volatile u32 *hi = (volatile u32 *)0x80700000u, *hi1 = (volatile u32 *)0xA0700000u;
    for (u32 i = 0; i < 0x40000u; i += 0x1001u) hi[i] = LAYOUT_ROM_WORD(i);
    for (u32 i = 0; i < 0x40000u; i += 0x1001u) check(0x01u, hi[i], LAYOUT_ROM_WORD(i));
    hi1[0x3BFFFu] = 0xC0FFEE00u;
    check(0x02u, hi1[0x3BFFFu], 0xC0FFEE00u);
    rom();
    eeprom_in();
    controller_pak();
    eeprom_out();
    osSendMesg(&s_mq, (OSMesg)&s_rep, OS_MESG_NOBLOCK);     /* (the port's program prints it and ends the run) */
    for (;;) ;
}

void game_boot(void)
{
    osCreateThread(&s_idle, 1, idle, 0, s_idle_stack + 128, 0);
    osStartThread(&s_idle);
    osCreateThread(&s_main, 3, test, 0, s_main_stack + 4096, 10);
    osStartThread(&s_main);
}

/* (libultra's own, which the game keeps) */
void __osSiGetAccess(void) { }
void __osSiRelAccess(void) { }
void bcopy(const void *src, void *dst, int n)
{
    const u8 *s = src; u8 *d = dst;
    if (d < s) while (n-- > 0) *d++ = *s++;
    else while (n-- > 0) d[n] = s[n];
}
void bzero(void *p, int n) { u8 *b = p; while (n-- > 0) *b++ = 0; }
int bcmp(const void *a, const void *b, int n)
{
    const u8 *x = a, *y = b;
    while (n-- > 0) if (*x++ != *y++) return 1;
    return 0;
}
