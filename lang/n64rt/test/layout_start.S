# layout_test's game: where IPL3 jumps (the runtime's first thread: sp and ra
# are its), and the libultra functions it leaves to the runtime -- four
# instructions each, of which the runtime replaces three.
#include "layout_game.h"
    .set    noreorder
    .section .text.start, "ax"
    .globl  _start
_start:
    la      $t0, __bss_start        # (the megabyte IPL3 copied has the ROM's next bytes there)
    la      $t1, __bss_end
1:  beq     $t0, $t1, 2f
    nop
    sw      $zero, 0($t0)
    b       1b
    addiu   $t0, $t0, 4
2:  j       game_boot
    nop

    .macro  STUB name
    .globl  \name
\name:
    jr      $ra
    nop
    nop
    nop
    .endm
#define X(n) STUB n ;
    LAYOUT_STUBS(X)
