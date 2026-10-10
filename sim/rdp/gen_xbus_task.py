#!/usr/bin/env python3
"""A task whose RSP program hands the RDP its commands through DMEM (XBUS),
as the first microcodes did -- no real task here does: for tb_dpc.

It sets XBUS, then several times over: brings a batch of random 64-bit words
from RDRAM into DMEM by DMA, writes DPC_START and DPC_END around them (now
and then END in two steps, the second extending the first), and waits until
DPC_CURRENT has reached END before it uses the buffer again. Then BREAK.

    gen_xbus_task.py SEED OUT.bin
"""
import random, struct, sys

RDRAM = 8 << 20
seed, out = int(sys.argv[1]), sys.argv[2]
rnd = random.Random(seed)
code = []
emit = code.append
ori = lambda rt, imm: emit(0x0D << 26 | rt << 16 | (imm & 0xFFFF))
lui = lambda rt, imm: emit(0x0F << 26 | rt << 16 | (imm & 0xFFFF))
mtc0 = lambda rt, rd: emit(0x10 << 26 | 4 << 21 | rt << 16 | rd << 11)
mfc0 = lambda rt, rd: emit(0x10 << 26 | rt << 16 | rd << 11)

DATA = 0x100000
ram = bytearray(RDRAM)
pos = DATA
ori(1, 2); mtc0(1, 11)                                   # DPC_STATUS: set XBUS
for batch in range(rnd.randrange(6, 14)):
    n = rnd.randrange(1, 100)                            # 64-bit commands
    buf = 0x400 + 8 * rnd.randrange(0, 0x180 - n) if n < 0x180 else 0x400
    for i in range(2 * n):                               # (one-word commands: 0x30..0x3F)
        w = rnd.getrandbits(32)
        struct.pack_into(">I", ram, pos + 4 * i, w if i & 1 else (w & 0xC0FFFFFF) | (0x30 + rnd.randrange(16)) << 24)
    ori(1, buf); mtc0(1, 0)                              # SP_MEM_ADDR
    lui(1, pos >> 16); ori(1, pos & 0xFFFF) if pos & 0xFFFF else None
    if pos & 0xFFFF:
        code[-1] = 0x0D << 26 | 1 << 21 | 1 << 16 | (pos & 0xFFFF)   # ori r1, r1, lo
    mtc0(1, 1)                                           # SP_DRAM_ADDR
    ori(1, 8 * n - 1); mtc0(1, 2)                        # SP_RD_LEN
    mfc0(1, 6); emit(0x05 << 26 | 1 << 21 | 0xFFFE); emit(0)   # wait for the DMA
    ori(1, buf); mtc0(1, 8)                              # DPC_START
    if n > 2 and rnd.random() < 0.4:
        ori(1, buf + 8 * rnd.randrange(1, n)); mtc0(1, 9)   # DPC_END, part of the way
        for _ in range(rnd.randrange(0, 12)): emit(0)
    ori(1, buf + 8 * n); mtc0(1, 9)                      # DPC_END
    mfc0(2, 10); emit(0x05 << 26 | 2 << 21 | 1 << 16 | 0xFFFE); emit(0)   # until DPC_CURRENT == END
    pos += 8 * n
emit(0x0000000D)                                         # BREAK
assert len(code) <= 1024
BOOT, TASK = 0x200000, 0x300000
ram[BOOT:BOOT + 4 * len(code)] = struct.pack(f">{len(code)}I", *code)
task = [0] * 16
task[0] = 1
task[2], task[3] = 0x80000000 | BOOT, 4 * len(code)
ram[TASK:TASK + 64] = struct.pack(">16I", *task)
open(out, "wb").write(b"RSPT" + struct.pack(">I", TASK) + bytes(8) + ram)
