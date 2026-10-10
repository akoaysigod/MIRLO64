# The RDP

`rtl/rdp` draws the N64's own command stream: what a game's microcode,
running on the RSP ([rsp.md](rsp.md)), leaves in memory for the RDP. It is
in the SoC with the Verilog macro `MIRLO_RSP`, in MRDP's place.

It comes from MRDP ([mrdp.md](mrdp.md)), whose pixel pipeline it keeps: the
N64's combiner and blender, a 16-bit Z buffer, one pixel a clock. What
differs is everything that MRDP had moved to the geometry core -- triangle
setup, texture decoding -- and is here the N64's again. The contract is the
same C model, `lang/c/mrdp/mrdp.c`, built with `MRDP_N64`: for the same
commands the RTL writes the same memory.

## Commands

| op | command | notes |
|---|---|---|
| 0x08-0x0F | triangle | the N64's: edges, then shade, texture, Z coefficients |
| 0x24, 0x25 | texture rectangle, flipped | |
| 0x26-0x28 | sync load / pipe / tile | nothing to do: commands run in order |
| 0x29 | sync full | done when every earlier write is in memory; counted (the DP interrupt) |
| 0x2D | set scissor | |
| 0x2E | set prim depth | |
| 0x2F | set other modes | what is honoured: below |
| 0x30 | load TLUT | |
| 0x32 | set tile size | |
| 0x33 | load block | |
| 0x34 | load tile | |
| 0x35 | set tile | format, size, line, TMEM address, palette, clamp / mirror / mask / shift |
| 0x36 | fill rectangle | |
| 0x37-0x3B | fill, fog, blend, prim, env colour | |
| 0x3C | set combine | both cycles |
| 0x3D-0x3F | texture, Z, colour image | |

Anything else (set key, set convert, the invalid opcodes) is counted and
skipped.

## Triangles

