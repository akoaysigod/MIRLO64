/* pak_test's side of the runtime: what n64rt_io.c gives libultra, on
 * n64rt_pak.c alone (no threads, no PIF). */
#include <stdarg.h>
#include <stdio.h>
#include <generated/csr.h>
#include "n64rt.h"

unsigned pak_test_kind;
static n64rt_game_t s_game = { .mem_size = 0x800000u };
const n64rt_game_t *n64rt_game = &s_game;

void n64rt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

s32 __osContRamRead(void *mq, int ch, u16 addr, u8 *buf) { (void)mq; return n64rt_pak_rw(ch, (u32)addr << 5, buf, 0); }
s32 __osContRamWrite(void *mq, int ch, u16 addr, u8 *buf, int force)
{
    (void)mq;
    if (force != 1 && addr < 7u && addr != 0u) return 0;
    return n64rt_pak_rw(ch, (u32)addr << 5, buf, 1);
}
s32 __osPfsGetStatus(void *mq, int ch) { (void)mq; return n64rt_pak_kind(ch) != N64RT_PAK_NONE ? 0 : 1; }
void __osSiGetAccess(void) { }
void __osSiRelAccess(void) { }
int pak_test_slot_port(void) { return gb_cart_id_read() == 0x47420001u; }
/* (osGbpakInit and osGbpakPower wait on a timer: no wait here) */
void osCreateMesgQueue(void *mq, void *msg, s32 n) { (void)mq; (void)msg; (void)n; }
s32 osSetTimer(void *t, u64 count, u64 interval, void *mq, void *msg) { (void)t; (void)count; (void)interval; (void)mq; (void)msg; return 0; }
s32 osRecvMesg(void *mq, void *msg, s32 flag) { (void)mq; (void)msg; (void)flag; return 0; }

u32 osGetCount(void) { return (u32)timer0_uptime_cycles_read(); }      /* (a repaired pak's new number) */

/* the core starting again: what is in the Controller Pak's memory stays */
void pak_test_boot(void) { n64rt_pak_init(); }
