# si_test's ROM: where it starts, and the functions of libultra's that the
# runtime replaces in any game -- threads, messages, timers, the caches. Each
# is a place for the runtime's jump (three instructions), found in the ROM by
# its name (si_spec.py), as a game's by its symbol map or its signature.
    .set    noreorder
    .section .text.start, "ax"
    .globl  _start
_start:
    lui     $sp, %hi(si_rom_stack_top)
    addiu   $sp, $sp, %lo(si_rom_stack_top)
    jal     si_rom_main
    addiu   $sp, $sp, -16
1:  b       1b
    nop

    .macro  replaced name
    .globl  \name
    .type   \name, @function
\name:
    break
    break
    break
    nop
    .size   \name, . - \name
    .endm
    .text
    replaced osCreateMesgQueue
    replaced osSendMesg
    replaced osRecvMesg
    replaced osSetEventMesg
    replaced osSetTimer
    replaced osGetTime
    replaced osGetCount
    replaced osCreateThread
    replaced osStartThread
    replaced osVirtualToPhysical
    replaced osWritebackDCache
    replaced osInvalDCache

# void si_rom_sled(void): 32 KiB of code run through once -- nothing of what
# ran before it is left in the instruction cache
    .globl  si_rom_sled
    .type   si_rom_sled, @function
si_rom_sled:
    .rept   8192
    nop
    .endr
    jr      $ra
    nop
    .size   si_rom_sled, . - si_rom_sled

    .bss
    .balign 16
    .space  16384
    .globl  si_rom_stack_top
si_rom_stack_top:
    .space  16
