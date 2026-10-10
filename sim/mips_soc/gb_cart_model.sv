// A Game Boy cartridge on the Pocket's cartridge port, for sim/mips_soc (the
// SoC built with MIRLO_GB_CART): an MBC3 with 32 KiB of RAM and a ROM read
// from a file (+gbcart=<rom.gb>; without it the slot is empty), as slow as a
// real one, and what watches the port's pins:
//   - the cartridge's data is good ACC clocks after its address and /RD (or
//     /CS) settle, and stays on the bus OFF clocks after /RD rises;
//   - a write takes the address and data as they are when /WR rises, and wants
//     them still since /WR fell (and the address SU clocks before that, both
//     HOLD clocks after);
// and the errors it counts (each printed, GBCART ERROR):
//   - the cartridge and the port driving D together (the data bank's
//     translator outward while the cartridge drives, OFF clocks included);
//   - the FPGA's driver on with its translator inward (address or data bank);
//   - /RD and /WR low together; a strobe shorter than PULSE clocks;
//   - the address, /CS or a write's data changing inside a strobe, or within
//     SU before it or HOLD after a write;
//   - /WR, /RD or /CS low with the cartridge in reset, or with the address
//     not driven;
//   - /WR or /CS low with no Game Boy cartridge in the slot (what a cartridge
//     of another kind must never see).
// A clock is the SoC's (15.9 ns).
`default_nettype none
module gb_cart_model #(
    parameter int ACC = 13,             // 200 ns: a slow mask ROM
    parameter int OFF = 4,
    parameter int SU = 2, HOLD = 2, PULSE = 16
) (
    input  wire         clk,
    input  wire         res_n,          // (the port's reset clamp released)
    input  wire         a_dir, a_oe,
    input  wire  [15:0] a,
    input  wire         d_dir, d_oe,
    input  wire  [7:0]  d_o,            // the port's data out
    output logic [7:0]  d_i,            // what the port reads
    input  wire         phi, wr_n, rd_n, cs_n
);
logic [7:0] rom [1 << 20];
logic [7:0] ram [1 << 15];
logic       present = 0;
int         errors = 0, reads = 0, writes = 0, phis = 0;
string      fn;
integer     fd, got;
initial begin
    for (int i = 0; i < (1 << 15); i++) ram[i] = 8'h00;
    if ($value$plusargs("gbcart=%s", fn)) begin
        fd = $fopen(fn, "rb");
        if (fd == 0) begin $display("gb_cart_model: cannot open %s", fn); $finish; end
        got = $fread(rom, fd);
        $fclose(fd);
        present = 1;
        $display("gb_cart_model: %s, %0d bytes, type %02x", fn, got, rom[20'h147]);
    end else $display("gb_cart_model: no cartridge in the slot");
end
task automatic err(input string what);
    errors++;
    if (errors <= 20) $display("GBCART ERROR %s (a %04x, /RD %b /WR %b /CS %b, D out %b, reset %b)", what, a, rd_n, wr_n, cs_n, d_dir, !res_n);
endtask

// the MBC
logic       ram_en;
logic [6:0] rom_bank;
logic [3:0] ram_bank;
wire        sel_rom = !a[15];
wire        sel_ram = a[15:13] == 3'b101 && !cs_n && ram_en && ram_bank < 4'd4;
wire [7:0]  q = sel_rom ? rom[a[14] ? {rom_bank[5:0], a[13:0]} : {6'd0, a[13:0]}] : ram[{ram_bank[1:0], a[12:0]}];
// the bus
wire        a_drv = a_dir && a_oe;
wire [15:0] a_bus = a;
wire        driving = present && res_n && !rd_n && (sel_rom || sel_ram);
logic [15:0] a_q;
logic        rd_q, wr_q, cs_q, phi_q;
logic [7:0]  d_q;
int          a_age, d_age, cs_age, rd_low, wr_low, since_wr, off_cnt, drv_age;
initial begin rd_q = 1; wr_q = 1; cs_q = 1; phi_q = 1; since_wr = 1000; rd_low = 0; wr_low = 0; off_cnt = 0; end
always_ff @(posedge clk) begin
    a_q <= a_bus; rd_q <= rd_n; wr_q <= wr_n; cs_q <= cs_n; phi_q <= phi; d_q <= d_o;
    a_age  <= (a_bus != a_q || !a_drv) ? 0 : a_age + 1;
    cs_age <= (cs_n != cs_q) ? 0 : cs_age + 1;
    d_age  <= (d_o != d_q || !(d_dir && d_oe)) ? 0 : d_age + 1;
    rd_low <= rd_n ? 0 : rd_low + 1;
    wr_low <= wr_n ? 0 : wr_low + 1;
    since_wr <= !wr_n ? 0 : since_wr + 1;
    if (phi && !phi_q) phis++;
    // what the cartridge puts on D: good ACC clocks after everything settled
    drv_age <= (!driving || a_bus != a_q || cs_n != cs_q) ? 0 : drv_age + 1;
    if (driving) off_cnt <= OFF; else if (off_cnt != 0) off_cnt <= off_cnt - 1;
    if (driving) d_i <= drv_age >= ACC ? q : ~q;
    else if (d_dir && d_oe) d_i <= d_o;                    // (the wire keeps what was last on it)
    // ---- the checks
    if ((driving || off_cnt != 0) && d_dir) err("the cartridge and the port drive D together");
    if (d_oe && !d_dir) err("the FPGA drives D against its translator");
    if (a_oe && !a_dir) err("the FPGA drives A against its translator");
    if (!rd_n && !wr_n) err("/RD and /WR low together");
    if (!res_n && (!rd_n || !wr_n || !cs_n)) err("a strobe with the cartridge in reset");
    if (res_n && !a_drv && (!rd_n || !wr_n)) err("a strobe with the address not driven");
    if (!present && (!wr_n || !cs_n)) err("/WR or /CS with no Game Boy cartridge in the slot");
    if ((!rd_n || !wr_n) && (a_bus != a_q || cs_n != cs_q)) err("the address or /CS changes inside a strobe");
    if (!rd_n && rd_q && a_age < SU) err("/RD too soon after the address");
    if (!wr_n && wr_q && (a_age < SU || cs_age < SU || d_age < SU)) err("/WR too soon after the address, /CS or the data");
    if (!wr_n && !(d_dir && d_oe)) err("/WR with no data driven");
    if (!wr_n && !wr_q && d_o != d_q) err("the data changes inside /WR");
    if (wr_n && since_wr < HOLD && (a_bus != a_q || cs_n != cs_q || d_o != d_q || !(d_dir && d_oe))) err("no hold after /WR");
    if (rd_n && !rd_q) begin reads++; if (rd_low < PULSE) err("/RD pulse too short"); end
    // ---- the cartridge itself
    if (!res_n || !present) begin ram_en <= 0; rom_bank <= 7'd1; ram_bank <= 0; end
    else if (wr_n && !wr_q) begin                           // /WR rises: the write
        writes++;
        if (wr_low < PULSE) err("/WR pulse too short");
        case (a_q[15:13])
        3'b000: ram_en <= d_q[3:0] == 4'hA;
        3'b001: rom_bank <= d_q[6:0] == 0 ? 7'd1 : d_q[6:0];
        3'b010: ram_bank <= d_q[3:0];
        3'b101: if (!cs_q && ram_en && ram_bank < 4'd4) ram[{ram_bank[1:0], a_q[12:0]}] <= d_q;
        default: ;
        endcase
    end
end
// (an empty slot's data pins: +gbfloat=<hex>, 0xFF if not given)
initial begin d_i = 8'hFF; if ($value$plusargs("gbfloat=%h", d_i)) $display("gb_cart_model: D floats at %02x", d_i); end
final $display("gb_cart_model: %0d reads, %0d writes, %0d PHI cycles, %0d errors%s", reads, writes, phis, errors,
               present ? "" : " (no cartridge)");
endmodule
`default_nettype wire
