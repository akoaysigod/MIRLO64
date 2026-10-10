# The RSP

`rtl/rsp` is the N64's RSP: a game's own graphics and audio microcode runs
on it, as on the console. It is written for Mirlo from the instruction
set's public description; no other implementation's code is in it. In the
SoC it takes the place of the geometry and audio cores (Verilog macro
`MIRLO_RSP`, `rtl/soc/mirlo_mips.sv`), and its graphics commands go to the
RDP (`rtl/rdp`, [rdp.md](rdp.md)).

## What it implements

* **Scalar unit** (`rsp_su.sv`): the RSP's MIPS subset, five stages, a
  12-bit PC, delay slots; no exceptions, no multiply or divide, no 64-bit
  operations.
* **COP0**: the SP registers (DMA addresses and lengths, STATUS, the
  semaphore) and the DP command registers, with the N64's set / clear
  write bits. BREAK halts and raises the SP interrupt if enabled.
* **DMA** between RDRAM and IMEM / DMEM: length, count and skip as the
  N64's, in 64-bit words; it runs behind the program, which waits on
  `DMA_BUSY`. A transfer out of IMEM reads IMEM (its second port, a word a
  cycle): the boot microcode of a CIC-6105 game writes eight bytes of
  itself to RDRAM.
* **Vector unit** (`rsp_vu.sv`): 32 registers of eight 16-bit elements, the
  48-bit accumulator, VCO / VCC / VCE, element broadcast.
  * the multiplies (VMULF, VMULU, VMUDL/M/N/H, VMACF, VMACU, VMADL/M/N/H);
  * add / subtract with carry, VABS, VSAR, the logic operations;
  * the compares and VMRG, VCL, VCH, VCR;
  * the reciprocals and square roots (VRCP, VRCPL, VRCPH, VRSQ, VRSQL,
    VRSQH) from the N64's two ROMs (`rsp_div_rom.hex`,
    `sim/rsp/make_div_rom.py`), VMOV, VNOP;
  * MFC2, MTC2, CFC2, CTC2;
  * loads LBV, LSV, LLV, LDV, LQV, LRV, LPV, LUV, LTV and stores SBV, SSV,
    SLV, SDV, SQV, SRV, SPV, SUV, STV, at any address and element.
* **Not implemented** (the `unimpl` output is set if one runs): LHV, LFV,
  LWV, SHV, SFV, SWV, VRNDP, VRNDN, VMULQ, VMACQ and the reserved COP2
  functions. `sim/rsp/rom_ucode_scan.py` finds the microcode in a ROM
  (also inside Yaz0 / Yay0 / MIO0 blocks) and lists its vector instructions:
  none of the games looked at so far uses any of these.

## How it is built

The area budget is what the two cores it replaces took, so it is narrower
than the N64's:

* `LANES` is 4 or 8 (the SoC's `MIRLO_RSP_LANES`): with four, a vector
  operation's eight elements go through the lanes in two beats. A lane is
  one 16 x 16 multiplier, one adder and the 48-bit accumulate.
* Vector operations run in a two-stage pipeline (select, execute) behind
  the scalar pipeline; the scalar unit goes on while one finishes, and
  waits only when it needs the result or the vector unit is still busy.
* The vector registers are LUT RAM (`rsp_lutram.v`), three copies: two
  addressed straight by the instruction's vs and vt (the multipliers'
  operands), one for everything else.
* With eight lanes VCL, VCH and VCR take a cycle more (the adder's flags in
  one, the select by them in the next), and a reciprocal one for its ROM.
* DMEM is 16 bytes wide, as the N64's: four banks of M10K, a word of each
  16-byte line in each, so one access takes the 16 bytes from any word's
  start (each bank at its own row). One access a cycle -- the DMA's, the
  vector unit's or the scalar unit's -- and every vector load and store is
  one: what each moves lies in those 16 bytes (LQV / SQV to the block's end,
  LRV / SRV the block's start, a packed load the 16 bytes of its 8-byte
  block, LDV / SDV from any byte). A load's data comes back rotated to the
  register's bytes (one 16-byte rotation by address minus element, which
  also gives the scalar unit its 1 to 4 bytes), a store's goes out rotated
  the other way; enables pick the bytes. LTV and STV go a byte a cycle.
* The DMA moves a 32-bit word a cycle each way (what the SDRAM gives).

