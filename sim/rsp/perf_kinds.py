#!/usr/bin/env python3
"""Where an RSP's cycles go, by instruction: from a log with a line per
executed instruction -- its cycles (since the one before it), then the
instruction word in hex as the last field. tb_rsp writes one (PERF=<file>);
two logs of the same task from two implementations are set side by side.

    perf_kinds.py [-g] ours.perf [other.perf]

-g: by family (vector multiplies, other vector operations, reciprocals,
vector loads, vector stores, DMA, COP2 moves, scalar) instead of by instruction.

In a log with five fields -- cycles, two stall counts, a DMA-busy flag, the
instruction -- the stalls are summed per instruction too, and what executes
while a DMA runs is counted apart (a microcode's wait loop: an implementation
whose DMA holds the program instead has those cycles in its MTC0).
"""
import re
import sys
from collections import defaultdict
import rom_ucode_scan as R

def name(w):
    op = w >> 26
    if op == 0x10 and ((w >> 21) & 31) == 4 and ((w >> 11) & 15) in (2, 3):
        return "MTC0 (starts a DMA)"
    k = R.classify(w)[0]
    return k if isinstance(k, str) else "?"

def read(path):
    by = defaultdict(lambda: [0, 0, 0, 0])            # instructions, cycles, stall a, stall b
    wait = [0, 0]
    for line in open(path):
        f = line.split()
        if not f:
            continue
        c, w = int(f[0]), int(f[-1], 16)
        if len(f) == 5 and f[3] == "1":                # executed while a DMA runs
            wait[0] += 1; wait[1] += c
            continue
        e = by[name(w)]
        e[0] += 1; e[1] += c
        if len(f) == 5:
            e[2] += int(f[1]); e[3] += int(f[2])
    if wait[0]:
        by["(while a DMA runs)"] = [wait[0], wait[1], 0, 0]
    return by

def family(k):
    if "DMA" in k: return "DMA"
    if re.match(r"^[LS][A-Z]V$", k): return "vector loads" if k[0] == "L" else "vector stores"
    if k in ("MFC2", "MTC2", "CFC2", "CTC2"): return "COP2 moves"
    if k.startswith("VRCP") or k.startswith("VRSQ"): return "reciprocals"
    if re.match(r"^V(MUL|MAC|MUD|MAD)", k): return "vector multiplies"
    if k[0] == "V": return "other vector operations"
    return "scalar"

def by_family(b):
    out = defaultdict(lambda: [0, 0, 0, 0])
    for k, e in b.items():
        for i in range(4): out[family(k)][i] += e[i]
    return out

def main():
    args = sys.argv[1:]
    fam = bool(args) and args[0] == "-g"
    if fam: args = args[1:]
    logs = [read(p) for p in args[:2]]
    if not logs:
        sys.exit(__doc__)
    raw = logs
    if fam: logs = [by_family(b) for b in logs]
    tot = [sum(e[1] for e in b.values()) for b in logs]
    ins = [sum(e[0] for e in b.values()) for b in logs]
    keys = sorted(set().union(*[b.keys() for b in logs]), key=lambda k: -logs[0].get(k, [0, 0])[1])
    if len(logs) == 1:
        print(f"{'':22s} {'count':>8s} {'cycles each':>11s} {'% of cycles':>11s}")
        for k in keys:
            n, c = logs[0][k][:2]
            print(f"{k:22s} {n:8d} {c / n:11.2f} {100 * c / tot[0]:10.1f}%")
        print(f"{'total':22s} {ins[0]:8d} {tot[0] / ins[0]:11.2f}   {tot[0]} cycles")
    else:
        a, b = logs
        print(f"{'':22s} {'count':>8s} | {'ours: each':>10s} {'cycles':>9s} | {'other: each':>11s} {'cycles':>9s} | {'more cycles':>11s} {'of the gap':>10s}")
        gap = tot[0] - tot[1]
        for k in keys:
            na, ca = a.get(k, [0, 0])[:2]; nb, cb = b.get(k, [0, 0])[:2]
            ea = f"{ca / na:10.2f}" if na else " " * 10
            eb = f"{cb / nb:11.2f}" if nb else " " * 11
            print(f"{k:22s} {max(na, nb):8d} | {ea} {ca:9d} | {eb} {cb:9d} | {ca - cb:11d} {100 * (ca - cb) / gap:9.1f}%")
        print(f"{'total':22s} {ins[0]:8d} | {tot[0] / ins[0]:10.2f} {tot[0]:9d} | {tot[1] / ins[1]:11.2f} {tot[1]:9d} | {gap:11d}   (x{tot[0] / tot[1]:.2f})")
        if any(e[2] or e[3] for e in raw[1].values()):
            s2 = sum(e[2] for e in b.values()); s3 = sum(e[3] for e in b.values())
            print(f"the other's stalls: {s2} cycles waiting for a vector register, {s3} after loads and register reads")


if __name__ == "__main__":
    main()
