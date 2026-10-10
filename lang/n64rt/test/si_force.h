/* (si_test picks the accessory itself, from its ROM: n64rt_pak.c's N64RT_PAK_FORCE) */
#ifndef __ASSEMBLER__
extern volatile unsigned *const si_rom_kind;
#define N64RT_PAK_FORCE (*si_rom_kind)
#endif
#ifdef SI_TEST_EEP16K
#define N64RT_EEP_FORCE 2u                         /* the 16 Kbit chip (si_rom.c's SI_EEP16K) */
#endif
