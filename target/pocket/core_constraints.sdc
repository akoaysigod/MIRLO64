#
# user core constraints
#
# put your clock groups in here as well as any net assignments
#

# Fix complaining about this net acting as a clock. Only used for JTAG, and this is not the correct period
create_clock -period 10MHz -name altera_reserved_tck [get_ports { altera_reserved_tck }]

# The SoC's clocks that are timed against each other: clk_sys, its double, the
# video clock -- and the game CPU's own, in a core built with MIRLO_CPU_CLK
# (a ratio of clk_sys off the same VCO: rtl/soc/cpu_port.sv crosses the two
# with no synchronisers, so every path between them is a timed one)
set core_sys_clocks [list \
    {ic|mp1|mf_pllbase_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk} \
    {ic|mp1|mf_pllbase_inst|altera_pll_i|general[1].gpll~PLL_OUTPUT_COUNTER|divclk} \
    {ic|mp1|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk}]
set core_cpu_clock {ic|mp1|mf_pllbase_inst|altera_pll_i|general[5].gpll~PLL_OUTPUT_COUNTER|divclk}
if {[get_collection_size [get_clocks -nowarn $core_cpu_clock]] > 0} { lappend core_sys_clocks $core_cpu_clock }

set_clock_groups -asynchronous \
 -group { bridge_spiclk } \
 -group { clk_74a } \
 -group { clk_74b } \
 -group { altera_reserved_tck } \
 -group $core_sys_clocks \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[2].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[4].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|audio|audio_pll|mf_audio_pll_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk \
          ic|audio|audio_pll|mf_audio_pll_inst|altera_pll_i|general[1].gpll~PLL_OUTPUT_COUNTER|divclk }

