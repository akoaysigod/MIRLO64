// rtl/soc/n64_video.sv with MIRLO_VI_AA against a model of the N64 VI's
// anti-alias and divot filters (the VI_filter / VI_filter_pen behaviour, with the
// coverage of a partial pixel taken as 3: the framebuffer keeps one bit).
//   tb_vi [seed] [divot 0/1] [partial %] [width: 320, 640 (x 480, the two fields woven) or 640p (x 240)] [dedither 0/1]
//   TB_PAL=1: vi_pal on from the start (PAL timing: 313 lines, 1,250,000 sys clocks a field); TB_SEQ=0,1,1,0,...: vi_pal set to the
//   n-th value at the n-th vsync (the TV system changes at the end of the field: the fields are checked line by line)
// A random RGBA5551 framebuffer (bit 0: the pixel is complete) is scanned out
// through the DMA port; a frame is taken off the pads and every pixel compared.
// 640 / 640p: a hi-res buffer is always shown in the 320 x 240 frame (rtl/soc/n64_video.sv): the pixel (x, y) is the
// average of the buffer's (2x, 2y) and (2x + 1, 2y) per channel, (a + b + 1) >> 1 (640: the first field's rows), and the
// VI's filters are off for it: the model is that average << 3. The pads outside DE are 0 (one scaler slot, no frame feature bits) in every mode.
#include "Vn64_video.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>

static Vn64_video *t;
static std::vector<uint16_t> fb;
static std::vector<uint8_t> hid;                    // each pixel's hidden two coverage bits (the RDP's plane)
static unsigned cvg(int x, int y);
static unsigned W = 320, H = 240;                 // the picture shown (the frame)
static unsigned WB = 320;                         // the buffer's width (VI_WIDTH)
static bool hi;                                   // a hi-res buffer (640): reduced to 320 x 240, the filters off
static bool tall;                                 // 640 x 480 (the first field is shown), else 640 x 240
static std::vector<uint16_t> fbw;                 // (the buffer; fb / hid are the picture shown)
static std::vector<uint8_t> hidw;
static unsigned rng = 1;
static int pal0;                                  // TB_PAL
static std::vector<int> seq;                      // TB_SEQ
static int sys_of(int j) { return j <= 0 ? 0 : seq.empty() ? 0 : seq[(size_t)j < seq.size() ? j : seq.size() - 1]; }   // the system of the field F_j
static unsigned rnd() { rng = rng * 1103515245u + 12345u; return rng >> 16; }

// the model: R6 G6 B6 (the pads: R, B their top five)
struct P6 { int r, g, b; };
static int ch(uint16_t p, int c) { return c == 0 ? (p >> 11) & 31 : c == 1 ? (p >> 6) & 31 : (p >> 1) & 31; }
static int rowerr[480];
static unsigned R = 0;                              // TB_ROWS: the picture's rows (VI window), the rest black
static unsigned Y0 = 0;                             // TB_Y0: its first line on the screen
static unsigned CV = 0, CR = 0;                     // TB_CROPV, TB_CROPR: the core's crop options (lines at top and bottom, x2 at 480; right pixels)
static bool inpic(int x, int y) { return x >= 0 && y >= 0 && x < (int)W && y < (int)(R ? R : H); }
static unsigned cvg(int x, int y) { return (fb[y * W + x] & 1u) << 2 | hid[y * W + x]; }
static bool complete(int x, int y) { return cvg(x, y) == 7u; }
static int dedither;                                // the VI's dither filter (argument 5)
static P6 aa_px(int x, int y)
{
    uint16_t c = fb[y * W + x];
    P6 o;
    int *po[3] = { &o.r, &o.g, &o.b };
    if (complete(x, y)) {                            // complete: as it is (8 bits: c << 3, the N64's), or dedithered
        for (int k = 0; k < 3; k++) {
            int cc = ch(c, k), v = cc << 3;
            if (dedither) {
                int add = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    if ((!dx && !dy) || !inpic(x + dx, y + dy)) continue;
                    int n = ch(fb[(y + dy) * W + x + dx], k);
                    add += n > cc ? 1 : n < cc ? -1 : 0;
                }
                v = (cc << 3) + add; if (v > 255) v = 255;
            }
            *po[k] = v;
        }
        return o;
    }
    static const int dx[6] = { -1, 1, -2, 2, -1, 1 }, dy[6] = { -1, -1, 0, 0, 1, 1 };
    for (int k = 0; k < 3; k++) {
        int v[6], cc = ch(c, k);
        for (int i = 0; i < 6; i++) {
            int xx = x + dx[i], yy = y + dy[i];
            v[i] = (inpic(xx, yy) && complete(xx, yy)) ? ch(fb[yy * W + xx], k) : cc;
        }
        // second smallest, second largest
        int s[6]; for (int i = 0; i < 6; i++) s[i] = v[i];
        for (int i = 0; i < 6; i++) for (int j = i + 1; j < 6; j++) if (s[j] < s[i]) { int q = s[i]; s[i] = s[j]; s[j] = q; }
        int mn = s[1] < cc ? s[1] : cc, mx = s[4] > cc ? s[4] : cc;
        // the N64's, on 8-bit channels (c << 3): c8 + ((mn8 + mx8 - 2 c8) * (7 - cvg) + 4) >> 3; its top six bits
        int c8 = cc << 3, w = 7 - (int)cvg(x, y);
        int o8 = c8 + (int)floor(((double)(((mn << 3) + (mx << 3) - 2 * c8) * w) + 4.0) / 8.0);
        *po[k] = o8;
    }
    return o;
}
static int med3(int a, int b, int c) { int s[3] = { a, b, c }; for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++) if (s[j] < s[i]) { int q = s[i]; s[i] = s[j]; s[j] = q; } return s[1]; }

