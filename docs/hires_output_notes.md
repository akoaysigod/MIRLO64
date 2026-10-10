# 640x480 output: what was tried, and where it is kept

Decision (owner, 2026-10-09): the Pocket output is ALWAYS 320x240 for now (branch `own-lores`, merged into
`integ` at b9aeace; sources 512 wide or more are shown at half width, 2x1 average, first field only for 480
rows, VI filters bypassed for them). To be reconsidered later.

## What existed before (kept in tags, not in `integ`)
- `archive/hires-own-hires` (own-hires 1f88ff8): "option A", the interlaced 640x480 output as the console sends it:
  a field of 240 rows per frame at 59.8 Hz, the Pocket's scaler-slot word after each line's DE, feature bits during
  VS ([3] last field, [2] odd, [1] interlaced), slot announced a frame ahead, one black frame at each size change.
- `archive/hires-interlaced` (own-st3 97da582): own-hires merged with integ, tb_vi updated to model fields, and the
  runtime option `N64RT_HIRES_OPT` (vi_opt, WIDTH bits 21:18): 1 announce the slot as 640x240, 2 last-field
  polarity, 4 no feature bits (each field progressive), 8 swap which field is odd.
- `archive/hires-prog60` (hires-b-prog60 04e5249): the fallback, progressive 640x480 at 59.8 Hz on a 25 MHz clock
  (needs an RTL fit).

## Pocket results (Pokemon Stadium, "Game Pak Check", 2026-10-09)
- Integ seed 41 with the default option: a picture appears (before: none) but every other row is black and the picture
  splits into vertical bands with a different row phase (photo: ~/mirlo-wt-archive/hires/stadium_gamepak_check_photo.jpg).
- Loader variants with HIRES_OPT 1, 2, 3, 4, 5, 8, 9, 10, 11 (n64rt-hires-opt<N>.sys in ~/pocket-bitstreams, built from
  integ 7e0d969 with the loader repo branch tlbmon; note their size, 56,488 bytes, differs from the shipped loader's
  56,456: the default option was never checked to reproduce it): none showed the whole screen; some showed one part
  right, others another. The replicas also used a video.json without the 640x240 mode, so options 1/3/5/9/11 were
  not a fair test.
- Conclusion at the time: the problem is probably not the field signalling. Open questions: whether the Pocket weaves
  interlaced slots at all, which field it calls odd, what height it expects for an interlaced slot (640x480 or
  640x240), and whether the vertical bands are the camera's rolling shutter. The Analogue docs do not say.

## Cost of keeping hi-res (measured on `own-lores` vs fe65db8, 8 seeds)
About 2 M10K (275 -> 273 of 308), 16-30 registers and 0-30 ALMs: invisible next to the +-100 ALM placement noise.
No change in timing closure in a statistically meaningful way. The argument for removing it was simplicity and
robustness, not area.

## If it is taken up again
Start from `archive/hires-interlaced`, get an HDMI capture (dock) of the interlaced slot instead of photos,
and compare with how other openFPGA cores (e.g. the Analogue-supported N64/PSX cores) declare interlaced slots.
