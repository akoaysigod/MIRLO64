#!/usr/bin/env python3
"""A Game Boy ROM for pak_test: 1 MiB, MBC3 + RAM + battery, random contents,
a header that adds up, each bank's last byte its number. The logo a header
must carry is not in this repository: it is read from libultra's own check
of it (ULTRALIB's src/io/gbpakreadid.c).

    make_gb.py <ultralib checkout> out.gb
"""
import random
import re
import sys


def main():
    ult, out = sys.argv[1:3]
    src = open(f"{ult}/src/io/gbpakreadid.c").read()
    m = re.search(r"nintendo\[\]\s*=\s*\{([^}]*)\}", src)
    logo = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
    assert len(logo) == 0x30
    rnd = random.Random(64)
    rom = bytearray(rnd.getrandbits(8) for _ in range(0x100000))
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    rom[0x104:0x134] = logo
    rom[0x134:0x144] = b"MIRLO TEST".ljust(16, b"\0")
    rom[0x144:0x146] = b"01"
    rom[0x146:0x14D] = bytes([0x03, 0x13, 0x05, 0x03, 0x01, 0x33, 0x00])   # MBC3 + RAM + battery, 1 MiB, 32 KiB
    x = 0
    for i in range(0x134, 0x14D):
        x = (x - rom[i] - 1) & 0xFF
    rom[0x14D] = x
    for b in range(64):
        rom[b * 0x4000 + 0x3FFF] = b
    open(out, "wb").write(rom)


if __name__ == "__main__":
    main()