static uint64_t sys_cyc;
static uint64_t pad_hash = 1469598103934665603ull;      // FNV over every pad word and sync of the run (NTSC: the same before and after the PAL change)
static int vph;
static void tick()
{
    // sys every step, clk_vid every fifth
    t->clk = 0; if (vph == 0) t->clk_vid = 0; t->eval();
    // the DMA: a read's word 4 sys cycles on
    static std::vector<std::pair<uint64_t, uint32_t>> pend;
    t->m_ready = (rnd() % 4) != 0;
    if (t->m_valid && t->m_ready) {
        uint32_t a = t->m_addr;                     // word address
        uint32_t i = a * 2, w = 0;
        if (a >= 0xDC0000u && a < 0xE00000u) {      // the hidden plane (SDRAM 0x0370_0000): pixels 16 (a - base) on
            uint32_t p0 = (a - 0xDC0000u) * 16u;
            for (uint32_t k = 0; k < 16; k++) if (p0 + k < hidw.size()) w |= (uint32_t)hidw[p0 + k] << (2 * k);
        } else if (i + 1 < fbw.size()) w = (uint32_t)fbw[i] << 16 | fbw[i + 1];   // (the buffer: the woven one at 640)
        pend.push_back({ sys_cyc + 4 + rnd() % 8, w });
    }
    t->m_rvalid = 0;
    if (!pend.empty() && pend.front().first <= sys_cyc) { t->m_rvalid = 1; t->m_rdata = pend.front().second; pend.erase(pend.begin()); }
    t->clk = 1; if (vph == 0) t->clk_vid = 1; t->eval();
    sys_cyc++; vph = (vph + 1) % 5;
}

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    unsigned seed = argc > 1 ? atoi(argv[1]) : 1;
    int divot = argc > 2 ? atoi(argv[2]) : 0;
    int pct = argc > 3 ? atoi(argv[3]) : 30;
    if (argc > 4 && atoi(argv[4]) == 640) { WB = 640; hi = true; tall = strcmp(argv[4], "640p") != 0; }   // hi-res: a 640 x 480 buffer (the fields woven) or 640 x 240
    if (getenv("TB_ROWS")) R = (unsigned)atoi(getenv("TB_ROWS"));
    dedither = argc > 5 ? atoi(argv[5]) : 0;
    if (getenv("TB_Y0")) Y0 = (unsigned)atoi(getenv("TB_Y0"));
    if (getenv("TB_CROPV")) CV = (unsigned)atoi(getenv("TB_CROPV"));
    if (getenv("TB_CROPR")) CR = (unsigned)atoi(getenv("TB_CROPR"));
    rng = seed * 2654435761u + 7;
    pal0 = getenv("TB_PAL") ? atoi(getenv("TB_PAL")) : 0;
    if (const char *q = getenv("TB_SEQ")) for (; *q; q++) if (*q == '0' || *q == '1') seq.push_back(*q - '0');
    const unsigned HB = hi && tall ? 480 : 240;      // the buffer's rows
    fbw.resize(WB * HB); hidw.resize(WB * HB);
    // patches of colour with partial pixels, and noise
    for (unsigned y = 0; y < HB; y++)
        for (unsigned x = 0; x < WB; x++) {
            uint16_t v = (uint16_t)(rnd() & 0xFFFE);
            if ((rnd() % 4) && x > 0) v = fbw[y * WB + x - 1] & 0xFFFE;
            unsigned c = (int)(rnd() % 100) >= pct ? 7u : rnd() % 7u;      // complete, or a partial coverage 0..6
            fbw[y * WB + x] = v | (uint16_t)(c >> 2);
            hidw[y * WB + x] = (uint8_t)(c & 3u);
        }
    fb.resize(W * H); hid.resize(W * H);
    for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) {       // the picture shown: the buffer, or reduced
        unsigned sx = hi ? 2 * x : x, sy = hi && tall ? 2 * y : y;
        uint16_t a = fbw[sy * WB + sx], b = hi ? fbw[sy * WB + sx + 1] : a;
        auto av = [&](int k) { return (ch(a, k) + ch(b, k) + 1) >> 1; };
        fb[y * W + x] = hi ? (uint16_t)(av(0) << 11 | av(1) << 6 | av(2) << 1 | ((a & b) & 1u)) : a;
        hid[y * W + x] = hidw[sy * WB + sx];
    }
    t = new Vn64_video;
    t->rst = 1; t->rst_vid = 1; t->enable = 1; t->vi_origin = 0; t->vi_width = WB; t->vi_tall = hi && tall; t->vi_black = 0;
    t->gamma = 0; t->vi_aa = 1; t->vi_divot = divot; t->vi_dedither = dedither; t->crop_v = CV; t->crop_r = CR; t->vi_lines = 240; t->vi_x0 = 0; t->vi_y0 = Y0; t->vi_rows = R;
    t->osd_en = 0; t->fps_en = 0; t->vi_pal = seq.empty() ? pal0 : 0;
    for (int i = 0; i < 200; i++) tick();
    t->rst = 0; t->rst_vid = 0;
    // frames off the pads
    std::vector<std::vector<int>> rows; std::vector<int> row;
    int pde = 0, pvs = 0, frames = 0, errs = 0, outside = 0;
    long changed = 0;                               // (pixels the filters change: the test is not empty)
    // the field's timing (n64_video.sv: 59.940 Hz): vsync to vsync = 1,042,710 sys clocks, 264 lines of 790 clk_vid clocks
    // (3950 sys), nine of 788 (3940)
    long vs_at = -1, hs_at = -1, tim_errs = 0; int hs_n = 0, hs_short = 0; unsigned fields_ok = 0;
    int phs = 0;
    const int nfr = seq.empty() ? (pal0 ? 5 : 4) : (int)seq.size() + 3;
    // the lines of the two systems (sys clocks): NTSC 790 clk_vid x 5 = 3950, nine 788 x 5 = 3940; PAL 800 x 5 = 4000, fifty 792 x 5 = 3960
    const long LN[2] = { 3950, 4000 }, LS[2] = { 3940, 3960 }, FLD[2] = { 1042710, 1250000 };
    const int NLN[2] = { 264, 313 }, NSH[2] = { 9, 50 };
    std::vector<long> hs_ds;
    while (frames < nfr) {
        tick();
        if (vph != 1) continue;                     // (just after clk_vid's edge)
        pad_hash = (pad_hash ^ ((uint64_t)t->vga_r | (uint64_t)t->vga_g << 8 | (uint64_t)t->vga_b << 16 | (uint64_t)t->vga_hsync << 24 | (uint64_t)t->vga_vsync << 25 | (uint64_t)t->vga_de << 26 | (uint64_t)t->vga_skip << 27)) * 1099511628211ull;
        if (!phs && t->vga_hsync) {
            if (hs_at >= 0) {
                long d = sys_cyc - hs_at;
                hs_ds.push_back(d);
                if (seq.empty() && (!pal0 || frames >= 2)) {
                    if (d == LS[pal0]) hs_short++; else if (d != LN[pal0] && !(vs_at >= 0 && hs_n == 0)) { if (tim_errs++ < 5) printf("line period %ld sys clocks\n", d); }
                }
            }
            hs_at = sys_cyc; hs_n++;
        }
        phs = t->vga_hsync;
        if (!t->vga_de && !t->vga_skip && (t->vga_r || t->vga_g || t->vga_b) && outside++ < 5) printf("pads outside DE: %d %d %d\n", t->vga_r, t->vga_g, t->vga_b);
        if (t->vga_de && !t->vga_skip) { row.push_back(t->vga_r); row.push_back(t->vga_g); row.push_back(t->vga_b); }
        if (pde && !t->vga_de && !t->vga_skip) { rows.push_back(row); row.clear(); }
        if (!t->vga_skip) pde = t->vga_de;
        if (!pvs && t->vga_vsync) {
            frames++;
            if (seq.empty()) {
              if (vs_at >= 0 && (!pal0 || frames >= 3)) {      // (PAL: the system changes at the end of the first field, NTSC from reset)
                long d = sys_cyc - vs_at;
                if (d != FLD[pal0] || hs_n != NLN[pal0] || hs_short != NSH[pal0]) { if (tim_errs++ < 5) printf("field: %ld sys clocks, %d lines, %d short (want %ld, %d, %d)\n", d, hs_n, hs_short, FLD[pal0], NLN[pal0], NSH[pal0]); }
                else fields_ok++;
              }
            } else if (vs_at >= 0) {
                // the interval from the last vsync (V_j, line 241 of a field F_(j-1)) to this one (V_(j+1), line 241 of F_j): the rest of
                // F_(j-1) (all of its short lines are in it) and the first 242 lines of F_j. The system of F_j is the request made at
                // V_j (taken at the frame boundary of F_(j-1), line 255 or 304, after the vsync, and applied where F_(j-1) ends); F_0 is NTSC
                const int j = frames - 1;
                const int a = sys_of(j - 1), b = sys_of(j);
                long d = sys_cyc - vs_at;
                const long want = (long)(NLN[a] - 242) * LN[a] + 242 * LN[b] - (long)NSH[a] * (LN[a] - LS[a]);
                if (d != want || hs_n != NLN[a]) { if (tim_errs++ < 8) printf("interval %d (field %s then %s): %ld sys clocks, %d lines (want %ld, %d)\n", frames, a ? "PAL" : "NTSC", b ? "PAL" : "NTSC", d, hs_n, want, NLN[a]); }
                else { fields_ok++; printf("interval %d: %s field, then %s: %ld sys clocks, %d lines\n", frames, a ? "PAL" : "NTSC", b ? "PAL" : "NTSC", d, hs_n); }
            }
            if (!seq.empty()) t->vi_pal = sys_of(frames);       // (V_j asks for F_j's system)
            vs_at = sys_cyc; hs_n = 0; hs_short = 0; hs_ds.clear();
            if (frames >= 3) {
                if (rows.size() != H) { printf("frame %d: %zu rows\n", frames, rows.size()); errs++; }
                for (unsigned ys = 0; ys < rows.size() && ys < H; ys++) {
                    if (ys < Y0) {                          // above the window: black
                        for (unsigned x = 0; x < W * 3; x++) if (rows[ys][x]) { if (errs < 10) printf("(%u,%u) above the window\n", x / 3, ys); errs++; break; }
                        continue;
                    }
                    unsigned y = ys - Y0;
                    if (rows[ys].size() != W * 3) { printf("row %u: %zu px\n", ys, rows[ys].size() / 3); errs++; continue; }
                    for (unsigned x = 0; x < W; x++) {
                        if (R && y >= R) {                  // below the window: black
                            int gr = rows[ys][3 * x], gg = rows[ys][3 * x + 1], gb = rows[ys][3 * x + 2];
                            if (gr || gg || gb) { if (errs < 10) printf("(%u,%u) below the window: pads %d %d %d\n", x, ys, gr, gg, gb); errs++; }
                            continue;
                        }
                        unsigned cvl = CV, ph = R ? R : H;
                        if (y < cvl || y >= ph - cvl || x >= W - CR) {     // cropped: black
                            int gr = rows[ys][3 * x], gg = rows[ys][3 * x + 1], gb = rows[ys][3 * x + 2];
                            if (gr || gg || gb) { if (errs < 10) printf("(%u,%u) cropped: pads %d %d %d\n", x, ys, gr, gg, gb); errs++; }
                            continue;
                        }
                        P6 e = hi ? P6{ ch(fb[y * W + x], 0) << 3, ch(fb[y * W + x], 1) << 3, ch(fb[y * W + x], 2) << 3 } : aa_px(x, y);
                        if (divot && !hi) {
                            bool part = !complete(x, y) || (x > 0 && !complete(x - 1, y)) || (x + 1 < W && !complete(x + 1, y));
                            if (part) {
                                P6 l = x > 0 ? aa_px(x - 1, y) : e, r = x + 1 < W ? aa_px(x + 1, y) : e;
                                e.r = med3(l.r, e.r, r.r); e.g = med3(l.g, e.g, r.g); e.b = med3(l.b, e.b, r.b);
                            }
                        }
                        int gr = rows[ys][3 * x], gg = rows[ys][3 * x + 1], gb = rows[ys][3 * x + 2];
                        if (frames == 3 && (e.r >> 3 != ch(fb[y * W + x], 0) || e.g >> 3 != ch(fb[y * W + x], 1) || e.b >> 3 != ch(fb[y * W + x], 2) || (e.r & 7) != 0)) changed++;
                        if (gr != e.r || gg != e.g || gb != e.b) {
                            if (errs < 10) printf("[frame %d] (%u,%u) fb %04x: pads %d %d %d, model %d %d %d\n", frames, x, y, fb[y * W + x], gr, gg, gb, e.r, e.g, e.b);
                            errs++; if (getenv("TB_V")) rowerr[ys]++;
                        }
                    }
                }
                if (getenv("TB_V")) { for (unsigned y = 0; y < 240; y++) if (rowerr[y]) printf("  frame %d row %u: %d bad\n", frames, y, rowerr[y]); memset(rowerr, 0, sizeof rowerr); }
            }
            // TB_DUMP=prefix: the framebuffer (RGBA5551 big-endian) and the frame off the pads (PPM)
            if (frames == 3 && getenv("TB_DUMP")) {
                std::string pfx = getenv("TB_DUMP");
                if (FILE *f = fopen((pfx + ".raw").c_str(), "wb")) { for (uint16_t v : fb) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; fwrite(b, 1, 2, f); } fclose(f); }
                if (FILE *f = fopen((pfx + ".hid").c_str(), "wb")) { fwrite(hid.data(), 1, hid.size(), f); fclose(f); }
                if (FILE *f = fopen((pfx + "_pads.ppm").c_str(), "wb")) {
                    fprintf(f, "P6\n%u %zu\n255\n", W, rows.size());
                    for (auto &r : rows) for (unsigned x = 0; x < W; x++) { uint8_t px[3] = { (uint8_t)r[3 * x], (uint8_t)r[3 * x + 1], (uint8_t)r[3 * x + 2] }; fwrite(px, 1, 3, f); }
                    fclose(f);
                }
            }
            rows.clear(); row.clear();
        }
        pvs = t->vga_vsync;
    }
    errs += outside + (int)tim_errs;
    if (seq.empty()) printf("field timing: %u fields of %ld sys clocks (%s at 62.5 MHz), %d lines each, %d of them shorter\n", fields_ok, FLD[pal0], pal0 ? "50.000 Hz, PAL" : "59.940 Hz", NLN[pal0], NSH[pal0]);
    else printf("field timing: %u intervals as predicted\n", fields_ok);
    if (fields_ok < (unsigned)(seq.empty() ? (pal0 ? 3 : nfr - 1) : seq.size())) { printf("field timing: not seen\n"); errs++; }
    printf("pad hash %016llx\n", (unsigned long long)pad_hash);
    printf("seed %u divot %d dedither %d partial %d%% width %u%s: %s (%d mismatches; %ld pixels changed by the filters)\n", seed, divot, dedither, pct, WB, hi ? (tall ? " (x480)" : "p") : "", errs ? "FAIL" : "PASS", errs, changed);
    return errs != 0;
}
