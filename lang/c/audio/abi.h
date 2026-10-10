/* N64 audio microcode (ABI, the pre-EU "old" command set SM64 US emits),
 * interpreted in integer C. Runs on the audio core; the same file builds on
 * the host for tests (lang/c/audio/test). */
#ifndef AUDIO_ABI_H
#define AUDIO_ABI_H

#include <stdint.h>

/* Opcodes and flags: include/PR/abi.h, non-SH/CN block. */
#define ABI_SPNOOP     0
#define ABI_ADPCM      1
#define ABI_CLEARBUFF  2
#define ABI_ENVMIXER   3
#define ABI_LOADBUFF   4
#define ABI_RESAMPLE   5
#define ABI_SAVEBUFF   6
#define ABI_SEGMENT    7
#define ABI_SETBUFF    8
#define ABI_SETVOL     9
#define ABI_DMEMMOVE   10
#define ABI_LOADADPCM  11
#define ABI_MIXER      12
#define ABI_INTERLEAVE 13
#define ABI_POLEF      14
#define ABI_SETLOOP    15

#define ABI_F_INIT 0x01
#define ABI_F_LOOP 0x02
#define ABI_F_LEFT 0x02
#define ABI_F_VOL  0x04
#define ABI_F_AUX  0x08

/* Execute one command. w1 carries a DRAM pointer for the commands that take
 * one (hence uintptr_t: 64-bit on the host test build). */
void abi_exec(uint32_t w0, uintptr_t w1);

/* Mirlo-N64: code for the audio core's second IMEM bank (audio.ld .text2,
 * 0x8000_6000, as fast as the first): naudio's, main's loop, what runs once */
#define ABI_TEXT2 __attribute__((section(".text2"), noinline))

/* Which microcode's command set: ABI1 (SM64's, the default), naudio
 * (n_aspMain) or EAD's (three games' variants). abi_identify() tells them apart from the microcode's data
 * segment (words 0x00, 0x10, 0x28, 0x30), as emulators do: -1 unknown. */
#define ABI_KIND_ABI1   0
#define ABI_KIND_NAUDIO 1
#define ABI_KIND_NEAD_MK  2     /* EAD's aspMain, per game (abi.c ne_ops) */
#define ABI_KIND_NEAD_SF  3
#define ABI_KIND_NEAD_OOT 4
void abi_select(int kind);
int  abi_identify(uint32_t d00, uint32_t d10, uint32_t d28, uint32_t d30);

/* Reset all interpreter state (between sessions/tests). */
void abi_reset(void);
/* Writes the per-note states held in DMEM back to DRAM (ABI_STATE_CACHE). */
void abi_state_flush(void);

/* Count of commands by opcode since the last abi_reset (diagnostics). */
extern uint32_t abi_op_count[16];

#endif
