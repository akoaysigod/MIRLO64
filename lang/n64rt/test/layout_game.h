/* layout_test: what its game and the port's program share. */
#ifndef LAYOUT_GAME_H
#define LAYOUT_GAME_H

/* the libultra functions the game leaves to the runtime: a stub each in the
 * game (layout_start.S), patched as a real game's are */
#define LAYOUT_STUBS(X) \
    X(osCreateThread) X(osStartThread) X(osCreateMesgQueue) X(osSendMesg) X(osRecvMesg) X(osSetTimer) X(osGetCount) \
    X(osPiRawStartDma) X(osPiRawReadIo) X(osEepromProbe) X(osEepromLongRead) X(osEepromLongWrite) \
    X(__osContRamRead) X(__osContRamWrite) X(__osPfsGetStatus)

#define LAYOUT_SETTING  0x80000320u     /* the "Controller 1 pak" setting, which the game picks: n64rt_pak.c's N64RT_PAK_FORCE */
#define LAYOUT_MAGIC    0x4C415954u     /* "LAYT" */
#define LAYOUT_ROM_FROM 0x00020000u     /* the ROM's word at an offset from here on: LAYOUT_ROM_WORD */
#define LAYOUT_ROM_WORD(o) (((o) * 2654435761u) ^ 0x5EED5EEDu)

#ifndef __ASSEMBLER__
/* What the game found, big-endian words: sent to the port's program when the
 * game is done (a message whose value is its address), and the first 64
 * bytes of what the game leaves in its EEPROM. */
typedef struct {
    unsigned magic;
    unsigned gen;               /* 1, or one more than the report found in the EEPROM */
    unsigned fails;             /* the checks that failed */
    unsigned first[3];          /* the first of them: its number, what was got, what was wanted */
    unsigned rom_words;         /* the ROM's words read and compared with LAYOUT_ROM_WORD */
    unsigned eep_sum;           /* the EEPROM as found: the sum of its 512 bytes */
    unsigned cpak_gen;          /* the Controller Pak's file as found: its generation, 0 with none */
    unsigned cpak_free;         /* its free bytes after the file is written */
    unsigned pad[6];
} layout_report_t;
#endif

#endif
