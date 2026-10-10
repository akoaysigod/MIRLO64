/* si_test's side of the runtime: its ROM (si_rom.c) as a game -- where it
 * boots and which of libultra's functions in it are replaced come from the
 * ROM's own symbols (si_spec.py) -- and what the ROM asks of this side
 * through two message queues it sends to: a line for the log, the core
 * starting again, the cartridge slot's port, the end. */
#include <stdio.h>
#include <generated/csr.h>
#include "n64rt.h"

extern const n64rt_patch_t si_patches[];
extern const unsigned si_npatches, si_entry, si_boot_len;
extern OSMesgQueue *const si_rom_out, *const si_rom_ctl;
extern volatile u32 *const si_rom_reply;

int main(void)
{
    printf("\nsi_test on Mirlo (n64rt)\n");
    static n64rt_game_t g;
    g.name = "si_test";
    g.boot_rom = 0x1000u;
    g.boot_vram = si_entry;
    g.boot_len = si_boot_len;
    g.entry = si_entry;
    g.mem_size = 0x400000u;
    g.cic = 6102;
    g.patches = si_patches;
    g.npatches = si_npatches;
    n64rt_boot_game_file(&g);
}

void n64rt_trace_mq(int op, OSMesgQueue *mq, OSMesg msg, u32 ra)
{
    (void)ra;
    if (op != 'S') return;
    if (mq == si_rom_out) n64rt_log("%s", (const char *)msg);
    else if (mq == si_rom_ctl) {
        switch ((u32)(uintptr_t)msg) {
        case 1: n64rt_pak_init(); break;
        case 2: *si_rom_reply = gb_cart_id_read() == 0x47420001u; break;
        case 3: n64rt_log("%c", 4); break;          /* (the simulation ends) */
        }
    }
}