With four lanes a computation's second pass runs behind the next instruction:
its operands are taken with the first pass's and kept, so the instruction is
in the scalar unit's E stage for one cycle, and what follows waits only for
the lanes (another computation) or for the register being written. A load of
one chunk goes ahead under it too; the registers have one write port, so the
computation's write is then a cycle late, from the kept results. `MULW` adds
four multipliers and accumulator adders to the four lanes: a multiply is one
pass on eight lanes and only the other operations take two.

| | ALMs | M10K | DSP | Star Fox 64, a heavy graphics task | its audio task | Pokemon Stadium, graphics | Super Mario 64, graphics |
|---|---|---|---|---|---|---|---|
| 4 lanes | 4,594 | 20 | 4 | 20.0 ms (1.53 cycles an instruction) | 7.6 ms (1.33) | 14.9 ms (1.33) | 12.9 ms (1.45) |
| 4 lanes, `MULW` | 5,092 | 20 | 8 | 18.4 ms (1.41) | 7.2 ms (1.26) | 14.7 ms (1.31) | 11.4 ms (1.29) |
| 8 lanes | 5,595 | 20 | 8 | 17.0 ms (1.31) | 7.0 ms (1.22) | 14.2 ms (1.27) | 10.8 ms (1.21) |

(Cyclone V 5CEBA4, standalone fit, 62.8 MHz; the tasks are of 820,000,
360,000, 704,000 and 557,000 instructions, with a memory that answers at
once. Star Fox 64's microcode runs its computations in long rows, where four
lanes cannot hide the second pass; Pokemon Stadium's has them one at a time.)

In the SoC (`MIRLO_RSP_LANES`, and `MIRLO_RSP_MULW` with 4) the whole design
is 18,182 ALMs with eight lanes and 17,314 with four and `MULW`. That Star
Fox frame on the whole SoC, with the RDP drawing it and the game CPU busy in
memory (`rsp_replay`), ends at 30.1 ms with eight lanes, 31.5 ms with four
and `MULW`, 32.8 ms with four.

(Those figures are from before DMEM was 16 bytes wide.) The 16-byte DMEM
takes every vector load and store to one cycle: with four lanes and `MULW`
the audio tasks run 4-8 % fewer cycles and the graphics ones 0.1-1 %. Against
a reference RSP implementation on the same task dumps (memory latency 8 on both): Pokemon
Stadium audio x1.08 (was x1.13), Star Fox 64 audio x1.07 (x1.16), Super Mario
64 audio x0.87 (x0.95), graphics x0.94-1.01. What is left of the audio gap is
the DMA: ours moves 4 bytes a cycle, all the SDRAM gives, the N64's RDRAM 8.
In the SoC it cost ~90 ALMs in the RSP (+93 / +155 for the whole design,
seeds 34 / 3) and saved 4 RAM blocks, and the scalar unit's paths into DMEM
are shorter than before (the RSP's clock +0.36 / +0.55 ns).

`PERF=<file>` on `tb_rsp` writes every instruction's cycles and
`perf_kinds.py` sums them by kind, against another such log if given.

## In the SoC

* The game CPU reaches the SP registers at `0x2000_0000` (`0x00` MEM_ADDR,
  `0x04` DRAM_ADDR, `0x08` RD_LEN, `0x0C` WR_LEN, `0x10` STATUS, `0x14`
  DMA_FULL, `0x18` DMA_BUSY, `0x1C` SEMAPHORE, `0x20` PC) and the DP command
  registers at `0x2000_0040`.
* The DMA is a port of the SDRAM arbiter; the N64's RDRAM is SDRAM
  `0x4380_0000`.
* The SP DMA registers are double, as the console's: a write to SP_MEM_ADDR / SP_DRAM_ADDR while a
  DMA runs is the next one's (the running one's pointers go on and are what a read gives), a host RD_LEN / WR_LEN is taken
  into pending registers (SP_DMA_FULL, STATUS bit 3; STATUS bit 2 and SP_DMA_BUSY read busy-or-full) and starts when the running
  one is done; a third command while FULL replaces the pending one. The microcode's own MTC0 still waits in E for its
  transfer. (Before 2026-10-09 a write while busy corrupted the running DMA and the second command was dropped.)
  A DMEM transfer shares DMEM's port with the processor: the DMA has it (a beat of a transfer in, the whole transfer of one out)
  and a scalar or vector load or store waits (`m_stall`; each beat of LTV / STV) instead of being lost -- 28 of 200 were
  lost in `sim/rsp/tb_dma`'s loop before. The words of a transfer in are written a cycle after they arrive (so the stall is
  known a cycle ahead, from a register): a DMA takes one cycle more (+0.11 % on a graphics task, +0.14 % on an audio one).
