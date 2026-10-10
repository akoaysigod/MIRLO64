# si_test: libultra's own SI code -- controllers, EEPROM, the accessory's
# blocks, and pak_test.c's battery on top of them -- as an N64 program (a ROM
# made here: si_rom.c) run by the runtime like a game, on the SoC in
# sim/mips_soc. With SI_GAME=1 none of the SI's functions is replaced: they
# run on the SI's registers and the PIF as the runtime emulates them, a trap
# each (../n64rt_io.c). It ends with what each costs, in clocks. libultra is
# not in this repository: ULTRALIB as for pak_test (VERSION likewise).
#
#   make -f si_test.mk ULTRALIB=<...>/lib/ultralib [SI_GAME=1] [EEP16K=1 | EEPDB=1] [HPONLY=1] [BUILD=<dir>]
#   (EEP16K=1: the 16 Kbit chip, forced at build time (n64rt_io.c): the test expects it. EEPDB=1: the test expects it too but the
#   runtime is left alone: the save-type database says so -- SLOT4=<a file with the line "????  eeprom16k"> on the bench)
#   (HPONLY=1: stop after the hot-plug test, which needs the bench's PAD2 window to open after the test starts: ~100 ms in)
#   DATA_JSON=<data.json with slot 0 at address 0> PAD=0-99999:0018 PAD2=55-95:30000010 <sim/mips_soc>/tb_soc $(BUILD)/si_test.bin -c 600
#   (PAD2: a dock pad with analog that is there from field 55 to 95; the data.json of the package has the ROM at 1 MiB, a game file
#   like this one needs its slot 0 at 0)
# (PAD: A and right held, which the test reads back through osContGetReadData)
MIRLO    ?= ../../..
VERSION  ?= 7
RSP      := 1
DEBUG    := 1
RT        = $(MIRLO)/lang/n64rt
PROGRAM   = si_port
BUILD    ?= build_si
OBJECTS   = $(BUILD)/si_main.o $(BUILD)/spec.o $(N64RT_OBJECTS)
.DEFAULT_GOAL := all
include $(RT)/n64rt.mk
include $(MIRLO)/lang/mips/program.mk
CFLAGS   += -O2 -Wall -include $(abspath si_force.h) $(if $(EEP16K),-DSI_TEST_EEP16K)
MIPS_LDFLAGS += --defsym=_fstack=0x400FFFF0

# the ROM: libultra's SI layer and what pak_test uses of it
ROM_ULT   = controller contreaddata contquery sirawdma sirawread sirawwrite si siacs conteepprobe conteepread conteepwrite conteeplongread \
            conteeplongwrite contramread contramwrite crc pfsgetstatus pfsisplug motor \
            gbpakinit gbpakpower gbpakgetstatus gbpakreadid gbpakreadwrite gbpaksetbank gbpakcheckconnector \
            pfsinitpak pfschecker contpfs pfsallocatefile pfsreadwritefile pfssearchfile pfsdeletefile \
            pfsfreeblocks pfsfilestate pfsselectbank
ROM_OBJ   = $(BUILD)/rom/si_rom_start.o $(BUILD)/rom/si_rom.o $(BUILD)/rom/pak_test.o $(addprefix $(BUILD)/rom/ult_,$(addsuffix .o,$(ROM_ULT)))
ROM_CFLAGS = $(MIPS_GAME_CFLAGS) -isystem $(MIPS_LIB)/include -ffunction-sections -fdata-sections -O2 -Wall -fno-strict-aliasing \
            -I$(ULTRALIB)/include -I$(ULTRALIB)/include/PR -I$(ULTRALIB)/include/PRinternal -I$(ULTRALIB)/src -I$(ULTRALIB)/src/io \
            -DBUILD_VERSION=$(VERSION) -DBUILD_VERSION_STRING=\"2.0\" -D_LANGUAGE_C -D_FINALROM -DNDEBUG -DF3DEX_GBI_2 $(if $(or $(EEP16K),$(EEPDB)),-DSI_EEP16K) $(if $(HPONLY),-DSI_HP_ONLY)
all: $(BUILD)/si_test.bin
$(BUILD)/rom/ult_%.o: $(ULTRALIB)/src/io/%.c
	@mkdir -p $(BUILD)/rom
	$(MIPS_GCC) $(ROM_CFLAGS) -w -c $< -o $@
$(BUILD)/rom/pak_test.o: pak_test.c
	@mkdir -p $(BUILD)/rom
	$(MIPS_GCC) $(ROM_CFLAGS) -Dmain=pak_test_main -Dprintf=si_rom_printf -c $< -o $@
$(BUILD)/rom/si_rom.o: si_rom.c
	@mkdir -p $(BUILD)/rom
	$(MIPS_GCC) $(ROM_CFLAGS) -c $< -o $@
$(BUILD)/rom/si_rom_start.o: si_rom_start.S
	@mkdir -p $(BUILD)/rom
	$(MIPS_GCC) $(MIPS_GAME_CFLAGS) -c $< -o $@
$(BUILD)/rom/si_rom.elf: $(ROM_OBJ) si_rom.ld
	$(MIPS_LD) -EB -T si_rom.ld --gc-sections -Map=$(BUILD)/rom/si_rom.map -o $@ $(ROM_OBJ) \
		--start-group $(MIPS_LIB)/libc.a $(MIPS_LIB)/libcrt.a --end-group
$(BUILD)/rom/si_rom.bin: $(BUILD)/rom/si_rom.elf
	$(MIPS_OBJCOPY) -O binary $< $@
$(BUILD)/spec.c: $(BUILD)/rom/si_rom.bin si_spec.py $(RT)/n64rt.h
	READELF="$(MIPS_BIN)readelf" python3 si_spec.py $(BUILD)/rom/si_rom.elf $< $(RT)/n64rt.h $(BUILD)/rom/si_rom.z64 > $@
$(BUILD)/spec.o: $(BUILD)/spec.c
	$(MIPS_GCC) $(CFLAGS) -c $< -o $@
$(BUILD)/si_main.o: si_main.c $(RT)/n64rt.h
	@mkdir -p $(BUILD)
	$(MIPS_GCC) $(CFLAGS) -c $< -o $@
# (the ROM where the runtime wants it: a simulation loads a file of any size at once)
$(BUILD)/si_test.bin: $(BUILD)/$(PROGRAM).bin $(BUILD)/spec.c
	python3 $(MIRLO)/tools/make_mirlo_game.py --video 320 $< $@ ROM0@0x1800000=$(BUILD)/rom/si_rom.z64
clean:
	rm -rf $(BUILD)
.PHONY: all clean
.SECONDARY:
