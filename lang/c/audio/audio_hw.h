/* Audio core address map (rtl/audio/AudioCore.v), as the audio core sees it;
 * the game CPU reaches it at 0x6000_0000 instead (game/audio_load.h). Shared by lang/c/audio and the game side
 * (lang/c/game/audio_load.c). */
#ifndef AUDIO_HW_H
#define AUDIO_HW_H

#include <stdint.h>

#define AUDIO_IMEM_BASE   0x80000000u   /* 8 KiB code: core fetch, SoC r/w   */
#define AUDIO_IMEM_SIZE   0x2000u
#define AUDIO_DMEM_BASE   0x80002000u   /* 8 KiB data/stack: core + SoC r/w  */
#define AUDIO_DMEM_SIZE   0x2000u
#define AUDIO_CTRL_BASE   0x80004000u
#define AUDIO_IMEM2_BASE  0x80006000u   /* 8 KiB more code: the second bank (.text2) */

#define AUDIO_RUN         (AUDIO_CTRL_BASE + 0x00u)  /* bit0: 1 = out of reset (SoC writes) */
#define AUDIO_CYCLES      (AUDIO_CTRL_BASE + 0x04u)  /* free-running sys-clock counter      */
#define AUDIO_MBOX(i)     (AUDIO_CTRL_BASE + 0x08u + 4u * (uint32_t)(i))   /* i = 0..7  */

/* MBOX0 is the core's state word; the trap handler (start.S) writes
 * AUDIO_ST_TRAP there and mcause/mepc/mtval to MBOX5..7. */
#define AUDIO_MBOX_STATE  0
#define AUDIO_ST_TRAP     0xA0D1DEADu


/* ---- ABI firmware (lang/c/audio/abi_main.c) mailbox protocol ----------------
 * The N64's RSP audio task + AI, over the eight mailbox words:
 *
 *   MBOX0 STATE     core: AUDIO_ST_ABI when idle, AUDIO_ST_ABI_BUSY while a
 *                   task runs (AUDIO_ST_TRAP on a trap)
 *   MBOX1 TASK      game: command-list address; non-zero = a task is queued.
 *                   core: clears it to 0 when it TAKES the task (starts it),
 *                   so the game can queue the next one while this one runs:
 *                   a queue of two. A task is done when MBOX1 is 0 and MBOX0
 *                   is AUDIO_ST_ABI -- or when the next one has been taken.
 *   MBOX2 TASK_N    game: command count, written BEFORE MBOX1; its top byte
 *                   is an N64 game's ABI (abi.h ABI_KIND_*) + 1, 0 = as before
 *   MBOX3 AI_NEXT   game: PCM buffer address queued as the AI's "next"
 *                   buffer (osAiSetNextBuffer); non-zero = slot taken.
 *                   core: clears it when that buffer becomes the current one.
 *   MBOX4 AI_LEN    game: its length in bytes, written BEFORE MBOX3
 *   MBOX5 AI_REMAIN core: bytes not yet played of what the AI was handed
 *                   (current buffer + what sits in the APF FIFO), at the
 *                   AI frequency -- what osAiGetLength() returns
 *   MBOX6 AI_FREQ   game: DAC rate in Hz (osAiSetFrequency); 0 = 32000
 *   MBOX7 STATS     core: cycles the last task took
 * The same two-slot AI model as the N64 (current + next): osAiSetNextBuffer
 * fails while MBOX3 is non-zero. */
#define AUDIO_ST_ABI      0xA0D1AB10u
#define AUDIO_ST_ABI_BUSY 0xA0D1AB11u
#define AMB_STATE     0
#define AMB_TASK      1
#define AMB_TASK_N    2
#define AMB_AI_NEXT   3
#define AMB_AI_LEN    4
#define AMB_AI_REMAIN 5
#define AMB_AI_FREQ   6
#define AMB_STATS     7

#endif
