/* rcp_bench: a game that does what the monitor emulates of the RCP's registers (n64rt_os.c emulate_hw_at, n64rt_io.c rcp_read_ /
 * rcp_write_) and checks the answers against the console's (n64-systemtest's spmem, pifram and cart_memory groups; the
 * hardware's rules in lang/n64rt/n64rt_os.c above emulate_hw_at). A program linked for 0x8000_0400 that runs as a game under the
 * loader (OWN) on the SoC in sim/mips_soc; the report is in the RDRAM at 0x8030_0000 (DUMP=0x43B00000,64): word 0 the magic
 * 0x600D600D (the checks passed) or 0xBADBAD00 + the number of failed checks, words 2-4 the first failed check (which, got, want),
 * word 5 the failed checks of the CPU's writes to the DPC, counted apart (only a core with the CPU's DPC write path passes them),
 * word 10 0xD0DE when it is done. A runtime that does not emulate the rest never gets there: its monitor stops at the first
 * sub-word store to DMEM.
 *
 *   make -f rcp_bench.mk; the run is in rcp_bench.mk */
typedef unsigned int u32;
#define K1(pa) ((volatile u32 *)(0xA0000000u | (pa)))
#define REPORT ((volatile u32 *)0x80300000u)
#define MFC0(r) ({ u32 v_; __asm__ volatile("mfc0 %0, $" #r : "=r"(v_)); v_; })
#define DMEM    ((volatile u32 *)0xA4000000u)
#define DMEM8   ((volatile unsigned char *)0xA4000000u)
#define SP_MEM_ADDR   K1(0x04040000u)
#define SP_DRAM_ADDR  K1(0x04040004u)
#define SP_RD_LEN     K1(0x04040008u)
#define SP_WR_LEN     K1(0x0404000Cu)
#define SP_STATUS     K1(0x04040010u)
#define SP_DMA_BUSY   K1(0x04040018u)
#define DPC_START     K1(0x04100000u)
#define DPC_END       K1(0x04100004u)
#define DPC_STATUS    K1(0x0410000Cu)
#define DPC_CLOCK     K1(0x04100010u)
#define PI_DRAM_ADDR  K1(0x04600000u)
#define PI_CART_ADDR  K1(0x04600004u)
#define PI_RD_LEN     K1(0x04600008u)
#define PI_WR_LEN     K1(0x0460000Cu)
#define PI_STATUS     K1(0x04600010u)

static u32 s_fails;
static void check(u32 what, u32 got, u32 want)
{
    if (got == want) return;
    if (!s_fails) { REPORT[2] = what; REPORT[3] = got; REPORT[4] = want; }
    s_fails++;
}
static void spwait(void) { while (*SP_DMA_BUSY) ; }

