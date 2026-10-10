# layout_test: a game that uses everything the memory's layout has
# (lang/mips/include/layout.h) at once -- a ROM of 64 MiB, 8 MiB of RDRAM, the
# EEPROM, a Controller Pak -- under the whole runtime, on the SoC in
# sim/mips_soc (built with MIRLO_RSP, MIRLO_Z_PSRAM and MIRLO_ROM64). The game is layout_game.c
# with libultra's own pak code (ULTRALIB: see Makefile); layout_test.sh runs
# it.
#
#   make -f layout.mk ULTRALIB=<...>/lib/ultralib
#   -> build/layout/layout.bin (the program), layout.z64 (the ROM)
MIRLO    ?= ../../..
VERSION  ?= 7
RSP       = 1
DEBUG     = 1
PROGRAM   = layout
BUILD     = build/layout
RT        = $(MIRLO)/lang/n64rt
OBJECTS   = $(BUILD)/layout_main.o $(N64RT_OBJECTS)
.DEFAULT_GOAL := all
include $(RT)/n64rt.mk
include $(MIRLO)/lang/mips/program.mk
CFLAGS   += -O2 -Wall -include $(abspath layout_force.h)
MIPS_LDFLAGS += --defsym=_fstack=0x400FFFF0

ULT_SRC   = gbpakinit gbpakpower gbpakgetstatus gbpakreadid gbpakreadwrite gbpaksetbank gbpakcheckconnector \
            pfsinitpak pfschecker contpfs pfsallocatefile pfsreadwritefile pfssearchfile pfsdeletefile \
            pfsfreeblocks pfsfilestate pfsselectbank
ULT_INC   = -I$(ULTRALIB)/include -I$(ULTRALIB)/include/PR -I$(ULTRALIB)/include/PRinternal -I$(ULTRALIB)/src -I$(ULTRALIB)/src/io \
            -DBUILD_VERSION=$(VERSION) -DBUILD_VERSION_STRING=\"2.0\" -D_LANGUAGE_C -D_FINALROM -DNDEBUG -DF3DEX_GBI_2
GAME_CFLAGS = $(MIPS_GAME_CFLAGS) -O2 -fno-strict-aliasing -ffunction-sections -fdata-sections
GAME_OBJS = $(BUILD)/g_layout_start.o $(BUILD)/g_layout_game.o $(addprefix $(BUILD)/g_ult_,$(addsuffix .o,$(ULT_SRC)))

all: $(BUILD)/$(PROGRAM).bin $(BUILD)/layout.z64
$(BUILD)/g_layout_start.o: layout_start.S layout_game.h
	@mkdir -p $(BUILD)
	$(MIPS_GCC) $(GAME_CFLAGS) -c $< -o $@
$(BUILD)/g_layout_game.o: layout_game.c layout_game.h
	@mkdir -p $(BUILD)
	$(MIPS_GCC) $(GAME_CFLAGS) -Wall $(ULT_INC) -c $< -o $@
$(BUILD)/g_ult_%.o: $(ULTRALIB)/src/io/%.c
	@mkdir -p $(BUILD)
	$(MIPS_GCC) $(GAME_CFLAGS) -w $(ULT_INC) -c $< -o $@
$(BUILD)/layout_game.elf: $(GAME_OBJS) layout_game.ld
	$(MIPS_LD) -EB -T layout_game.ld --gc-sections -Map=$(BUILD)/layout_game.map -o $@ $(GAME_OBJS) $(MIPS_LIB)/libcrt.a
$(BUILD)/layout_game.bin: $(BUILD)/layout_game.elf
	$(MIPS_OBJCOPY) -O binary $< $@
$(BUILD)/layout_syms.h: $(BUILD)/layout_game.elf
	$(MIPS_NM) $< | awk '$$3 ~ /^[A-Za-z_][A-Za-z0-9_]*$$/ { print "#define GAME_" $$3 " 0x" $$1 "u" }' > $@
$(BUILD)/layout_main.o: layout_main.c layout_game.h $(BUILD)/layout_syms.h $(RT)/n64rt.h
	$(MIPS_GCC) $(CFLAGS) -I$(BUILD) -c $< -o $@
$(BUILD)/layout.z64: $(BUILD)/layout_game.bin make_layout_rom.py
	python3 make_layout_rom.py $< $@
.PHONY: all
.SECONDARY:
