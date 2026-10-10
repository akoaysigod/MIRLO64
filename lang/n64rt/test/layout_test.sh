#!/bin/bash
# layout_test, its two runs (layout.mk made build/layout) on the SoC in
# sim/mips_soc, built with MIRLO_RSP, MIRLO_Z_PSRAM and MIRLO_ROM64. The ROM,
# the program and the slots' files all go through the bridge's port, as the
# Pocket writes them (BRIDGE_ALL): in the first run the slots after the ROM
# and the program, in the second -- on the two saves the first run left, read
# back through the bridge -- the slots first. Some 150 M cycles each.
#   layout_test.sh <.../tb_soc> [a directory for the runs' files]
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TB="${1:?the bench: sim/mips_soc/obj_.../tb_soc}"
B="$HERE/build/layout"
OUT="${2:-$B/run}"
mkdir -p "$OUT/a" "$OUT/b"
python3 "$HERE/layout_check.py" make "$OUT"
run() {     # the run, then its slots
    local r=$1; shift
    env VERIFY=1 BRIDGE_ALL=1 ROM="$B/layout.z64" SLOTS_OUT="$OUT/$r" "$@" "$TB" "$B/layout.bin" -c 400 > "$OUT/$r.log" 2>&1 || true
    grep -a "layout_test\|verify\|read back\|errors" "$OUT/$r.log"
}
run a SLOT1="$OUT/eep0.bin"
run b SLOTS_FIRST=1 SLOT1="$OUT/a/slot1.bin" SLOT2="$OUT/a/slot2.bin"
python3 "$HERE/layout_check.py" check "$OUT"
