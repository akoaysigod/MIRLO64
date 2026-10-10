# tlb_bench: the runtime's TLB instructions (tlb_bench.c) as a game on the SoC in sim/mips_soc: their results, and what each costs.
#
#   make -f tlb_bench.mk [MIRLO=<checkout>] [BUILD=<dir>] [MIPS_CC=... MIPS_BIN=...]       -> $(BUILD)/tlb_bench.z64
#   (a ROM the OWN loader runs as it is). A loader made with TLBPROF=1 turns the game's stores to 0x0490_0004 into the simulation's
#   marks; then, in sim/mips_soc:
#     ROM=$(BUILD)/tlb_bench.z64 CPUSTAT=1 DUMP=0x43B00000,64 stdbuf -oL obj_soc/tb_soc <loader n64rt.bin> -c 120
#   The "[cpustat N cycles" lines at the marks are the six timings of tlb_bench.c in its order (CPU clocks, divided by the number of
#   instructions in it: 32 x ROUNDS tlbr, 32 x ROUNDS tlbp that finds nothing, the same that finds the last entry, ROUNDS x 32 tlbwi
#   that change nothing, 2 x 4 x ROUNDS tlbwi that map and unmap a page, 4 x ROUNDS tlbwr); the DUMP shows 0x600D600D at +0 when the
#   results are right and the checksum at +4, the same for any runtime that does the instructions as the VR4300 does.
MIRLO  ?= ../../..
BUILD  ?= build_tlbbench
include $(MIRLO)/lang/mips/mips.mk
GCFLAGS = $(MIPS_GAME_CFLAGS) -O2 -fno-strict-aliasing -ffreestanding $(EXTRA)
all: $(BUILD)/tlb_bench.z64
$(BUILD)/tlb_bench.o: tlb_bench.c
	@mkdir -p $(BUILD)
	$(MIPS_GCC) $(GCFLAGS) -c $< -o $@
$(BUILD)/tlb_bench.elf: $(BUILD)/tlb_bench.o layout_game.ld
	$(MIPS_LD) -EB -T layout_game.ld --gc-sections -o $@ $<
$(BUILD)/tlb_bench.bin: $(BUILD)/tlb_bench.elf
	$(MIPS_OBJCOPY) -O binary $< $@
$(BUILD)/tlb_bench.z64: $(BUILD)/tlb_bench.bin
	python3 -c "import struct,sys; g=open(sys.argv[1],'rb').read(); h=struct.pack('>IIII',0x80371240,0x0F,0x80000400,0x1444)+bytes(16)+b'TLB BENCH'.ljust(20); open(sys.argv[2],'wb').write((h.ljust(0x1000,b'\0')+g).ljust(0x101000,b'\0'))" $< $@
clean:
	rm -rf $(BUILD)
.PHONY: all clean