void game_main(void)
{
    /* ---- DMEM: words, bytes and halves ---- */
    DMEM[0] = 0x01234567u; DMEM[1] = 0x89ABCDEFu;
    check(1, DMEM[0], 0x01234567u);
    check(2, DMEM8[1], 0x23u);
    check(3, *(volatile unsigned short *)(0xA4000004u + 2), 0xCDEFu);
    check(4, (u32)*(volatile signed char *)(0xA4000004u), 0xFFFFFF89u);            /* lb sign-extends */
    /* sh: the whole word, the register in its lane, zeros under it; sb the same */
    DMEM[0] = 0xDEADBEEFu; DMEM[1] = 0xBADDECAFu;
    { u32 v = 0x12345678u; __asm__ volatile("sh %0, 0(%1)\n\tsh %0, 6(%1)" :: "r"(v), "r"(DMEM) : "memory"); }
    check(5, DMEM[0], 0x56780000u); check(6, DMEM[1], 0x12345678u);
    DMEM[0] = DMEM[1] = DMEM[2] = DMEM[3] = 0;
    { u32 v = 0x12345678u; __asm__ volatile("sb %0, 0(%1)\n\tsb %0, 5(%1)\n\tsb %0, 10(%1)\n\tsb %0, 15(%1)" :: "r"(v), "r"(DMEM) : "memory"); }
    check(7, DMEM[0], 0x78000000u); check(8, DMEM[1], 0x56780000u); check(9, DMEM[2], 0x34567800u); check(10, DMEM[3], 0x12345678u);
    /* sd: only the upper 32 bits, at the address */
    DMEM[0] = 0xDEADBEEFu; DMEM[1] = 0xBADDECAFu; DMEM[2] = 0xABABABABu;
    __asm__ volatile(".set push\n.set mips3\n.set noat\n\tlui $8, 0xABCD\n\tori $8, $8, 0xEF98\n\tdsll32 $8, $8, 0\n\tlui $9, 0x7654\n\tori $9, $9, 0x3210\n"
                     "\tor $8, $8, $9\n\tsd $8, 0(%0)\n.set pop" :: "r"(DMEM) : "$8", "$9", "memory");     /* the register is 0xABCDEF98_76543210 */
    check(11, DMEM[0], 0xABCDEF98u); check(12, DMEM[1], 0xBADDECAFu);
    /* ld: two words, the upper first */
    DMEM[0] = 0x11223344u; DMEM[1] = 0x55667788u;
    { u32 hi, lo; __asm__ volatile(".set push\n.set mips3\n\tld $8, 0(%2)\n\tdsrl32 %0, $8, 0\n\tsll %1, $8, 0\n.set pop" : "=r"(hi), "=r"(lo) : "r"(DMEM) : "$8");
      check(13, hi, 0x11223344u); check(14, lo, 0x55667788u); }
    /* lwl / lwr: an unaligned word at +1 */
    DMEM[0] = 0x01234567u; DMEM[1] = 0x89ABCDEFu;
    { u32 v = 0; __asm__ volatile("lwl %0, 1(%1)\n\tlwr %0, 4(%1)" : "+r"(v) : "r"(DMEM) : "memory"); check(15, v, 0x23456789u); }
    { u32 v = 0xFFFFFFFFu; __asm__ volatile("lwr %0, 1(%1)" : "+r"(v) : "r"(DMEM) : "memory"); check(16, v, 0xFFFF0123u); }
    { u32 v = 0xFFFFFFFFu; __asm__ volatile("lwl %0, 2(%1)" : "+r"(v) : "r"(DMEM) : "memory"); check(17, v, 0x4567FFFFu); }
    /* swl / swr must run (the hardware's result is not measured): and not touch the next word */
    DMEM[0] = 0; DMEM[1] = 0x5A5A5A5Au; DMEM[2] = 0xA5A5A5A5u;
    { u32 v = 0x12345678u; __asm__ volatile("swl %0, 0(%1)" :: "r"(v), "r"(DMEM) : "memory"); }
    check(18, DMEM[0], 0x12345678u); check(19, DMEM[1], 0x5A5A5A5Au);
    /* lwc1 / swc1 */
    DMEM[0] = 0x3F800000u;                                      /* 1.0f */
    { float f; __asm__ volatile("lwc1 %0, 0(%1)" : "=f"(f) : "r"(DMEM) : "memory"); f = f + f; __asm__ volatile("swc1 %0, 4(%1)" :: "f"(f), "r"(DMEM) : "memory"); }
    check(20, DMEM[1], 0x40000000u);                           /* 2.0f */
    /* DMEM repeats every 8 KiB up to 0x0404_0000 */
    DMEM[0] = 0x01234567u; *(volatile u32 *)(0xA4000000u + 0x3E000u) = 0x76543210u;
    check(21, DMEM[0], 0x76543210u); check(22, *(volatile u32 *)(0xA4000000u + 0x2000u), 0x76543210u);
    check(23, *(volatile u32 *)(0xA4000000u + 0x1000u + 0x2000u * 5), DMEM[0x1000 / 4]);

    /* ---- SP registers: a read of DMEM from the CPU leaves the game's SP_MEM_ADDR and SP_DRAM_ADDR alone ---- */
    *SP_MEM_ADDR = 0x40u; *SP_DRAM_ADDR = 0x1000u;
    (void)DMEM[0x100 / 4];
    check(24, *SP_MEM_ADDR, 0x40u); check(25, *SP_DRAM_ADDR, 0x1000u);
    /* ... and a DMA of the game's still under way is not corrupted by the registers' next writes */
    {   volatile u32 *src = (volatile u32 *)0xA0100000u, *src2 = (volatile u32 *)0xA0110000u;
        for (u32 i = 0; i < 1024; i++) { src[i] = 0xA0000000u + i; src2[i] = 0xB0000000u + i; }
        *SP_MEM_ADDR = 0; *SP_DRAM_ADDR = 0x00100000u; *SP_RD_LEN = 0xFFFu;                  /* 4 KiB RDRAM -> DMEM */
        *SP_MEM_ADDR = 0x800u; *SP_DRAM_ADDR = 0x00110000u; *SP_RD_LEN = 0x7Fu;              /* 128 bytes -> DMEM + 0x800 (the first is still running) */
        spwait();
        check(26, DMEM[0], 0xA0000000u); check(27, DMEM[0x7FC / 4], 0xA00001FFu); check(28, DMEM[0x800 / 4], 0xB0000000u);
        check(29, DMEM[0x87C / 4], 0xB000001Fu); check(30, DMEM[0x880 / 4], 0xA0000220u); }

    /* ---- DPC_CLOCK ticks (a CIC-6102 game) ---- */
    { u32 a = *DPC_CLOCK; u32 b = *DPC_CLOCK; check(31, b > a, 1); check(32, b - a < 100000u, 1); }

    /* ---- PI: masking, the advance after a DMA, the length registers ---- */
    *PI_CART_ADDR = 0xFFFFFFFFu; check(33, *PI_CART_ADDR, 0xFFFFFFFEu);
    *PI_DRAM_ADDR = 0xFFFFFFFFu; check(34, *PI_DRAM_ADDR, 0x00FFFFFEu);
    check(35, *PI_RD_LEN, 0x7Fu);
    {   volatile u32 *dst = (volatile u32 *)0xA0120000u;
        for (u32 i = 0; i < 8; i++) dst[i] = 0xAAAAAAAAu;
        *PI_CART_ADDR = 0x10001000u; *PI_DRAM_ADDR = 0x00120000u; *PI_WR_LEN = 15u;       /* 16 bytes of the ROM */
        while (*PI_STATUS & 1u) ;
        check(36, *PI_CART_ADDR, 0x10001010u); check(37, *PI_DRAM_ADDR, 0x00120010u); check(38, *PI_WR_LEN, 0x7Fu);
        *PI_CART_ADDR = 0x10001000u; *PI_DRAM_ADDR = 0x00120000u; *PI_WR_LEN = 4u;        /* 5 bytes: the cart side moves 6, the RDRAM side 8 */
        while (*PI_STATUS & 1u) ;
        check(39, *PI_CART_ADDR, 0x10001006u); check(40, *PI_DRAM_ADDR, 0x00120008u); }

    /* ---- the CPU's writes to the DPC (a core with the CPU's DPC write path: rcp_dpc_view.sv; word 5 counts these failures apart: a core
     * without it drops the writes) ---- */
    {   u32 base = s_fails;
        *DPC_STATUS = 0x2u;  check(41, *DPC_STATUS & 1u, 1u);                    /* set XBUS */
        *DPC_STATUS = 0x1u;  check(42, *DPC_STATUS & 1u, 0u);                    /* clear XBUS */
        *DPC_STATUS = 0x8u;  check(43, (*DPC_STATUS >> 1) & 1u, 1u);             /* set FREEZE (so that the RDP stays out of it) */
        *DPC_START = 0x00001000u; check(44, *DPC_START, 0x00001000u);
        *DPC_START = 0x00002000u; check(45, *DPC_START, 0x00001000u);            /* ignored: a start is pending */
        *DPC_END = 0x00001000u;  check(46, *DPC_END, 0x00001000u);
        *DPC_STATUS = 0x4u;  check(47, (*DPC_STATUS >> 1) & 1u, 0u);             /* clear FREEZE */
        REPORT[5] = s_fails - base; s_fails = base;
    }
    REPORT[1] = 0;
    REPORT[0] = s_fails ? 0xBADBAD00u + s_fails : 0x600D600Du;
    REPORT[10] = 0xD0DEu;
    for (;;) ;
}

__attribute__((section(".text.start"))) void _start(void)
{
    __asm__ volatile(".set noreorder\n\tj game_main\n\tnop");
}
