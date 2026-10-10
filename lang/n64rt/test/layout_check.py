#!/usr/bin/env python3
"""layout_test's files (layout_test.sh):

    layout_check.py make <dir>     the first run's save, random bytes: <dir>/eep0.bin
    layout_check.py check <dir>    the two runs' logs and what was read back from the
                                   slots (<dir>/a, <dir>/b) against what each run was given
"""
import random
import struct
import sys


def make(d):
    rnd = random.Random(49)
    open(f"{d}/eep0.bin", "wb").write(bytes(rnd.getrandbits(8) for _ in range(512)))


def check(d):
    bad = []

    def want(what, got, exp):
        if got != exp:
            bad.append(f"{what}: {got!r}, not {exp!r}")

    given = {"a": f"{d}/eep0.bin", "b": f"{d}/a/slot1.bin"}
    for gen, run in ((1, "a"), (2, "b")):
        log = open(f"{d}/{run}.log", errors="replace").read()
        for line in ("layout_test: PASS", " 0 SDRAM protocol errors"):
            want(f"run {run}: '{line}' in the log", line in log, True)
        want(f"run {run}: the two comparisons with the files", log.count(" 0 differ]"), 2)
        eep_in = open(given[run], "rb").read()
        eep = open(f"{d}/{run}/slot1.bin", "rb").read()
        pak = open(f"{d}/{run}/slot2.bin", "rb").read()
        want(f"run {run}: the sizes read back", (len(eep), len(pak)), (512, 0x8000))
        r = dict(zip("magic gen fails f0 f1 f2 rom_words eep_sum cpak_gen cpak_free".split(),
                     struct.unpack(">10I", eep[:40])))
        want(f"run {run}: the report's magic", r["magic"], 0x4C415954)
        want(f"run {run}: its run", r["gen"], gen)
        want(f"run {run}: failed checks", r["fails"], 0)
        want(f"run {run}: the EEPROM as the game found it", r["eep_sum"], sum(eep_in))
        want(f"run {run}: the Controller Pak's file as found", r["cpak_gen"], gen - 1)
        want(f"run {run}: the Controller Pak's free bytes", r["cpak_free"], 123 * 256 - 256)
        want(f"run {run}: the EEPROM's other bytes", eep[64:], bytes((i * 5 + gen) & 255 for i in range(64, 512)))
        want(f"run {run}: the Controller Pak has the game's file", bytes((gen * 17 + i * 3) & 255 for i in range(256)) in pak, True)
        print(f"run {run}: {r['rom_words']} ROM words read by the game; the saves read back: "
              f"EEPROM {len(eep)}, Controller Pak {len(pak)} bytes")
    for b in bad:
        print("FAIL " + b)
    print("layout_test: FAILED" if bad else "layout_test: PASS (both runs)")
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("make", "check"):
        sys.exit(__doc__)
    sys.exit(make(sys.argv[2]) if sys.argv[1] == "make" else check(sys.argv[2]))
