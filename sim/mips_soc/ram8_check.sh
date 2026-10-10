#!/bin/bash
# ram8_check.sh OBJ_DIR [Mcycles] -- lang/mips/test/ram8_test.c on the whole SoC (a tb_soc built with the closing macros:
# MIRLO_RSP, MON, MI, PI, VI, AI, SI, PI2...; build.sh soc), and what the program cannot see itself:
#   the test's own checks (PASS), the AI's frames against the buffer it was handed above 4 MiB (tools/ai_check.py --snap),
#   the scan-out's base (VITRACE: ORIGIN 0x640000 -> SDRAM 0x03E40000) and its picture (VGA=1: left half red, right half blue).
# The program: make -C lang/mips/test ram8_test (MIPS_CC, MIPS_BIN in the environment, as for the other tests).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$HERE/../.." && pwd)"
OBJ="${1:?usage: ram8_check.sh OBJ_DIR [Mcycles]}"; C="${2:-60}"; O="${OUT:-/tmp/ram8_check}"
mkdir -p "$O"
# (the test is a program linked for 0x4000_0000: slot 0 at 0, not at the ROM's 1 MiB)
python3 - "$R/pkg/pocket/Cores/tortuga.Mirlo/data.json" "$O/data0.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
d['data']['data_slots'][0]['address'] = '0x00000000'
json.dump(d, open(sys.argv[2], 'w'))
EOF
cd "$HERE"
DATA_JSON="$O/data0.json" AILOG="$O/ai.log" VITRACE=1 VGA=1 VGAT=1 FB_N64=1 \
    "$OBJ/tb_soc" "$R/lang/mips/test/build/ram8_test.bin" -c "$C" -o "$O" -e 1 > "$O/run.log" 2>&1
grep -E "^(ram8_test|FAIL|G:|A$|B$|C$|D$|E$|F$|G$)" "$O/run.log" || true
fail=0
grep -q "ram8_test: PASS" "$O/run.log" || { echo "ram8_check: the program did not PASS"; fail=1; }
python3 "$R/tools/ai_check.py" "$O/ai.log" --snap > "$O/ai_check.txt" 2>&1 && echo "ai_check: the frames are the buffer" || { echo "ai_check: FAILED"; cat "$O/ai_check.txt"; fail=1; }
grep -q "base 03e40000" "$O/run.log" && echo "VI: the scan-out's base is RDRAM + 0x640000" || { echo "VI: no scan-out base 03e40000"; fail=1; }
last=$(ls "$O"/v*.ppm | tail -1)
python3 - "$last" <<'EOF' || fail=1
import sys
d = open(sys.argv[1], 'rb').read()
i = d.index(b'\n'); hdr = d[:i].split()
w, h = int(hdr[1]), int(hdr[2]); pix = d[i + 1:]
def px(x, y): o = 3 * (y * w + x); return tuple(pix[o:o + 3])
l, r = px(80, 120), px(240, 120)
ok = l[0] > 200 and l[1] < 60 and l[2] < 60 and r[2] > 200 and r[0] < 60 and r[1] < 60
print("VI: scan-out picture left", l, "right", r, "-> ok" if ok else "-> WRONG")
sys.exit(0 if ok else 1)
EOF
[ $fail = 0 ] && echo "ram8_check: PASS" || { echo "ram8_check: FAILED"; exit 1; }
