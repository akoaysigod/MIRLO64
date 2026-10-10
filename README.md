# Mirlo

Mirlo is a MIPS computer for the [Analogue Pocket](https://www.analogue.co/pocket), built on the openFPGA platform, with hardware for 3D graphics in the style of the Nintendo 64. It is a system-on-chip for the Pocket's Cyclone V FPGA, written in SystemVerilog, plus the SDK to write software for it in C.

The design fills most of a Cyclone V 5CEBA4F23C8, so timing margins are thin (see [Building the hardware](#building-the-hardware)). The project is experimental.

## What is inside

Three MIPS cores, a rasterizer and the Pocket's peripherals:

* **Game CPU**: a 32-bit VR4300-class MIPS (`rtl/mips/mips_core.sv`), big-endian as the N64, with a single-precision FPU, 16 KiB I-cache and 8 KiB D-cache, running from the Pocket's 64 MiB SDRAM. Your program runs here.
* **Geometry core**: a second MIPS (`rtl/mips/mips_geom.sv`, MIPS32's integer subset) with a fixed-point vector unit as its CFU (custom function unit). It walks the display lists the game CPU writes: it transforms, lights and clips vertices, and sets up triangles. See [docs/geometry_core.md](docs/geometry_core.md).
* **MRDP**: an N64-style rasterizer. It takes the RDP's command format and provides:
  * TMEM and tiles, with a 3-point filter;
  * the N64 colour combiner and blender;
  * a 16-bit Z buffer;
  * RGB565 framebuffers.

  See [docs/mrdp.md](docs/mrdp.md).
* **Audio core**: a third MIPS (`rtl/mips/mips_lite.sv`) with a DSP CFU for mixing and ADPCM, feeding the Pocket's 48 kHz audio. Its programs are loaded by the game CPU at run time (`lang/c/audio`).
* **The Pocket's peripherals**:
  * input: all four controllers, including analog sticks and triggers;
  * video: 268 × 240 (fills the Pocket's screen) or 320 × 240;
  * vblank and a frame counter;
  * audio;
  * file access through the APF bridge (data slots, saves);
  * core settings through `interact.json`;
  * a JTAG UART for logs and program upload.

The register reference is [docs/control.md](docs/control.md); the map itself is `tools/mirlo_regs.py`, which generates both the hardware's decode and the firmware's `csr.h`.

## Repository layout

| directory | contents |
|---|---|
| `rtl/` | the hardware in SystemVerilog and Verilog: the MIPS cores (`mips/`), the MRDP rasterizer (`mrdp/`), the N64-style RDP and RSP (`rdp/`, `rsp/`), the geometry core and vector unit (`geom/`, `vpu/`), audio, video, and the SoC with SDRAM and Pocket peripherals (`soc/`) |
| `target/pocket/`, `platform/pocket/` | the openFPGA glue: top level, clocks, APF bridge |
| `projects/` | the Quartus project (`mirlo.qpf`, `mirlo.qsf`) |
| `lang/` | the SDK: `mips/` (toolchain flags, C runtime, boot ROM), `c/` (3D SDK, geometry-core firmware, examples), `n64rt/` (N64 compatibility runtime) |
| `sim/` | Verilator testbenches |
| `tools/` | register-map generator, packaging, JTAG upload and test tools |
| `pkg/pocket/` | the files that go on the SD card: core definition, platform, save-type database |
| `docs/` | design notes and the register reference |

## Installing on the Pocket

1. Download `tortuga.Mirlo_<version>.zip` from the [releases](https://github.com/tortugaveloz/MIRLO64/releases).
2. Unzip it onto the **root** of the Pocket's SD card, merging with the folders already there. It adds only Mirlo's own files:

   ```
   Assets/mirlo/common/          n64rt.sys (the runtime) and n64savedb.txt; where Mirlo's games go
   Cores/tortuga.Mirlo/          the core: mirlo.rev (the bitstream), core.json, ...
   Platforms/mirlo.json          the platform, under Console
   Platforms/_images/mirlo.bin   its image
   ```

3. Put the games (`.bin` files) in `Assets/mirlo/common/`. Mirlo needs one to start: the release carries none; build one with the SDK (see [Writing software](#writing-software)) or get a Mirlo game such as [Super Mirlo 64](https://github.com/tortugaveloz/supermirlo64).
4. On the Pocket: **openFPGA → Console → Mirlo**, then pick a game.

Saves (`.sav`) appear in `Saves/mirlo/common/` when you quit a game. To update the core, unzip a newer release the same way; it replaces the files in `Cores/tortuga.Mirlo/`.

## Using the core

### Games

When you launch Mirlo, the Pocket asks for a game: any `.bin` in `Assets/mirlo/common/`.
* The Pocket itself loads the file into SDRAM, with its own loading bar.
* The boot ROM then jumps to the program at `0x4000_0000`.

To restart a program, quit the core and launch it again.

A game file is a plain program image, or one packed with `tools/make_mirlo_game.py`. A packed file adds:
* data blocks the program reads itself;
* a footer with the video mode (268 or 320 wide).

### Over JTAG

With a USB Blaster on the Pocket's JTAG port you can load a bitstream and a program without touching the SD card:

```bash
tools/jtag/jtag_program_and_run.sh <bitstream.sof> <program.bin> <log> [seconds]
```

This programs the FPGA with `quartus_pgm`, then uploads the program through the boot ROM's serial boot (SFL protocol), over the JTAG UART, and logs its output. It needs:
* `openocd`;
* Quartus' `quartus_pgm` (set `QUARTUS_PGM` if it is not in `~/altera/25.1std`).

Launch the core from the Pocket's menu once first, so that its bridge is up. The upload runs at about 3 KB/s, so it suits small programs; large games belong on the SD card. `tools/jtag/jtag_uart_relay.py` alone gives you the UART on a PTY.

Use a genuine USB Blaster: clones have damaged Pockets.

### Core settings

`interact.json` offers:
* **JTAG UART**: route the UART over JTAG.
* **Crop Vertical** (None, 8 or 12 lines; default 8) and **Crop Right** (None or 1 column; default 1 column): blank the picture's edges.
* **Controller 1 pak** (None, Controller Pak or Transfer Pak; there is no Rumble Pak: the Pocket and its dock give a core nothing to vibrate). The size of a cartridge's EEPROM is not a setting: the save-type database (`Assets/mirlo/common/n64savedb.txt`; a game not in it, or no file, is 4 Kbit) decides.
* **Stick** (D-pad, L stick or R stick) and **D-pad** (None, D-pad, L stick or R stick), **Z button**, **L button**, **R button** (each None, L, R, ZL or ZR), **C buttons** (None: the face buttons only; Right stick: also the right analog stick), **Start button** (None, Start or Select+Start), **Modifier** (None, L, R, ZL or ZR: held, it turns the face buttons into the C buttons): the N64 controller's layout (`docs/mips.md`, "The core's settings for the controller").

## Writing software

Guides and examples: [lang/README.md](lang/README.md).

* **C**: `lang/c/examples/*`. `lang/c/game` is the 3D SDK: frame loop, display lists and the geometry-core hand-off.
* `lang/mips` is the SDK's base: the toolchain flags (`mips.mk`), picolibc and compiler-rt built for the cores (`lib.mk`), the C runtime and linker files, and `program.mk` for a game-CPU program.

The game CPU's code is `-march=vr4300 -mabi=32 -EB -msingle-float` (no 64-bit instructions; doubles in software). It is big-endian, as the N64, and an N64 game's code can run on it unmodified: see [docs/mips.md](docs/mips.md#running-n64-code). Any MIPS GCC with its binutils works; `lang/mips/mips.mk` names them.

Register addresses can change between hardware revisions: take them from the generated `csr.h`, never from memory.

## Building the hardware

### Tools

* **A MIPS GCC and binutils** (e.g. Ubuntu's `gcc-mips-linux-gnu`; the flags choose the byte order and ABI). Set `MIPS_CC` and `MIPS_BIN` for `lang/mips/mips.mk`.
* **Python 3**, with **meson** and **ninja** to build picolibc.
* **Quartus Prime Lite** with Cyclone V support (tested with 25.1std).
* **Verilator** for the simulations under `sim/`.

### Steps

```bash
git clone --recursive https://github.com/tortugaveloz/MIRLO64.git
cd MIRLO64

python3 tools/mirlo_regs.py                        # the register map: rtl/soc/mirlo_regs.svh, csr.h
(cd lang/mips && make -f lib.mk VARIANT=game && make -f lib.mk VARIANT=lite)   # picolibc, compiler-rt
(cd lang/mips/boot && make)                        # the game CPU's boot ROM
(cd lang/c/geom && make)                           # the geometry core's firmware
python3 tools/mips_inits.py                        # both images, for the bitstream
python3 sim/rsp/make_div_rom.py rtl/rsp/rsp_div_rom.hex   # the RSP's reciprocal ROM (not in git)

cd projects
quartus_sh --flow compile mirlo
python3 ../tools/package_bitstream.py output_files/mirlo.rbf mirlo.rev   # bit-reversed, as the Pocket wants it

(cd ../lang/n64rt && make)                         # the runtime: build/n64rt.bin, released as n64rt.sys
cp ../lang/n64rt/build/n64rt.bin n64rt.sys
python3 ../tools/make_release.py mirlo.rev n64rt.sys tortuga.Mirlo_0.1.zip   # the SD-card zip
```

`n64rt.sys` is the program the core runs as its data slot 3 (`Assets/mirlo/common/n64rt.sys`): it loads the game (a `.z64`) from slot 0 and runs it with the game's own libultra. It needs the libraries, the register map and the geometry core's firmware of the first lines above, and no bitstream: `lang/n64rt/Makefile` builds it (about 60 KB) from `lang/n64rt/loader/main.c` and the runtime in `lang/n64rt`.

The boot ROM and the geometry core's firmware are part of the bitstream: after changing either, or the register map, repeat the sequence (see [docs/geometry_core.md](docs/geometry_core.md)).

The design fills most of the device and its timing margins are thin. If a compile misses timing, try a few `SEED` values in `projects/mirlo.qsf`.

### Simulation and tests

| what | where |
|---|---|
| the whole SoC (CPU tests, the cube demo, the audio core, games) | `sim/mips_soc` (`./build.sh soc`, then `obj_soc/tb_soc <program.bin>`) |
| CPU and C library self-tests | `lang/mips/test` |
| MRDP C model vs RTL | `sim/mrdp` |
| MRDP setup microcode | `tools/mrdp_ucode.py test 2000` |
| geometry core RTL vs host build | `sim/geom_full/check_host_vs_rtl.sh` |
| vector unit | `sim/vpu` |
| audio DSP unit | `sim/audio` |
| SDRAM controller + PHY vs a chip model | `sim/mips_soc` (`./build.sh sdr`) |
| host unit tests | `make hosttest` in `lang/c/geom` |

## Other documents

* [docs/resolution.md](docs/resolution.md): the two video modes, and how to add another.

## License

Mirlo is released under the [Apache License 2.0](LICENSE). It includes and builds on third-party work under its own licenses: see [NOTICE](NOTICE).
