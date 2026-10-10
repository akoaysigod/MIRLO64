#!/bin/bash
# Verilates rtl/soc/rcp_ai.sv against tb_ai.cpp (the AI's queue / interrupt semantics, the reader's lead, an HVQM2-style driver).
#   ./build.sh [rtl dir]   -> obj_ai/tb_ai          ./obj_ai/tb_ai [handler latency us]  (exit status 0: all checks pass)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; S="${1:-$HERE/../../rtl/soc}"
export VERILATOR_ROOT="${VERILATOR_ROOT:-$HOME/local/usr/share/verilator}"
V="${VERILATOR:-$HOME/local/usr/bin/verilator}"
cd "$HERE"
"$V" --cc --exe --build -j 8 -O2 -Wno-fatal -Wno-lint -Wno-style --public-flat-rw --top-module rcp_ai --Mdir obj_ai \
    "$S/rcp_ai.sv" tb_ai.cpp -o tb_ai > obj_ai.log 2>&1 || { grep -E "%Error" obj_ai.log | head; exit 1; }
echo "OK -> $HERE/obj_ai/tb_ai"
