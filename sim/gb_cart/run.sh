#!/bin/bash
# The cartridge port's bus master against the cartridge model, and the model's
# checks against mutants of the bus master (each must fail).
#   sim/gb_cart/run.sh <rom.gb>      (lang/n64rt/test/make_gb.py makes one)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$HERE/../.."
export VERILATOR_ROOT="${VERILATOR_ROOT:-$HOME/local/usr/share/verilator}"
VL="${VERILATOR:-$HOME/local/usr/bin/verilator}"
ROM="${1:?a Game Boy ROM}"
build() {   # name, gb_cart.sv
    "$VL" --binary --timing -j 4 -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Mdir "$HERE/obj_$1" \
        --top-module tb_gb_cart "$2" "$R/sim/mips_soc/gb_cart_model.sv" "$HERE/tb_gb_cart.sv" -o tb > "$HERE/obj_$1.log" 2>&1 \
        || { grep -A3 "%Error" "$HERE/obj_$1.log" | head; return 1; }
    "$HERE/obj_$1/tb" +gbcart="$ROM" 2>&1 | grep -a "tb_gb_cart\|GBCART ERROR\|FAIL" | head -${3:-6}
}
echo "== gb_cart.sv"
build ref "$R/rtl/soc/gb_cart.sv" || exit 1
mutant() {  # name, sed expression, what it breaks
    sed "$2" "$R/rtl/soc/gb_cart.sv" > "$HERE/obj_mut_$1.sv"
    cmp -s "$HERE/obj_mut_$1.sv" "$R/rtl/soc/gb_cart.sv" && { echo "== mutant $1: the edit matched nothing"; return; }
    echo "== mutant $1 ($3): must fail"
    build "mut_$1" "$HERE/obj_mut_$1.sv" 3
}
mutant sample  "s/if (t == 6'd58) rdata <= d2;/if (t == 6'd12) rdata <= d2;/"                   "the data taken 130 ns after /RD"
mutant turn    "s/if (t == 6'd4)  gb_rd_n <= 0;/if (t == 6'd4) begin gb_rd_n <= 0; dw <= a_ok; end/" "the data bank outward in a read"
mutant hold    "s/if (t == 6'd56) dw <= 0;/if (t == 6'd47) dw <= 0;/"                           "the data released before /WR rises"
mutant wide    "s/if (t == 6'd48) gb_wr_n <= 1;/if (t == 6'd20) gb_wr_n <= 1;/"                 "a 64 ns /WR"
mutant trust   "s/if (t == 6'd16 \&\& a_ok) gb_wr_n <= 0;/if (t == 6'd16) gb_wr_n <= 0;/"        "/WR without trust"
mutant stagger "s/assign gb_d_dir = dw || dw_d;     assign gb_d_oe = dw \&\& dw_d;/assign gb_d_dir = dw \&\& dw_d; assign gb_d_oe = dw || dw_d;/" "the FPGA's driver on before its translator turns"
