// rtl/soc/gb_cart.sv against sim/mips_soc/gb_cart_model.sv (an MBC3 with the
// port's timing and contention checks): random reads and writes of the ROM,
// its banks and the RAM, the port switched off and on in between, compared
// with a model of the memory here. +gbcart=<rom.gb> (lang/n64rt/test/make_gb.py).
// run.sh also builds mutants of gb_cart.sv, each of which must be caught.
`default_nettype none
module tb_gb_cart;
logic clk = 0, rst = 1;
always #8 clk = !clk;
logic        on = 0, trust = 0, start = 0, we = 0;
logic [15:0] addr = 0;
logic [7:0]  wdata = 0;
wire         busy;
wire  [7:0]  rdata;
wire         gb_on, gb_a_dir, gb_a_oe, gb_d_dir, gb_d_oe, gb_phi, gb_wr_n, gb_rd_n, gb_cs_n;
wire  [15:0] gb_a;
wire  [7:0]  gb_d_o, gb_d_i;
gb_cart dut (.*);
gb_cart_model cart (.clk, .res_n(gb_on), .a_dir(gb_a_dir), .a_oe(gb_a_oe), .a(gb_a), .d_dir(gb_d_dir), .d_oe(gb_d_oe),
    .d_o(gb_d_o), .d_i(gb_d_i), .phi(gb_phi), .wr_n(gb_wr_n), .rd_n(gb_rd_n), .cs_n(gb_cs_n));

int          fails = 0, n_rd = 0, n_wr = 0;
logic [7:0]  ram [1 << 15];
logic        m_en;
logic [6:0]  m_rom;
logic [3:0]  m_ram;
task automatic xfer(input logic w, input logic [15:0] a, input logic [7:0] d, output logic [7:0] q);
    int guard = 0;
    @(posedge clk); start <= 1; we <= w; addr <= a; wdata <= d;
    @(posedge clk); start <= 0;
    @(posedge clk);
    while (busy && guard < 400) begin @(posedge clk); guard = guard + 1; end
    if (busy) begin $display("FAIL: busy never ends"); fails++; end
    q = rdata;
endtask
task automatic power(input logic o);
    @(posedge clk); on <= o; trust <= o;
    repeat (200) @(posedge clk);
    if (!o) begin m_en = 0; m_rom = 1; m_ram = 0; end
endtask
initial begin
    logic [7:0] q, exp;
    logic [15:0] a;
    for (int i = 0; i < (1 << 15); i++) ram[i] = 0;
    m_en = 0; m_rom = 1; m_ram = 0;
    repeat (8) @(posedge clk);
    rst <= 0;
    // off: an access is answered at once and reaches nothing
    xfer(0, 16'h0100, 0, q);
    if (q !== 8'hFF) begin $display("FAIL: off, read %02x", q); fails++; end
    power(1);
    for (int k = 0; k < 6000; k++) begin
        int r = $urandom_range(99);
        if (r < 2) begin power(0); power(1); end
        else if (r < 8) begin xfer(1, 16'h0000 + $urandom_range(16'h1FFF), 8'h0A, q); m_en = 1; n_wr++; end
        else if (r < 10) begin xfer(1, 16'h1000, 8'h00, q); m_en = 0; n_wr++; end
        else if (r < 20) begin q = 8'($urandom_range(63)); xfer(1, 16'h2000 + $urandom_range(16'h1FFF), q, exp); m_rom = q[6:0] == 0 ? 7'd1 : q[6:0]; n_wr++; end
        else if (r < 28) begin q = 8'($urandom_range(3)); xfer(1, 16'h4000 + $urandom_range(16'h1FFF), q, exp); m_ram = q[3:0]; n_wr++; end
        else if (r < 50) begin
            a = 16'hA000 + 16'($urandom_range(16'h1FFF)); exp = 8'($urandom);
            xfer(1, a, exp, q); n_wr++;
            if (m_en) ram[{m_ram[1:0], a[12:0]}] = exp;
        end else if (r < 75) begin
            a = 16'($urandom_range(16'h7FFF));
            xfer(0, a, 0, q); n_rd++;
            exp = cart.rom[a[14] ? {m_rom[5:0], a[13:0]} : {6'd0, a[13:0]}];
            if (q !== exp) begin $display("FAIL: ROM %04x (bank %0d) read %02x, is %02x", a, m_rom, q, exp); fails++; end
        end else begin
            a = 16'hA000 + 16'($urandom_range(16'h1FFF));
            xfer(0, a, 0, q); n_rd++;
            exp = ram[{m_ram[1:0], a[12:0]}];
            if (m_en && q !== exp) begin $display("FAIL: RAM %04x (bank %0d) read %02x, is %02x", a, m_ram, q, exp); fails++; end
        end
        if (fails > 10) k = 6000;
    end
    // not trusted: a write and a RAM read reach the cartridge as nothing (no /WR, no /CS)
    power(0);
    @(posedge clk); on <= 1; trust <= 0; repeat (200) @(posedge clk);
    begin
        int w0 = cart.writes;
        xfer(1, 16'h2000, 8'h05, q);
        xfer(1, 16'hA000, 8'h55, q);
        if (cart.writes != w0) begin $display("FAIL: a write got through without trust"); fails++; end
        xfer(0, 16'h7FFF, 0, q);
        if (q !== cart.rom[20'h07FFF]) begin $display("FAIL: untrusted ROM read %02x", q); fails++; end
    end
    power(0);
    $display("tb_gb_cart: %0d reads, %0d writes, %0d mismatches, the cartridge's checks: %0d errors", n_rd, n_wr, fails, cart.errors);
    $display((fails == 0 && cart.errors == 0) ? "tb_gb_cart: PASS" : "tb_gb_cart: FAILED");
    $finish;
end
endmodule
`default_nettype wire
