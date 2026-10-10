# Resolution

(The section "Two modes" below describes the earlier cores, with a 268 x 240 frame; the N64 core's frames are the next section's.)

## Two modes, chosen by the game file (Mirlo)

The core runs 268 x 240 (the default: fills the Pocket's screen, the N64's
320-wide frame cropped 26 px a side, 2D moved in) or the N64's own 320 x 240
(4:3, nothing cropped or moved). The game file's header picks: byte 26 of the
MIRLODATA footer, 0 = 268, 1 = 320 (`tools/make_mirlo_game.py --video 320`).
Older files read 0. There is no menu override: a game runs in the mode its
file asks for.

How it switches, at boot (lang/c/game/frame.c `frame_set_video`, called by
the program with the game file header's choice before `frame_init()`):
the scan-out's `vtg_mode` register (rtl/soc/n64_video.sv: two constant
timing sets, 340 x 280 and 360 x 264 -- since replaced by the one 320 x 240 frame, see the section below,
~60 Hz, which also set the frame's length), the Pocket's scaler slot
(sent on the RGB bus during blanking; video.json slot 0 = 268, slot 1 = 320),
the clears' width and the geom core's screen edges (GDL_FBSIZE).

The older, manual procedure below (a new pixel clock and timings) still
applies for any other resolution.

## Frame size (N64 games: always 320 x 240)

`rtl/soc/n64_video.sv` sends the Pocket ONE frame, the scaler slot 0 of
`video.json`:

| slot | frame       | clocks a pixel | clocks a line | lines a field | rate          |
|------|-------------|----------------|---------------|---------------|---------------|
| 0    | 320 x 240   | 2              | 790 (nine: 788) | 264         | 59.940 Hz     |
| 0    | 320 x 240   | 2              | 800 (fifty: 792) | 313        | 50.000 Hz (PAL) |

On the pixel clock `clk_vid` = sys / 5 (12.5 MHz, `mf_pllbase`'s outputs 3 and
4). A pixel lasts two clocks and `video_skip` marks the second, so the Pocket's
scaler sees 320 pixels a line; a field (790 clocks x 264 lines, less 18 clocks
in nine vblank lines = 208,542 clk_vid clocks = 1,042,710 system clocks) is the
N64's 59.940 Hz (V_SYNC 0x20D, H_SYNC 0xC15 of every NTSC libultra mode; until
2026-10-09 it was 792 x 264 = 1,045,440, 59.783 Hz), so the VI's field timing does
not move with the size, nor the scan-out's reads (240 rows of 320 words a field: 18.4 MB/s,
through the 64 KiB FIFO).

