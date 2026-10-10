// rsp_lutram: 32 words of LUT RAM (MLAB), one clocked write, the read in the
// cycle -- a lane of the vector registers (rsp_vu). Plain Verilog, in the one
// form Quartus maps to altdpram without adding bypass logic.
`default_nettype none
module rsp_lutram #(
    parameter W = 16
) (
    input  wire         clk,
    input  wire         we,
    input  wire [4:0]   wa,
    input  wire [W-1:0] wd,
    input  wire [4:0]   ra,
    output wire [W-1:0] q
);
    (* ramstyle = "MLAB, no_rw_check" *) reg [W-1:0] m [0:31];
    always @(posedge clk) if (we) m[wa] <= wd;
    assign q = m[ra];
endmodule
`default_nettype wire
