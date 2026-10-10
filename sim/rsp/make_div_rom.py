#!/usr/bin/env python3
"""The RSP's reciprocal ROM for rtl/rsp/rsp_top.sv ($readmemh): 512 reciprocal
entries, then 512 inverse-square-root entries -- the tables sim/rsp/rsp.c
builds (make_roms), one 16-bit word a line."""
import sys
rcp = [0xFFFF] + [(((1 << 34) // (i + 512)) + 1) >> 8 & 0xFFFF for i in range(1, 512)]
rsq = []
for i in range(512):
    a = (i + 512) >> (i & 1)
    b = 1 << 17
    while a * (b + 1) * (b + 1) < (1 << 44):
        b += 1
    rsq.append((b >> 1) & 0xFFFF)
out = open(sys.argv[1], "w") if len(sys.argv) > 1 else sys.stdout
for w in rcp + rsq:
    out.write(f"{w:04x}\n")
