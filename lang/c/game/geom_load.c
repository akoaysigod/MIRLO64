#include "geom_load.h"
#include <generated/csr.h>

#define GEOM_HB_READY 0xB0000040u           /* lang/c/geom/main.c: waiting for a display list */

void geom_load(const uint32_t *rom_a, uint32_t rom_a_words,
               const uint32_t *rom_b, uint32_t rom_b_words)
{
    mailbox_geom_run_write(0);
    for (uint32_t i = 0; i < rom_a_words; i++) ((volatile uint32_t *)(uintptr_t)GEOM_ROM_A_BASE)[i] = rom_a[i];
    for (uint32_t i = 0; i < rom_b_words; i++) ((volatile uint32_t *)(uintptr_t)GEOM_ROM_B_BASE)[i] = rom_b[i];
}

uint32_t geom_start(void)
{
    mailbox_geom_msg_write(0);
    mailbox_geom_run_write(1);
    /* the firmware zeroes its .bss and starts in well under a millisecond */
    for (uint32_t n = 0; n < 20000000u; n++)
        if (mailbox_geom_msg_read() == GEOM_HB_READY) return 0;
    uint32_t hb = mailbox_geom_msg_read();
    return hb ? hb : 1u;
}
