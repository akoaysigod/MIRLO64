// RDP depth engine (RDP_Z_PORT): a row's depth span between the line buffers
// and a depth memory of its own -- the SoC's PSRAM (rtl/soc/psram_z.sv) -- so
// that depth goes beside the colour image's words instead of taking turns
// with them on the one SDRAM. rdp_mem keeps the colour spans and the loads.
//
// The memory is asked a 32-bit word (two pixels, the even one in [31:16]) a
// command, at the word's address in the N64's memory; a row's words go as a
// run of consecutive words whose last has `last` set, which the memory can
// turn into one burst:
//   a read run     the span [x0, x1) of the row at zbase into the old Z
//                  buffer ({set, word}); z_have counts the words in
//   a write run    the row's results out of the new Z buffer and its mask
//                  -- every word of the span, a word with no pixel passed
//                  with no lane enabled -- once the row's last result is in
//                  (res_a / res_b: the results so far, by set), and none at
//                  all for a row no pixel of which passed; or a fill of the
//                  span (the depth image cleared as a colour image)
// A run is not broken into by the other kind. Nothing here orders a read
// after a write of the same word: as rdp_mem, the sequencer starts a
// primitive's reads with the write engine idle.
module rdp_zmem (
    input  wire        clk,
    input  wire        rst,

    // ---- reads
    input  wire        rd_valid,
    output wire        rd_ready,
    input  wire [31:0] rd_zbase,
    input  wire [9:0]  rd_x0,
    input  wire [10:0] rd_x1,          // exclusive
    input  wire        rd_set,
    output reg  [9:0]  z_have,
    output wire        lbw_we,         // the old Z buffer's write port
    output wire [9:0]  lbw_addr,
    output wire [31:0] lbw_data,

    // ---- writes
    input  wire        wr_valid,
    output wire        wr_ready,
    input  wire        wr_fill,
    input  wire [31:0] wr_zbase,
    input  wire [9:0]  wr_x0,
    input  wire [10:0] wr_x1,
    input  wire [31:0] wr_fillc,
    input  wire        wr_set,
    input  wire [10:0] res_a, res_b,
    input  wire        any_a, any_b,   // a pixel of the row has passed, by set (as early as its result)
    output reg  [9:0]  lbr_addr,       // the new Z buffer's read port (data two edges on)
    input  wire [31:0] lbr_zdata,
    input  wire [1:0]  lbr_zmask,      // bit 0 the even pixel

    // ---- the depth memory
    output reg         z_valid,
    input  wire        z_ready,
    output reg         z_we,
    output reg  [20:0] z_addr,         // word address (the N64's 8 MiB)
    output reg  [31:0] z_wdata,
    output reg  [3:0]  z_wmask,        // [3] the first byte, bits 31:24
    output reg         z_last,
    input  wire        z_rvalid,       // read words, in the order asked
    input  wire [31:0] z_rdata,

    output wire        rd_busy,
    output wire        wr_busy,
    output wire        cmd_pending     // a command not yet taken
);
    wire cmd_free = !z_valid || z_ready;
    assign cmd_pending = z_valid;

    // ---- the read run
    reg        r_act, r_set, r_all;
    reg [20:0] r_zb;
    reg [8:0]  r_idx, r_last, q_idx;
    assign lbw_we   = r_act && z_rvalid;
    assign lbw_addr = {r_set, q_idx};
    assign lbw_data = z_rdata;
    wire [10:0] rd_xl = rd_x1 - 11'd1;
    assign rd_ready = !r_act;
    assign rd_busy  = r_act;
    wire       rd_want = r_act && !r_all;

    // ---- the write run
    reg        w_act, w_fillm, w_set, w_all;
    reg [20:0] w_zb;
    reg [10:0] w_len;                  // the span's pixels
    reg        w_odd0, w_odd1;         // it starts at an odd pixel; it ends before one
    wire [31:0] w_fillc = wr_fillc;    // (the fill colour does not change under a fill)
    reg [8:0]  w_idx, w_lastw;
    wire [10:0] wr_xl = wr_x1 - 11'd1;
    assign wr_ready = !w_act;
    assign wr_busy  = w_act;
    // the row is whole: its last pixel's result is in the new buffer
    wire [10:0] w_res  = w_set ? res_b : res_a;
    wire        w_whole = w_fillm || w_res >= w_len;
    // words out of the new buffer (address, two edges, a 4-entry FIFO), in the span's order
    reg        p1, p2;
    reg [33:0] rf [0:3];               // {zdata, zmask}
    reg [2:0]  rf_wr, rf_rd;
    wire [2:0] rf_cnt = rf_wr - rf_rd;
    wire       rf_room = ({1'b0, rf_cnt} + {3'd0, p1} + {3'd0, p2}) < 4'd4;
    wire [33:0] rf_head = rf[rf_rd[1:0]];
    reg  [8:0]  h_idx;                 // the head's word
    reg         h_1st;                 // it is the span's first
    wire [31:0] h_zdata = rf_head[33:2];
    wire [1:0]  h_zmask = rf_head[1:0];
    wire        h_v     = rf_wr != rf_rd;
    // a word's pixels in the span: all but the first word's even one and the last word's odd one
    wire [3:0]  h_range = { {2{!(w_odd0 && h_1st)}}, {2{!(w_odd1 && h_idx == w_lastw)}} };
    wire [3:0]  h_sel   = w_fillm ? h_range : (h_range & { {2{h_zmask[0]}}, {2{h_zmask[1]}} });

    // whose run the channel is in: 0 neither, 1 a read's, 2 a write's
    reg [1:0] own;

    always @(posedge clk) begin
        if (rst) begin
            r_act <= 1'b0; w_act <= 1'b0; z_valid <= 1'b0; own <= 2'd0;
            p1 <= 1'b0; p2 <= 1'b0; rf_wr <= 0; rf_rd <= 0;
            z_have <= 10'd0;
        end else begin
            // ---- the command channel: a run at a time
            if (cmd_free) begin
                z_valid <= 1'b0;
                if (own != 2'd2 && rd_want) begin
                    z_valid <= 1'b1; z_we <= 1'b0;
                    z_addr <= r_zb + {12'd0, r_idx};
                    z_last <= r_idx == r_last;
                    own <= (r_idx == r_last) ? 2'd0 : 2'd1;
                    if (r_idx == r_last) r_all <= 1'b1;
                    r_idx <= r_idx + 9'd1;
                end else if (own != 2'd1 && h_v) begin
                    z_valid <= 1'b1; z_we <= 1'b1;
                    z_addr <= w_zb + {12'd0, h_idx};
                    z_wdata <= w_fillm ? w_fillc : h_zdata;
                    z_wmask <= h_sel;
                    z_last <= h_idx == w_lastw;
                    own <= (h_idx == w_lastw) ? 2'd0 : 2'd2;
                    rf_rd <= rf_rd + 3'd1;
                    h_idx <= h_idx + 9'd1; h_1st <= 1'b0;
                end
            end

            // ---- reads
            if (!r_act) begin
                if (rd_valid) begin
                    r_act <= 1'b1; r_set <= rd_set; r_all <= 1'b0;
                    r_zb <= rd_zbase[22:2];
                    r_idx <= rd_x0[9:1]; q_idx <= rd_x0[9:1]; r_last <= rd_xl[9:1];
                    z_have <= 10'd0;
                end
            end else if (z_rvalid) begin
                z_have <= z_have + 10'd1;
                if (q_idx == r_last) r_act <= 1'b0;
                q_idx <= q_idx + 9'd1;
            end

            // ---- writes
            if (!w_act) begin
                p1 <= 1'b0; p2 <= 1'b0;
                if (wr_valid) begin
                    w_act <= 1'b1; w_fillm <= wr_fill; w_set <= wr_set; w_all <= 1'b0;
                    w_zb <= wr_zbase[22:2];
                    w_len <= wr_x1 - {1'b0, wr_x0}; w_odd0 <= wr_x0[0]; w_odd1 <= wr_x1[0];
                    w_idx <= wr_x0[9:1]; h_idx <= wr_x0[9:1]; h_1st <= 1'b1; w_lastw <= wr_xl[9:1];
                end
            end else begin
                p2 <= p1;
                if (p2) begin
                    rf[rf_wr[1:0]] <= {lbr_zdata, lbr_zmask};
                    rf_wr <= rf_wr + 3'd1;
                end
                if (!w_all && w_whole && !w_fillm && !(w_set ? any_b : any_a)) begin
                    w_all <= 1'b1;                      // nothing passed: nothing to write
                    p1 <= 1'b0;
                end else if (!w_all && rf_room && w_whole) begin
                    lbr_addr <= {w_set, w_idx};
                    p1 <= 1'b1;
                    if (w_idx == w_lastw) w_all <= 1'b1;
                    w_idx <= w_idx + 9'd1;
                end else
                    p1 <= 1'b0;
                if (w_all && !p1 && !p2 && rf_wr == rf_rd) w_act <= 1'b0;
            end
        end
    end
endmodule
