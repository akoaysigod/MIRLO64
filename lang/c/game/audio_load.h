/* Game-CPU side of the audio core (rtl/audio/AudioCore.v): load its IMEM and
 * DMEM images and release it from reset. See lang/c/audio/audio_hw.h. */
#ifndef AUDIO_LOAD_H
#define AUDIO_LOAD_H

#include <stdint.h>
#include "../audio/audio_hw.h"

/* the audio core's addresses (audio_hw.h, its own view: 0x8000_0000) as the
 * game CPU sees them: 0x6000_0000 (0x8000_0000 is an N64 game's RDRAM there) */
#define AUDIO_GAME_VIEW(a) ((a) - 0x80000000u + 0x60000000u)
static inline void     audio_w32(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)AUDIO_GAME_VIEW(a) = v; }
static inline uint32_t audio_r32(uint32_t a)             { return *(volatile uint32_t *)(uintptr_t)AUDIO_GAME_VIEW(a); }
static inline uint32_t audio_mbox(int i)                 { return audio_r32(AUDIO_MBOX(i)); }
static inline void     audio_set_mbox(int i, uint32_t v) { audio_w32(AUDIO_MBOX(i), v); }

/* Holds the core in reset, writes both images, verifies them and clears the
 * mailbox. Does NOT start the core (set inputs in the mailbox first, then
 * audio_start()). Returns the number of words that read back wrong. */
int audio_load(const uint32_t *imem, uint32_t imem_words,
               const uint32_t *dmem, uint32_t dmem_words);
/* the second IMEM bank (lang/c/audio's .text2), after audio_load(); returns
 * the words that read back wrong */
int audio_load_imem2(const uint32_t *imem2, uint32_t imem2_words);

static inline void audio_start(void) { audio_w32(AUDIO_RUN, 1); }
static inline void audio_stop(void)  { audio_w32(AUDIO_RUN, 0); }

#endif
