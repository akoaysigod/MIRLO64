#include "audio_load.h"

int audio_load(const uint32_t *imem, uint32_t imem_words,
               const uint32_t *dmem, uint32_t dmem_words)
{
    audio_stop();
    for (uint32_t i = 0; i < imem_words; i++) audio_w32(AUDIO_IMEM_BASE + 4u * i, imem[i]);
    for (uint32_t i = 0; i < dmem_words; i++) audio_w32(AUDIO_DMEM_BASE + 4u * i, dmem[i]);
    for (int i = 0; i < 8; i++) audio_set_mbox(i, 0);
    int bad = 0;
    for (uint32_t i = 0; i < imem_words; i++) bad += audio_r32(AUDIO_IMEM_BASE + 4u * i) != imem[i];
    for (uint32_t i = 0; i < dmem_words; i++) bad += audio_r32(AUDIO_DMEM_BASE + 4u * i) != dmem[i];
    return bad;
}

int audio_load_imem2(const uint32_t *imem2, uint32_t imem2_words)
{
    int bad = 0;
    for (uint32_t i = 0; i < imem2_words; i++) audio_w32(AUDIO_IMEM2_BASE + 4u * i, imem2[i]);
    for (uint32_t i = 0; i < imem2_words; i++) bad += audio_r32(AUDIO_IMEM2_BASE + 4u * i) != imem2[i];
    return bad;
}
