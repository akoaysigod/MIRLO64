// A Game Boy cartridge in the Pocket's cartridge slot, for the game CPU: one
// byte read or written at a 16-bit address, as the console's bus does it. It
// is what the N64 Transfer Pak's cartridge is when the core's setting says the
// real one (lang/n64rt/n64rt_pak.c); with the Verilog macro MIRLO_GB_CART.
//
// The port (platform/pocket/apf_top.v; the same 32 pins as a Game Boy's):
//   cart_tran_bank3 [7:0]  A7-A0     out while on
//   cart_tran_bank2 [7:0]  A15-A8    out while on
//   cart_tran_bank1 [7:0]  D7-D0     in; out only while a write's data is on it
//   cart_tran_bank0 [7:4]  PHI, /WR, /RD, /CS    always out
//   cart_pin30_pwroff_reset          the clamp that holds /RES (pin 30) low: 1
//                                    lets the cartridge's own pull-up raise it
//   cart_tran_pin30, pin31 (VIN)     never driven
// Every pin goes through a level translator whose direction is the bank's
// `_dir` (1: the FPGA drives). The data bank is the one that turns round: it
// is an output only inside a write, and a write never has /RD low, so the
// translator and the cartridge never drive D together (see the timing). A
// bank's direction changes a clock before the FPGA's own driver comes on and
// a clock after it goes off, so the two never drive the wire between them
// against each other either.
//
// Off (`on` 0; the reset state): the Pocket's template state -- the address
// and data banks inputs, PHI, /WR, /RD, /CS high, the cartridge held in reset
// by the clamp. A cartridge in reset neither drives its data bus nor takes a
// write, so it may be put in or pulled out.
//
// On: /RES released, the address banks driven, PHI running at clk / 64 (1.02
// us at 62.8 MHz, the console's 0.95), high in the first half of its cycle.
// An access takes one PHI cycle, from its rising edge -- clk cycles of 15.9 ns:
//      0  the address (it may be there earlier); /CS low if it is
//         0xA000-0xFDFF, the cartridge's RAM (the console's rule)
//   read:   4  /RD low   (64 ns after the address)
//          58  the data taken (as the pin was 0.8 us after /RD: two registers
//              behind it)
//          60  /RD high; the address stays
//   write:  8  D's translator outward, 9 D driven (/RD has been high for
//              190 ns at least)
//          16  /WR low   (address 255 ns before, data 111 ns)
//          48  /WR high  (a 509 ns pulse)
//          56  D released, 57 its translator inward (data held 127 ns after
//              /WR; the address and /CS 255 ns, to the cycle's end)
// `on` is taken at a cycle's end, so nothing is cut short but by a reset.
//
// Until `trust` is set there is no /CS and no write (a write does nothing): a
// cartridge that has not been read and found to be a Game Boy's sees its
// address pins driven and /RD, nothing else. A GBA cartridge left in the slot
// would answer /CS and /RD on the address pins, and has its save RAM where a
// Game Boy cartridge has its data pins.
`default_nettype none
module gb_cart (
    input  wire         clk,
    input  wire         rst,
    input  wire         on,             // the cartridge out of reset, the bus driven
    input  wire         trust,          // /CS and writes allowed (the cartridge is known to be a Game Boy's)
    input  wire         start,          // an access: one cycle, not while busy
    input  wire         we,
    input  wire  [15:0] addr,
    input  wire  [7:0]  wdata,
    output logic        busy,
    output logic [7:0]  rdata,
    // the port
    output logic        gb_on,          // the reset clamp's release
    output wire         gb_a_dir,       // the address banks' translators outward
    output wire         gb_a_oe,        // ... and the FPGA driving them
    output logic [15:0] gb_a,
    output logic [7:0]  gb_d_o,
    output wire         gb_d_dir,       // the data bank's translator outward
    output wire         gb_d_oe,        // ... and the FPGA driving it
    input  wire  [7:0]  gb_d_i,
    output logic        gb_phi, gb_wr_n, gb_rd_n, gb_cs_n
);
logic [5:0] t;
logic       pend, act, a_we, a_ok;
logic [7:0] a_wdata, d1, d2;
logic       on_d, dw, dw_d;                     // (direction first, driver a clock later; off the other way round)
assign gb_a_dir = gb_on || on_d;  assign gb_a_oe = gb_on && on_d;
assign gb_d_dir = dw || dw_d;     assign gb_d_oe = dw && dw_d;
// (as configured, before the first clock and its reset: the off state too --
// bank0's translator is always outward, so these are on the cartridge's pins
// from the moment the FPGA is loaded)
initial begin
    gb_on = 0; on_d = 0; dw = 0; dw_d = 0; gb_phi = 1; gb_wr_n = 1; gb_rd_n = 1; gb_cs_n = 1;
    busy = 0; pend = 0; act = 0; t = 0; gb_a = 0; gb_d_o = 0;
end
always_ff @(posedge clk) begin
    d1 <= gb_d_i; d2 <= d1;
    on_d <= gb_on; dw_d <= dw;
    if (rst) begin
        gb_on <= 0; on_d <= 0; dw <= 0; dw_d <= 0; t <= 0; pend <= 0; act <= 0; busy <= 0;
        gb_a <= 0; gb_d_o <= 0; gb_phi <= 1; gb_wr_n <= 1; gb_rd_n <= 1; gb_cs_n <= 1;
    end else begin
        if (start && !busy) begin pend <= 1; busy <= 1; a_we <= we; a_ok <= trust; a_wdata <= wdata; gb_a <= addr; end
        if (!gb_on) begin
            // off: wait for `on`; an access asked for meanwhile is answered at once (nothing is read)
            t <= 0; gb_phi <= 1; gb_wr_n <= 1; gb_rd_n <= 1; gb_cs_n <= 1; dw <= 0; act <= 0;
            if (pend) begin pend <= 0; busy <= 0; rdata <= 8'hFF; end
            if (on) gb_on <= 1;
        end else begin
            t <= t + 6'd1;
            if (t == 6'd0) gb_phi <= 1;
            if (t == 6'd32) gb_phi <= 0;
            if (t == 6'd63) begin
                // the cycle's end: the next access starts, or the port goes off
                act <= 0; gb_cs_n <= 1; gb_phi <= 1;
                if (pend) begin
                    pend <= 0; act <= 1;
                    gb_cs_n <= !(a_ok && gb_a >= 16'hA000 && gb_a < 16'hFE00);
                end else if (!on) gb_on <= 0;
            end
            if (act) begin
                if (a_we) begin                         // (not trusted: the cycle passes, nothing is written)
                    if (t == 6'd8  && a_ok) begin gb_d_o <= a_wdata; dw <= 1; end
                    if (t == 6'd16 && a_ok) gb_wr_n <= 0;
                    if (t == 6'd48) gb_wr_n <= 1;
                    if (t == 6'd56) dw <= 0;
                end else begin
                    if (t == 6'd4)  gb_rd_n <= 0;
                    if (t == 6'd58) rdata <= d2;
                    if (t == 6'd60) gb_rd_n <= 1;
                end
                if (t == 6'd62) busy <= 0;
            end
        end
    end
end
endmodule
`default_nettype wire
