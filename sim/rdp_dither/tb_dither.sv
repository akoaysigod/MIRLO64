// Exhaustive check of rdp_dither_tab / rdp_dither_add (rtl/rdp/rdp_pix.v)
// against the N64's RGB dither as the documentation gives it:
//   per channel: if (threshold < c[2:0]) c += (c > 247) ? 7 - c[2:0] : 8 - c[2:0];  out = c >> 3
// with the threshold from the 4 x 4 magic square (RGB_DITHER_SEL 0) or Bayer
// matrix (1), by {y[1:0], x[1:0]}; 3 turns it off. And the property that
// matters: over one matrix tile the mean of the dithered 5-bit channel, at 8
// bits, is the 8-bit input -- a ramp has no bands.
module tb_dither;
    reg  [4:0] c; reg [2:0] lo, d; wire [4:0] o;
    reg  off, bayer; reg [1:0] y, x; wire [2:0] dt;
    rdp_dither_add add (.c(c), .lo(lo), .d(d), .o(o));
    rdp_dither_tab tab (.off(off), .bayer(bayer), .y(y), .x(x), .dt(dt));

    integer magic [0:15], bay [0:15];
    integer v, t, e, errs, sum, cl, ref8, ref_o;
    integer cnt_flat, cnt_band, last, tmp;
    initial begin
        magic[0]=0; magic[1]=6; magic[2]=1; magic[3]=7; magic[4]=4; magic[5]=2; magic[6]=5; magic[7]=3;
        magic[8]=3; magic[9]=5; magic[10]=2; magic[11]=4; magic[12]=7; magic[13]=1; magic[14]=6; magic[15]=0;
        bay[0]=0; bay[1]=4; bay[2]=1; bay[3]=5; bay[4]=4; bay[5]=0; bay[6]=5; bay[7]=1;
        bay[8]=3; bay[9]=7; bay[10]=2; bay[11]=6; bay[12]=7; bay[13]=3; bay[14]=6; bay[15]=2;
        errs = 0;
        // 1. the adder: every 8-bit value, every threshold
        for (v = 0; v < 256; v = v + 1)
            for (t = 0; t < 8; t = t + 1) begin
                c = v >> 3; lo = v & 7; d = t; #1;
                tmp = v;
                if (t < (v & 7)) tmp = v + ((v > 247) ? 7 - (v & 7) : 8 - (v & 7));
                ref_o = tmp >> 3;
                if (o !== ref_o[4:0]) begin errs = errs + 1; if (errs < 10) $display("ADD v=%0d t=%0d got %0d want %0d", v, t, o, ref_o); end
            end
        // 2. the tables
        for (cl = 0; cl < 16; cl = cl + 1) begin
            y = cl >> 2; x = cl & 3;
            off = 0; bayer = 0; #1; if (dt !== magic[cl][2:0]) begin errs = errs + 1; $display("MAGIC cl %0d got %0d", cl, dt); end
            bayer = 1; #1; if (dt !== bay[cl][2:0]) begin errs = errs + 1; $display("BAYER cl %0d got %0d", cl, dt); end
            off = 1; #1; if (dt !== 3'd7) begin errs = errs + 1; $display("OFF cl %0d", cl); end
        end
        // 3. no bands: for each 8-bit value, a tile's mean (x 8) is the value (the top 248..255 saturate)
        for (e = 0; e < 2; e = e + 1) begin
            for (v = 0; v < 248; v = v + 1) begin
                sum = 0;
                for (cl = 0; cl < 16; cl = cl + 1) begin
                    y = cl >> 2; x = cl & 3; off = 0; bayer = e; #1;
                    c = v >> 3; lo = v & 7; d = dt; #1;
                    sum = sum + o * 8;
                end
                if (sum != v * 16) begin errs = errs + 1; if (errs < 10) $display("MEAN sel=%0d v=%0d sum=%0d want %0d", e, v, sum, v * 16); end
            end
        end
        // 4. a ramp over 64 pixels of one row, with and without: how many levels / flat runs
        cnt_flat = 0; last = -1; cnt_band = 0;
        for (v = 0; v < 64; v = v + 1) begin   // 64 px: 8-bit value 100 + v/2 (a gentle gradient, 32 steps)
            y = 1; x = v & 3; off = 0; bayer = 0; #1;
            c = (100 + v / 2) >> 3; lo = (100 + v / 2) & 7; d = dt; #1;
            if (o != last) begin cnt_band = cnt_band + 1; last = o; end
        end
        $display("ramp 100..131 over 64 px, row 1: %0d runs of equal output (undithered: 5)", cnt_band);
        if (errs == 0) $display("PASS"); else $display("FAIL %0d", errs);
        $finish;
    end
endmodule
