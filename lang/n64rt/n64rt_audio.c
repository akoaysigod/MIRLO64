/* n64rt: the RSP's audio task and the AI, on Mirlo's audio core.
 *
 * The core runs MIRLO's ABI firmware (lang/c/audio, PROG=abi_main): the
 * N64's audio microcodes' command lists (ABI 1, naudio, EAD's), reading and
 * writing the game's RDRAM in the N64's order, and the AI's two-buffer queue,
 * through its mailbox (audio_hw.h AMB_*). A task names its ABI; which one is
 * told from the microcode's data, as abi.c's abi_identify() does. */
#include <stdio.h>
#include <generated/csr.h>
#include "n64rt.h"
#include "audio_load.h"
#include "abi.h"
#include AUDIO_FW_HEADER

static int s_ok;

void n64rt_audio_init(void)
{
    int bad = audio_load(audio_imem_words, AUDIO_IMEM_NWORDS, audio_dmem_words, AUDIO_DMEM_NWORDS);
    bad += audio_load_imem2(audio_imem2_words, AUDIO_IMEM2_NWORDS);
    audio_set_mbox(AMB_AI_FREQ, 32000);
    audio_start();
    for (u32 n = 0; audio_mbox(AMB_STATE) != AUDIO_ST_ABI && n < 10000000u; n++) ;
    s_ok = audio_mbox(AMB_STATE) == AUDIO_ST_ABI;
    n64rt_log("n64rt: audio core firmware %u+%u+%u words (%d bad), %s\n", AUDIO_IMEM_NWORDS, AUDIO_IMEM2_NWORDS,
              AUDIO_DMEM_NWORDS, bad, s_ok ? "running" : "NOT running -- silent");
}

/* the microcode's ABI, from its data (abi.c abi_identify) */
static int abi_kind(const u32 *d)
{
    if (d[0] == 1u && d[0x30 / 4] == 0xf0000f00u) return d[0x28 / 4] == 0x1e24138cu ? ABI_KIND_ABI1 : -1;
    switch (d[0x10 / 4]) {
    case 0x0000127cu: return ABI_KIND_NAUDIO;
    case 0x11181350u: return ABI_KIND_NEAD_MK;
    case 0x110412ccu: return ABI_KIND_NEAD_SF;
    case 0x1f681230u: case 0x1f801250u: return ABI_KIND_NEAD_OOT;
    default: return -1;
    }
}

/* A task is handed over and runs on its own; its SP event comes when the
 * core is done (n64rt_audio_poll, from the timer's tick). */
static int s_busy;
u32 n64rt_audio_poll(void)
{
    if (s_busy && audio_mbox(AMB_TASK) == 0 && audio_mbox(AMB_STATE) == AUDIO_ST_ABI) {
        s_busy = 0;
        return 1u << OS_EVENT_SP;
    }
    return 0;
}
int n64rt_audio_busy(void) { return s_busy; }

/* 1: handed to the core (its SP event comes later), 0: done already */
int n64rt_audio_task(OSTask *t)
{
    static u32 s_ud = ~0u;
    static int s_kind = -1;
    if (!s_ok) return 0;
    u32 ud = N64RT_PHYS(t->t.ucode_data);
    if (ud != s_ud) {
        s_ud = ud;
        s_kind = abi_kind((const u32 *)(uintptr_t)N64RT_K0(ud));
        n64rt_log("n64rt: audio microcode: ABI kind %d\n", s_kind);
    }
    if (s_kind < 0) return 0;                            /* one it does not speak: silence */
    for (u32 n = 0; audio_mbox(AMB_TASK) != 0 && n < 20000000u; n++) ;
    audio_set_mbox(AMB_TASK_N, (t->t.data_size / 8u) | (u32)(s_kind + 1) << 24);
    audio_set_mbox(AMB_TASK, N64RT_PHYS(t->t.data_ptr));
    s_busy = 1;
    return 1;
}

/* ---- the AI ------------------------------------------------------------------- */
#define VI_NTSC_CLOCK 48681812u
s32 n64rt_osAiSetFrequency(u32 f)
{
    if (!f) return -1;
    u32 dac = (VI_NTSC_CLOCK + f / 2u) / f;              /* libultra: the DAC rate, rounded */
    if (dac < 132u) return -1;
    u32 real = VI_NTSC_CLOCK / dac;
    if (s_ok) audio_set_mbox(AMB_AI_FREQ, real);
    return (s32)real;
}
s32 n64rt_osAiSetNextBuffer(void *buf, u32 size)
{
    if (!s_ok) return 0;
    if (audio_mbox(AMB_AI_NEXT) != 0) return -1;         /* both slots taken */
    audio_set_mbox(AMB_AI_LEN, size);
    audio_set_mbox(AMB_AI_NEXT, N64RT_PHYS(buf));
    return 0;
}
u32 n64rt_osAiGetLength(void) { return s_ok ? audio_mbox(AMB_AI_REMAIN) : 0; }
u32 n64rt_osAiGetStatus(void)
{
    if (!s_ok) return 0;
    return (audio_mbox(AMB_AI_NEXT) ? 0x80000000u : 0u) | (audio_mbox(AMB_AI_REMAIN) ? 0x40000000u : 0u);
}

/* (the AI in hardware: only a core with the RSP has it -- n64rt_rsp.c) */
void n64rt_ai_hw_event(void) { }
void n64rt_ai_hw_reset(void) { }