* SP_SEMAPHORE is taken by a CPU read too (0, then 1, 1; a write clears it), as by the microcode's MFC0.
* The CPU can write DPC_START, DPC_END and DPC_STATUS (the monitor forwards a game's store to `0x2000_0040`, `0x44`, `0x4C`):
  `mirlo_mips.sv` merges them with the microcode's strobes (the CPU's wait for a cycle the microcode is not writing a
  register: `h_ready`). `rtl/soc/rcp_dpc_view.sv` makes the register the console's: a START written while START_VALID is
  set is ignored, and DPC_STATUS reads `{start valid [10], end valid [9], DMA busy [8], CBUF ready [7], 0, PIPE busy [5], 0,
  START_GCLK [3], flush, freeze, xbus}`; START_GCLK and PIPE_BUSY are set by a write of END (not frozen) and by the unfreeze and
  stay until the RDP completes a SYNC FULL (so after a list without one the register reads 0xA8, as n64-systemtest waits
  for). `lang/mips/test/rcpcpu_test.c` checks it all on the SoC; `sim/rsp/tb_dma` the DMA part on the RSP alone.
* The RDP's XBUS reads (its commands taken from DMEM) are DMEM's second
  port (the banks' port B); the processor has port A.
* The game CPU has no TLB of the VR4300's kind (`rtl/soc/mips_sys.sv`; the
  macro `MIRLO_TLB` that put it in, ~550 ALMs, was removed). An N64 game's mapped addresses
  are those below `0x2000_0000`, and the runtime keeps what the game maps
  (`osMapTLB`). With `MIRLO_TLB_RAM` the CPU has translation tables in block
  RAM that the runtime fills a page at a time, on the TLB's exception
  ([mips.md](mips.md)); without it a load or store to a mapped page traps and
  is done on the game's behalf (`n64rt_os.c`), which is for what a game maps
  briefly -- Super Mario 64 maps `0x0400_0000` while it builds its title
  screen's head (200,000 accesses, once) -- and code cannot run from a mapped
  page.
* `lang/n64rt/n64rt_rsp.c` (a port built with `RSP=1`) starts a task as
  libultra's `osSpTaskLoad` does -- the header, its pointers physical, to
  DMEM `0xFC0` and the boot microcode to IMEM by DMA, PC 0, halt cleared --
  and takes its end from STATUS. The RSP's SP interrupt (a task's BREAK) is
  the game CPU's IP2, so the runtime sees a task end as it does; a timer's
  poll every 0.5 ms stands behind it. A graphics task yields and resumes as
  on the N64. A port may have a yield held back (`n64rt_yield_wait_ms`): a
  game yields its graphics task for an audio task, and the frame then ends
  an audio task later, because what the RDP has most work with comes at the
  graphics task's end. Held back, a graphics task that ends within the wait
  is not stopped (the game sees its yield lose the race, which its
  scheduler allows for) and the audio task runs while the RDP draws. Only
  for a game whose audio is a buffer ahead of the AI. With no audio core the game CPU feeds the Pocket's audio FIFO
  itself from the game's AI buffers, resampled to 48 kHz.

## How it is verified

| what | where | checks |
|---|---|---|
| reference model | `sim/rsp/rsp.c` (`rsp_task` runs task dumps) | the model against an independent RSP in simulation, state after every instruction, on real tasks and random programs |
| RTL vs model | `sim/rsp/build_rtl.sh`, `obj_rsp/tb_rsp` | lockstep: registers, flags and memory compared at every instruction, with random memory latency; real tasks and `gen_rand_task.py` programs, 4 lanes, 4 with `MULW` and 8. (Where a computation's late register write lands under the next one's first pass, the registers are compared and the accumulator and flags with the instruction after.) |
| what games use | `sim/rsp/rom_ucode_scan.py <rom>` | every vector instruction of a ROM's microcode against what is implemented |
| in the SoC | `sim/mips_soc` built with `MIRLO_RSP` | a game on the whole SoC |

Task dumps come from a port built with `RSPDUMP=1` run in `sim/mips_soc`
with `RSPDUMP=<dir>`: the RDRAM and the task header at the moment the task
starts. They hold the game's data and stay out of the repository.
