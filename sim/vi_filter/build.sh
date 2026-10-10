#!/bin/bash
# Verilate rtl/soc/n64_video.sv with MIRLO_VI_AA against tb_vi.cpp's model of the VI filters.
#   ./build.sh && ./obj_vi/tb_vi <seed> <divot 0/1> <partial %>
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; S="$HERE/../../rtl/soc"
export VERILATOR_ROOT="${VERILATOR_ROOT:-$HOME/local/usr/share/verilator}"
V="${VERILATOR:-$HOME/local/usr/bin/verilator}"
cd "$HERE"
"$V" --cc --exe --build -j 8 -O2 -Wno-fatal -Wno-lint -Wno-style --top-module n64_video -GN64FB=1 -GORIGIN_MASK=24\'hFFFFFF \
    +define+MIRLO_VI_AA=1 --Mdir obj_vi "$S/n64_video.sv" "$S/afifo.sv" tb_vi.cpp -o tb_vi > obj_vi.log 2>&1 || { grep -E "%Error" obj_vi.log | head; exit 1; }
echo "OK -> $HERE/obj_vi/tb_vi"