Every triangle is walked as the N64 walks it (the model's `MRDP_CVG`,
`lang/c/mrdp/mrdp.h`; Angrylion's rules), with or without AA_EN:

* **Subscanlines.** Each row is four subscanlines. The edges start at
  `XH & ~1` (and XM, XL) at the first row's top and step `(DxDy >> 2) & ~1`
  each subscanline, one subscanline a clock (`S_AASUB`, five clocks a row).
  Each edge is taken to eighths of a pixel with a sticky bit and clamped to
  the scissor. A subscanline counts if it is between YH and YL and the
  scissor's quarter rows, and its edges have not crossed.
* **Samples.** Each subscanline has two samples in a checkerboard, so a
  pixel has an 8-sample mask. The span is every pixel a subscanline reaches.
  With AA_EN a pixel is drawn if any sample is covered; **without it, only
  if sample 7 is** -- the pixel's top-left corner, so an edge exactly on a
  pixel's left takes it and any fraction after does not.
  The coverage is the mask's count either way
  (a rectangle's is 8). Before this a triangle without AA_EN was MRDP's
  centre rule: a pixel whose centre is inside, whole -- half a pixel from the
  console's (41.8 % of the pixels of 2,939 random small triangles differed).
* **Attributes and Z.** Attributes are the plane's value at the pixel's
  **top-left corner** (x, the row's top) -- the N64's `lineFrac`,
  not the pixel centre: S and T at (x, y) mean
  texel S, not S + 0.5, so a 1:1 mapped texture is not blurred by a half-pixel
  phase. Z moves to the first covered sample (as the N64's offx / offy do),
  from the corner by (0, 2, 4, 6) eighths a pixel; sample 7, the only one
  a pixel without AA_EN has, is at the corner. Shade is not corrected to the
  sample (the console does: it would take about 150 ALMs here; the error
  is under a level on smooth shading).
* **Cost.** The walker adds five clocks a row (measured: +0.1 % cycles on
  144 SF64 and 6 Stadium task dumps); the old centre-rule path (the
  `exm`/`exl` edges, `S_SPAN`) is gone from the RTL. The span reads cost
  some 15-20 % of the RDP's time on Stadium's dumps: an AA surface with
  IM_RD reads its colour span, as the N64 does.

The N64's coefficients go in as they are, with two things of theirs minded:

* XH and XM are the edges at YH's scanline, but **XL is the low edge at YM
  itself**, the middle vertex: it is taken back by YM's quarter rows of its
  slope before the walk. (Read as if at YM's scanline, a shallow low edge is
  off by up to 3/4 of its slope: cracks along every such edge.)
* S and T are s10.5 in the integer half and W is s.15; they are shifted into
  the pipeline's units where the pixels start.

A triangle's attribute words wait in a 64-word staging RAM; each attribute's
value, DaDx and DaDe are put together from their integer and fraction halves
there (about ten cycles an attribute). DaDy is not used, but Z's (a decal's
depth tolerance).

## Textures

TMEM is the N64's 4 KiB, as 2048 16-bit words. The loads copy the texture
image as it is in memory:

* **load block**: a run of texels, in whole 64-bit words;
* **load tile**: a rectangle, each row at the tile's `line`;
* **load TLUT**: 16-bit entries, each written four times over, as the N64
  keeps them (64 bits an entry in the upper half);
* a 32-bit RGBA tile is split as the N64 splits it: R and G in the lower
  half of TMEM, B and A in the same word of the upper half.

The source may start at any byte. An odd row's 64-bit words go into TMEM
with their halves changed places, and are sampled so, as the N64's: a row is
odd by the load's own count (LOAD TILE's rows; LOAD BLOCK's dxt summed over
its words), so a LOAD BLOCK with dxt 0 loads every row as an even one -- and
a game that does that keeps its textures' odd rows swapped in the ROM.

A texel is decoded when it is sampled, by the tile's size and format:

| | 4-bit | 8-bit | 16-bit | 32-bit |
|---|---|---|---|---|
| RGBA | (as I) | (as I) | 5551 | 8888 |
| IA | 3 + 1 | 4 + 4 | 8 + 8 | |
| I | 4 | 8 | | |
| CI, or any with the TLUT on | index, with the tile's palette | index | 5551 (CI 16 is read as RGBA, as on the console) | |

A TLUT entry is RGBA5551 or IA 8 + 8 by the other modes' TLUT type. YUV is
not decoded.

The filter is the N64's three-point one, so a pixel reads three texels at
once: TMEM is kept three times (`rdp_tmem.v`, 15 M10K in all), each copy
with a second small memory of the TLUT's entries for the lookup.

## The pixel pipeline

* **Cycle types**: 1-cycle; 2-cycle (two combiner stages one after the
  other in the pipeline, a pixel a clock -- two clocks when the first
  stage reads TEXEL1, below); copy (the texel; with the alpha compare
  enabled a texel whose alpha bit is 0 is not written -- not the blend
  colour's compare -- and the depth image is neither read nor written,
  whatever the mode word says); fill.
* **Combiner**: the N64's, for colour and alpha: `((A - B) * C + D * 256 +
  0x80) >> 8`, kept to 9 bits (as Angrylion's). "1" is 256 (A and D), C is the plain 0..255 (an 8-bit 255 is
  255/256, so TEXEL0 * SHADE at full brightness is 254). A colour result is
  clamped as the hardware does: bits 8:7 both set -> 0, bit 8 -> 255 (an
  overflow past 383 reads as a small number). The first cycle's result
  (COMBINED) reaches the second as its 9 raw bits (0..383, or -128..-1 above;
  as C a signed 9-bit). An alpha result is 0..256: bit 8, and exactly 255,
  give **256**, which is what CVG_X_ALPHA
  multiplies the coverage by -- a fully covered opaque texel keeps all 8
  samples. (Before, ONE was 255 and C was `c + (c >> 7)`, no rounding: off
  by one in about 40 % of channels.) NOISE, KEY and CONVERT inputs read 0.
  `sim/rdp/cc_unit.sh` checks the RTL's functions against the model's over
  2 x 10^9 operand sets; TEXEL1's samples are 8-bit operands like TEXEL0's.
  * **SHADE is a byte, 0..255** when it enters the combiner: the interpolated
    value is saturated below 0 to 0 and above 255 to 255, a plain positive number
    as A, B, C and D (colour and alpha); the blender's shade alpha is clamped at 255
    after the alpha dither is added. (With 9 bits, 0..511, a lit surface's shade
    overshoots 255 at its edges -- Link's tunic with Navi's light: 270..330 at the
    shoulder -- and OoT's tunic is a 2-cycle `TEXEL0 * SHADE` then `ENV * COMBINED`:
    the first cycle's raw 9 bits reached 256 and over, and as COMBINED, the second
    cycle's C, they are a negative number: black pixels, which the N64 does not
    show.) `test_cvg.c` section 10.
  * **TEXEL1** in the first cycle of a 2-cycle pixel is the next tile's
    sample (tile + 1, wrapping at 8) at the same S, T: OoT's skybox draws
    `(TEXEL1 - TEXEL0) * PRIM_ALPHA + TEXEL0`, the day's two textures
    blended by the prim alpha, and its terrain a second layer. A pixel
    whose first cycle reads it (`two_pass`, `rdp_pix.v`: a select of 2, or
    9 for the C alpha) goes through the texture stages twice, a clock
    apart, on the same TMEM copies and filter -- the first pass the tile,
    the second the next tile (`ts1_*`, the tile's fields registered a
    second time; each stage takes the fields of its pass's tile) -- and
    leaves P9 once with both. `rdp_top` issues such a pixel every second
    clock; the OoT frame at the intro (3158 commands, 282 k pixels) takes
    10 % more RDP clocks. **The second cycle's TEXEL0 is that same next
    tile's sample** (the N64 shifts the first cycle's TEXEL1 into it): a pixel whose
    second cycle reads TEXEL0 (colour or alpha, `cc1_uses_texel0`, `cc1_t0`)
    is a `two_pass` pixel too. Mario Golf's ground is `(TEXEL0 - 0) *
    TEXEL1` (an I8 detail tile 0 by the RGBA16 grass of tile 1), then
    `(COMBINED - TEXEL0) * A + TEXEL0`: with TEXEL0 the detail tile again
    (as it was until this was found) the grass came out a pale grey-green
    with speckle, not green. The second cycle's TEXEL1 stays the tile's own
    sample (the N64's is the next pixel's; no stream reads it there). A
    census of 175 task dumps of OoT, Stadium, Pilotwings, Star Fox 64 and
    Banjo-Kazooie shows no other picture changed. There is no LOD, detail or
    sharpen: one level of the tile.
  * **No texture coefficients**: a triangle whose command has no S, T, W block
    (or a fill rectangle) has S = T = W = 0, and the texture stages run all
    the same: TEXEL0 is the tile's texel (0, 0), whatever the last texture
    bound there left (on the console each primitive starts with
    S, T, W at 0 and the fetch is never skipped). OoT's Link needs it: the tunic
    collar (`gLinkChildCollarNearDL`) is `G_TEXTURE` off with a combiner of
    `TEXEL0 * SHADE`, then `ENV * COMBINED`, and with TEXEL0 = 0 it was
    black -- Link's black neck. (`rdp_pix.v`: S, T, W gated to 0 by `p_tex`,
    the sample never zeroed; `lang/c/mrdp/test_cvg.c` section 8.)
* **Blender**: the N64's. P * A5 + M * (B5 + 1) with 5-bit weights; with B
  as memory alpha, A5 and B5 are the coverages. The result is >> 5 (8 bits,
  wrapping), or -- a blend without FORCE_BL -- divided by A5/4 + B5/4 + 1
  through a reciprocal table (3 DSP blocks). blend_en is
  FORCE_BL, or AA_EN with no coverage overflow and not clearly in front.
  CLR_ON_CVG with no overflow keeps M. 2-cycle mode's first cycle always
  blends. The colour image is read when a select needs it, and for the
  memory's coverage (IM_RD with AA_EN or CVG_X_ALPHA). Fog works through
  it.
* **Coverage**: CVG_X_ALPHA (coverage x alpha), ALPHA_CVG_SEL, the alpha
  dither (ALPHA_DITHER_SEL; "noise" is three bits of a 23-bit LFSR that
  steps for every pixel with coverage), and CVG_DST (clamp, wrap, zap, save).
* **RGB dither** (RGB_DITHER_SEL): the blender's 8 bits go to 5 with a 3-bit
  threshold per channel, the magic square (0) or Bayer matrix (1) at the
  pixel's place on the screen, or **noise** (2, `G_CD_NOISE`: OoT's
  `SETUPDL_0`, its dust and other soft effects): the LFSR's bits [2:0], [5:3]
  and [8:6] as that pixel's alpha dither saw them, carried down the pipe to the output stage. (Before, 2
  used the Bayer matrix: the dust of Epona's gallop was an ordered checker
  screen instead of a random grain.) 3: off.
  * **Storage: the N64's three bits.** The colour image's bit 0 is the
    coverage's top bit. Its two hidden bits (RDRAM's 9th bits) are in a
    plane of their own.
    * **Layout.** 1 MiB at SDRAM 0x0370_0000, just below the RDRAM
    (`layout.h` `LAYOUT_HID`; the layout mark is unchanged). The 16-bit word
    at RDRAM byte A has bits [2k+1:2k] (k = A[4:1]) of the 32-bit word
    A[22:5]. The RDP addresses it with its address's bit 23, which
    `mirlo_mips.sv` maps there.
    * **Pixel spans.** A span reads its groups' words first, into two raw
    buffers. The pixels take their memory coverage there. The write engine
    writes each group back with the span's new bits merged in, the new
    colour buffer's lanes carrying them.
    * **Fills.** A fill rectangle writes each 4-pixel quad's {a, a} by byte
    enables, with no read.
    * **Start.** The runtime starts the plane all ones (`n64rt_boot.c`).
    * **Cost.** On the Stadium and SF64 dumps: 10-13 % more memory words,
    and 5-6 % more RDP cycles.
  * **`MRDP_CVG1`.** The model's first version, one bit, kept for
    reference.
* **Z**: 16-bit, linear (not the N64's compressed format), compare and
  update; primitive depth. The N64's rules use the primitive's dz (all ones
  under the top bit of |DzDx| + |DzDy|, in the stored depth's units):
  * **Opaque**: in front, or -- without a coverage overflow -- within dz
    behind.
  * **Interpenetrating**: the coverage is scaled by how far the pixel is in
    front.
  * **Translucent**: in front.
  * **Decal** (ZMODE_DEC: a shadow on the floor): within dz either way, and
    not over a cleared depth.

  The memory's dz is not kept, so the blender's coverage shifts are 0 with
  Z_CMP.
* **Alpha compare** against the blend colour's alpha, or the alpha dither
  (2-cycle: the first cycle's alpha). A pixel with no coverage left
  (AA_EN, or CVG_X_ALPHA's cut-outs) is not drawn.

## Images

The colour image is the game's: RGBA5551 (the low bit the coverage, above), in the
N64's byte order, two pixels a word with the left one first. Only 16-bit
colour images are drawn to. A row must start on a 4-byte boundary (an image
4-aligned, of even width). The scan-out (`n64_video`, `N64FB`) reads the
same format.

The scan-out does the N64 video interface's gamma correction when the game
has it on (`VIDEO_FRAMEBUFFER.WIDTH` bit 16, which the runtime sets from
the VI mode and `osViSetSpecialFeatures`): each channel's square root, from
a table on its five bits. Star Fox 64 has it on and draws that much darker;
most games turn it off.

With **`MIRLO_VI_AA`** the scan-out has the VI's anti-alias and divot
filters. `rcp_vi` takes them from
VI_CONTROL: AA_MODE 0 or 1, and DIVOT_ENABLE.

* **Fetch.** The picture is fetched a line ahead of the display. Two line
  buffers hold the rows above and below (1024 x 17, 4 M10K).
* **Anti-alias.** A pixel whose coverage is not complete takes six taps:
  (x±1) above and below, (x±2) beside. A tap that is not complete is the
  centre itself. Per channel, the second smallest and second largest of the
  six are widened to take in the centre.
* **The true coverage.** The scan-out reads the hidden plane: a group's word
  before its colour words, and +12.5 % of its reads. Each FIFO word carries
  its pair's four bits; the FIFO is 37 bits wide, 8 more M10K. The N64's
  c + ((min + max - 2c)(7 - cvg) + 4) >> 3 is computed exactly on the
  8-bit channels, and the pads take its top six bits.
* **Divot.** Where any of three neighbours on the line is partial, each
  channel takes the median of the three.
* **Dedither** (VI_CONTROL bit 16). On a complete pixel (with AA off, every
  pixel), each channel adds the signs of its eight ring neighbours against
  the centre (on 5 bits) to its eight bits. This rebuilds the smooth colour
  behind the RDP's dither, so the grain is never seen.
* **8:8:8 to the Pocket.** The filters' eight bits go out as they are.
  The gamma is the N64's 2 sqrt(64 v) on eight bits, from a table.
  `core_top` no longer converts 565 to 888.
* **Edges.** A game's border rows filled with alpha 0 have coverage 0. The
  first one next to the picture is filtered toward it, the N64's own result,
  which a TV's overscan hides. (The macro `MIRLO_VI_AA_NO_CVG0`, which left
  coverage-0 pixels unfiltered, was removed.) The core's video options (interact.json, fields 3 and
  5 of the interact words: `mirlo_regs` hands them to the scan-out) are
  "Crop Vertical" and "Crop Right".
  * **Crop Vertical:** None, 8 or 12 lines blanked at the picture's top and
    bottom (twice as many in the 480-line frame).
  * **Crop Right:** None, 1 or 2 pixels blanked at its right edge (Stadium's
    full-screen overlays end at x = 319.5).
  * **How:** both are relative to the picture (a window too), output as
    black by the scan-out. The frame and its timing are unchanged. The
    default is None, the N64's own output.
* **Delay.** The timing is delayed six pixels to match.

`sim/vi_filter` checks the pads against a model of the filters at 320 x 240, with divot on and off, and
that a 640-wide picture (480 or 240 rows) is reduced into 320 x 240 (the pixel pairs averaged, the first field's rows) with the filters off.

Not done:

* The VI's scaling (a framebuffer is shown 1:1, or 2:1 when 512 wide or
  more).
* Its window.
* The filter on a 640-wide picture shown at half width.

## The depth memory (`MIRLO_Z_PSRAM`, removed from the SoC)

The macros `MIRLO_Z_PSRAM` and `MIRLO_Z_CDRAW` were removed from the RTL (2026-10,
branch own-cleanrtl); `rdp_top` still has the port as `RDP_Z_PORT` / `RDP_Z_CDRAW`
(the bench's builds). What follows is how the SoC used it.

Of the words the RDP moves, about three in ten are depth: on the one SDRAM
they take their turn with the colour image's, with the game CPU's and the
scan-out's. With `MIRLO_Z_PSRAM` (the bench's `RDP_Z_PORT`) the depth
spans go to a memory of their own, the Pocket's two PSRAMs
(`rtl/soc/psram.sv`, [mips.md](mips.md)), while the colour spans keep the
SDRAM:

* `rdp_zmem.v` reads a row's depth span and writes its results back as
  **runs** of consecutive words, which the PSRAM does as one burst each: a
  read run as the row's setup starts, a write run once the row's last pixel
  is through (no run for a row none of whose pixels passed). A word is at
  its address in the N64's memory, so the depth image can be anywhere.
* A game clears its Z buffer by filling it as a colour image: a fill
  (fill mode, whatever the shape) on a colour image whose address is the
  depth image's is a run there too.
* Anything else drawn to such a colour image (a saved Z buffer put back
  with texture rectangles: few games, few scenes) needs `RDP_Z_CDRAW` (it was `MIRLO_Z_CDRAW`)
  as well: it is then `rdp_mem`'s whole, with its port
  turned to the depth memory a word at a time -- right, slow, and some 200
  LUTs. Without it such a primitive is drawn in the N64's memory, where
  nothing looks for it, and the RDP says so: `z_cdraw`, bit 9 of the
  PSRAMs' state, which the runtime logs once.
* What is **not** there: the RDRAM at the depth image's address is not
  written any more. A game that reads its Z buffer with the CPU (a lens
  flare's test against the scene) reads what the RDRAM held, and a texture
  loaded from the depth image's address is loaded from the RDRAM too.

What it gives, on the whole SoC with a task replay (Star Fox 64, Corneria,
the explosion: the heaviest frame at hand; `rsp_replay`, the game CPU idle
to busy):

| | frame's end, SDRAM only | with the depth memory |
|---|---|---|
| the explosion (gfx_142) | 25.8 - 30.2 ms | 22.6 - 25.5 ms |
| an ordinary frame (gfx_060) | 19.0 - 20.8 ms | 18.1 - 19.5 ms |

The RDP's words on the SDRAM go from 686,000 to 483,000 on the first. The
PSRAM's burst costs about eight clocks before its first word, so short
spans gain less than long ones (`tb_rdp`'s `TB_ZOV`, `TB_ZCW`: the same
frame alone takes 977,000 clocks on the SDRAM, 855,000 with the pair of
PSRAMs, 817,000 with a depth memory that cost nothing, 915,000 with one
PSRAM at 16 bits a clock).

## Its registers and command fetch

`rdp_dpc.sv` is the N64's DPC_START, DPC_END, DPC_CURRENT and DPC_STATUS,
written by the microcode through the RSP's COP0. It reads the 64-bit
commands between CURRENT and END from RDRAM, through a port of the SDRAM
arbiter of its own, into the command FIFO. A START written while a run is
still being fetched waits for it, with its END, as on the N64. With XBUS
set the commands are in the RSP's DMEM instead (the first microcodes:
Pilotwings 64's): they are read from a copy of DMEM the RSP keeps for it
(both of DMEM's own ports are the processor's).

The CPU can write START, END and STATUS too (`mirlo_mips.sv`; the runtime forwards a game's stores to `0x2000_0040`), and
what a reader of DPC_STATUS sees is `rcp_dpc_view.sv`'s, not `rdp_dpc.sv`'s own: the console's layout (START_GCLK and PIPE_BUSY
sticky until a SYNC FULL, 0xA8 after a list without one, DMA busy in bit 8, bits 4 and 6 never), and a START written while START
is valid is ignored ([rsp.md](rsp.md)).

## How it is verified

| what | where | checks |
|---|---|---|
| a frame in software | `sim/rdp/rdp_task` | a task dump through the RSP's model and this model, to a PPM; `-v` lists the commands, `rdp_stats.py` what they use |
| RTL vs model, real tasks | `sim/rdp/build_rtl.sh`, `obj_rdp/tb_rdp task...` | the RSP model's commands to both; all 8 MiB identical |
| RTL vs model, random | `tb_rdp -r SEED [N]` | random state, loads of every kind at any alignment, triangles and rectangles in every cycle type |
| the command fetch | `DPC=1 ./build_rtl.sh`, `obj_rdp_dpc/tb_dpc task...` | the RSP's RTL with `rdp_dpc`: the words handed on are the model's, on real tasks and on `gen_xbus_task.py`'s XBUS ones |
| in the SoC | `sim/mips_soc` built with `MIRLO_RSP` (`FB_N64=1` for its frame dumps) | a game on the whole SoC: RSP, command fetch, RDP, scan-out |
| the depth memory | `ZPORT=1 ./build_rtl.sh`, `obj_rdp_z/tb_rdp` (`TB_RAND_CZ=1`: the colour image on the depth image now and then) | the same comparisons with the depth spans on their own port, timed as the PSRAM pair |

A frame's time is the RSP's and the RDP's together, on one SDRAM with the
game CPU and the scan-out:

* `tb_rdp` with `TB_SDRAM=1` gives the RDP the SDRAM's own timing (rows,
  the bus's turns, refresh) instead of a memory that answers at once, and
  `TB_CPU=<percent>` another master beside it.
* `rdp_flow.py` puts the two benches' logs together (`tb_rsp` with
  `DPCT=<file>`: when each command is handed over; `tb_rdp` with
  `TB_CMDT=<file>`: what each takes to draw) and gives the frame's end for
  the two as they are and for either made faster or slower.
* `lang/mips/test/rsp_replay.c` runs a task dump on the whole SoC in
  `sim/mips_soc` (`PRELOAD=<dump>@0x437FFFF0`, `MEMSTAT=1` for the SDRAM's
  use by port): the RSP's and the RDP's times with the scan-out reading and
  the game CPU idle or busy in memory. The dump's task header must not be in
  the SDRAM's last bytes (the boot ROM's stack is there).

`TB_STEP=1` compares after every command and reports the first that differs.

## Area and timing (Cyclone V 5CEBA4, 62.8 MHz)

| | ALMs | M10K | DSP |
|---|---|---|---|
| RDP, standalone | ~4,500 | 33 | 26 |
| without the blender | ~4,300 | 31 | 20 |
| the SoC with the RSP (8 lanes) and the RDP | ~17,850 (97 %) | 184 | 41 |

In the SoC the RDP is ~4,800 ALMs and the game CPU ~3,800 (~4,350 with its
TLB, which is left out). Three fitter seeds close timing.

It is smaller than MRDP was (~5,800 in the design): the triangle setup
engine and its microcode are gone, the N64's coefficients needing none.
