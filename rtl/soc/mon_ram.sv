// The monitor level's own memory (MIRLO_MON): 2 KiB at 0xFFFF_8000 on the
// game CPU's device bus. Its address is the top of the 32-bit space so that a
// store with $zero as its base reaches it -- the monitor's first instructions,
// when every register is the game's, put two of them here and so get the
// scratch the rest of the entry needs. A block of RAM: nothing of the logic
// (a byte lane each, as the block RAM's byte enables are written).
`default_nettype none

module mon_ram (
    input  wire         clk,
    input  wire         we,
    input  wire  [8:0]  a,
    input  wire  [31:0] wd,
    input  wire  [3:0]  be,
    output wire  [31:0] rd
);
genvar g;
generate for (g = 0; g < 4; g = g + 1) begin : lane
    (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m [0:511];
    reg [7:0] q;
    always @(posedge clk) begin
        if (we && be[g]) m[a] <= wd[8 * g +: 8];
        q <= m[a];
    end
    assign rd[8 * g +: 8] = q;
end endgenerate
endmodule
`default_nettype wire
