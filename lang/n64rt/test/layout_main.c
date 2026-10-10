/* layout_test's port: the runtime with the game of layout_game.c, as a port
 * of a real game has it -- the ROM is a file of its own (data slot 0, at
 * 1 MiB), and the libultra functions to replace are the game's stubs
 * (layout_start.S; their addresses from the game's symbols,
 * build/layout/layout_syms.h). */
#include <stdio.h>
#include <generated/csr.h>
#include "n64rt.h"
#include "layout_game.h"
#include "layout_syms.h"

static const n64rt_patch_t s_patches[] = {
#define X(n) { N64RT_##n, GAME_##n },
    LAYOUT_STUBS(X)
#undef X
};

int main(void)
{
    printf("\nlayout_test\n");
    static n64rt_game_t g;
    g.name = "layout_test";
    g.boot_rom = 0x1000u;
    g.boot_vram = GAME__start;
    g.boot_len = 0x100000u;
    g.entry = GAME__start;
    g.mem_size = 0x800000u;
    g.cic = 6102;
    g.patches = s_patches;
    g.npatches = sizeof s_patches / sizeof s_patches[0];
    apf_bridge_slot_id_write(0);                    /* the ROM's size: slot 0's in the core's table */
    for (volatile int i = 0; i < 256; i++) { }
    n64rt_boot_rom(&g, apf_bridge_file_size_read());
}

/* The game's report: its address is the message it sends when it is done
 * (the runtime's trace of the queues, a debugging build's). Printed, and
 * the run ended. */
void n64rt_trace_mq(int op, OSMesgQueue *mq, OSMesg msg, u32 ra)
{
    (void)mq; (void)ra;
    u32 a = (u32)(uintptr_t)msg;
    if (op != 'S' || (a >> 23) != 0x100u || (a & 3u)) return;
    const layout_report_t *r = (const layout_report_t *)(uintptr_t)a;
    if (r->magic != LAYOUT_MAGIC) return;
    n64rt_log("layout_test: run %u: %u ROM words read, EEPROM sum %u, "
              "Controller Pak file %u, %u bytes free\n", r->gen, r->rom_words, r->eep_sum,
              r->cpak_gen, r->cpak_free);
    if (r->fails) n64rt_log("layout_test: %u checks failed, the first: %02x got %08x want %08x\n",
                            r->fails, r->first[0], r->first[1], r->first[2]);
    n64rt_log(r->fails ? "layout_test: FAILED\n" : "layout_test: PASS\n");
    while (uart_txfull_read()) { }
    uart_rxtx_write(4);
}
