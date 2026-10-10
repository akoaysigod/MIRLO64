#!/usr/bin/env python3
"""layout_test's ROM: 64 MiB -- an N64 header, the game (layout_game.c) where
IPL3 takes a game's first megabyte from, and from 0x20000 on words that say
where they are (layout_game.h's LAYOUT_ROM_WORD), which the game and the
bench both check.

    make_layout_rom.py <game.bin> <out.z64> [megabytes]
"""
import array
import struct
import sys

FROM = 0x20000


def main():
    game = open(sys.argv[1], "rb").read()
    size = (int(sys.argv[3]) if len(sys.argv) > 3 else 64) << 20
    assert 0x1000 + len(game) <= FROM, "the game is too big"
    head = struct.pack(">IIII", 0x80371240, 0x0000000F, 0x80000400, 0x00001444) + bytes(16) + b"LAYOUT TEST".ljust(20)
    rom = head.ljust(0x1000, b"\0") + game
    rom = rom.ljust(FROM, b"\0")
    words = array.array("I", ((o * 2654435761 ^ 0x5EED5EED) & 0xFFFFFFFF for o in range(FROM, size, 4)))
    if sys.byteorder == "little":
        words.byteswap()
    open(sys.argv[2], "wb").write(rom + words.tobytes())


if __name__ == "__main__":
    main()