**PAL (2026-10-10, branch `own-pal`).** The same slot and pixel clock, with a PAL field: 313 lines of 800 clk_vid clocks (400
pixels, 64 us: the console's PAL line is 3178 clocks of 49.6565 MHz) and fifty of them (lines 253..302, in vblank) four pixels
(8 clocks) shorter: 263 x 800 + 50 x 792 = 250,000 clk_vid clocks = **1,250,000 system clocks = exactly 50.000 Hz**
(V_SYNC 0x271, H_SYNC 0xC69 of libultra's PAL modes: 312.5 lines of 3178 clocks = 993,125 VI clocks of 49.6565 MHz = 20.000 ms).
The picture is the same 240 lines in the same slot: libultra's PAL modes show 237 lines like the NTSC ones (V_START 95..569
half-lines against 37..511), so no letterbox or crop is needed for a lo-res or an interlaced hi-res mode; a picture taller
than 240 rows (a PAL game's own 264- or 288-row mode) is cut at its 240th row, as an NTSC one is. Which system is in use:
`VIDEO_FRAMEBUFFER_WIDTH` bit 18 (the runtime writes it from the ROM's region, `n64rt_tv`) until the game writes a V_SYNC that
names one -- 525 lines (512..575) NTSC, 625 (576..703) PAL; anything else (0, noise) changes nothing --, then that. The system is
taken at the scan-out's frame boundary and applied where the field ends, so no field is cut short (tb_vi checks the 8
transitions of a sequence line by line); bit 19 of the same register reads the system in use. MPAL is NTSC here (525 lines, 59.9
Hz). The Pocket's output stays 12.5 MHz / 2 clocks a pixel / 320 x 240: only the number of lines (313) and the line length
(400) change, and the frame rate follows (50 Hz is inside the 47..61 Hz the scaler takes; not yet seen on a Pocket).

(The 268 x 240 frame and `VTG_MODE` of the earlier
cores are gone: the register stays, it does nothing.) Outside DE the pads are 0:
there is no scaler-slot word and no frame feature bits.

**A hi-res picture is shown in that frame, reduced without a buffer.** An earlier branch
(`own-st3`, `integ` up to fe65db8) sent a 640 x 480 interlaced picture as the
N64 does, as a 640 x 240 field each VI field with the Pocket's frame feature
bits (slots 1 and 2 of `video.json`, `VIDEO_FRAMEBUFFER_WIDTH` bits 21:18, the
runtime's `N64RT_HIRES_OPT`); it never displayed correctly on the Pocket for
Pokemon Stadium's Game Pak Check, and was dropped. Now:

- VI_WIDTH 512 or more (a 640-wide framebuffer) is shown at half its width: the
  scan-out reads every word of a row (two pixels) and shows their average, per
  channel (a + b + 1) >> 1 (a 2 x 1 box on what the word has: ~31 ALUTs; showing
  the left pixel alone, point sampling, was the first try and costs none);
- one of 480 rows (interlaced: the VI's Y scale steps two rows a line, or WIDTH is 1280 -- two rows' pitch -- with Y scale 1.0, Donkey Kong 64's; the
  runtime sets `WIDTH` bit 17) shows its first field alone, the buffer's even
  rows (point sampled vertically) (the reads skip the odd ones; the first field's origin is the lower of
  the two origins libultra writes a row apart, which `rcp_vi.sv` keeps); its
  window is a field's rows, half those of the mode;
- the VI's filters (AA, divot, dither: MIRLO_VI_AA) are off for it: their taps
  would be the neighbours of the full-size picture, and their line buffers
  are one frame's width, 320 (512 words of 19 bits);
- the cost: Stadium's menus and any 640-wide mode lose half their horizontal
  and (480-row ones) half their vertical resolution, and the 480-row ones show
  one field of the weave, with no deinterlacing.

The runtime picks tall from the VI mode (`n64rt_io.c` `vi_width_gamma`):
interlaced when the rows the mode shows -- its window's lines times
VI_Y_SCALE's step -- come to more than 300 (libultra's HPF/HAF: 237 lines a
field at a step of 2).

A framebuffer narrower than 320 (Banjo-Kazooie's 292 x 213) is shown 1:1 with
black around it, inside the 320 x 240 frame.

## Changing the resolution by hand

If you don't like the default resolution selected by the core, you can change it to anything supported by the Analogue Pocket. Remember openFPGA is limited to 800x720, so you can't choose a resolution outside of those bounds.

1. Find your desired vertical and horizontal counts. [See agg23's wiki](https://github.com/agg23/analogue-pocket-utils/wiki/Video) for additional information about how to select counts, but briefly, visit https://tomverbeure.github.io/video_timings_calculator and enter your desired resolution. It will show you the "conforms to protocol" options that you can select between, but you are not required to use one of these for the Pocket; it is just an example. If you choose an unusual resolution (like the default 266x240), there won't be a standard to base the calculations off of, so the tool will estimate what it would be.  
Note that you want all of your system clocks (CPU, and 2x for SDRAM) to be integer multiples of this clock, and to pass timing you probably need the CPU clock to be < 60MHz. For example, you could choose counts that require a 10MHz clock, which would either be a 5x or 6x multiple (50MHz or 60MHz) for the CPU clock.

2. In Quartus, open the `mf_pllbase` megafunction on the left side of the screen. This will open the tool where you can manipulate the clocks for the core. The last two clocks should be the video pixel clock that you chose, the first is your CPU clock (remember to keep it an integer multiple of the video clock), and the second and third clocks are your SDRAM clock (remember to keep it 2x the CPU clock). Enter those values and click finish.

3. Edit the clock constants: `CLOCK_HZ` in `tools/mirlo_regs.py` (then run it) and `SYS_HZ` in `target/pocket/core_top.sv`. Put your counts into `rtl/soc/n64_video.sv` (its two timing sets and the frame's word counts).

4. Update the Pocket `video.json` to your new resolution. Without it, the Pocket won't know what resolution you want to display.

5. Rebuild the firmware that has the clock compiled in (see the top-level README's build steps).

6. Build the Quartus project by pressing "Build".

7. Reverse the produced bitstream (`*.rbf`). See https://www.analogue.co/developer/docs/packaging-a-core#fpga-bitstream

8. Update your clock speed constants in your software code, if any.

9. The core should now be ready to use at the new resolution.
