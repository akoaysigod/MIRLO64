// One copy of TMEM for one of the texture filter's texels (rdp_pix reads
// three texels a pixel, each from its own copy; rdp_mem's loads write all the
// copies alike).
//
// TMEM is the N64's 4 KiB as 2048 16-bit words (lang/c/mrdp/mrdp.c,
// MRDP_N64): four memories of 512, {half, lane} -- the lower and upper 2 KiB,
// the even and odd 16-bit words. A read at 16-bit word `ra` gives that word,
// and with it the same word of the upper half (a 32-bit texel's B and A).
// The TLUT's entries -- the 16-bit words 4 * index of the upper half -- are
// kept a second time in a memory of their own, for the lookup's read.
module rdp_tmem (
    input  wire        clk,
    // write (loads): the memories enabled, {upper lane 1, upper lane 0, lower lane 1, lower lane 0}
    input  wire [3:0]  we,
    input  wire [8:0]  wa,
    input  wire [15:0] wd0, wd1, wd2, wd3,
    // read: the data one cycle after the address
    input  wire [10:0] ra,
    output wire [15:0] q,           // word ra
    output wire [15:0] qh,          // the upper half's word at ra[9:0]
    input  wire [7:0]  ta,
    output wire [15:0] tq           // TLUT entry ta
);
    wire [15:0] q0, q1, q2, q3;
    mrdp_sdpram #(.AW(9), .DW(16), .NB(1)) m0 (.clk(clk), .we(we[0]), .waddr(wa), .wdata(wd0), .wbe(1'b1), .raddr(ra[9:1]), .rdata(q0));
    mrdp_sdpram #(.AW(9), .DW(16), .NB(1)) m1 (.clk(clk), .we(we[1]), .waddr(wa), .wdata(wd1), .wbe(1'b1), .raddr(ra[9:1]), .rdata(q1));
    mrdp_sdpram #(.AW(9), .DW(16), .NB(1)) m2 (.clk(clk), .we(we[2]), .waddr(wa), .wdata(wd2), .wbe(1'b1), .raddr(ra[9:1]), .rdata(q2));
    mrdp_sdpram #(.AW(9), .DW(16), .NB(1)) m3 (.clk(clk), .we(we[3]), .waddr(wa), .wdata(wd3), .wbe(1'b1), .raddr(ra[9:1]), .rdata(q3));
    mrdp_sdpram #(.AW(8), .DW(16), .NB(1)) tl (.clk(clk), .we(we[2] && !wa[0]), .waddr(wa[8:1]), .wdata(wd2), .wbe(1'b1), .raddr(ta), .rdata(tq));
    reg half, lane;
    always @(posedge clk) begin half <= ra[10]; lane <= ra[0]; end
    assign q  = half ? (lane ? q3 : q2) : (lane ? q1 : q0);
    assign qh = lane ? q3 : q2;
endmodule
