SOURCE_DIR = src
BUILD_DIR = build
include $(N64_INST)/include/n64.mk

ROM_NAME = duke3d

# The compiled game is the same for every ROM (base game and expansions) and
# holds no game data: `make engine` builds it, with the project's media already
# converted, into $(ENGINE_DIR). tools/pack_rom.py then packs it with your own
# game files from gamedata/ into output/*.z64 -- the same script the GitHub
# Release ships, so users need neither Docker nor the N64 toolchain.
#
#   make                        engine + output/duke3d.z64 (gamedata/DUKE3D.GRP)
#   make ADDON=gamedata/nwinter engine + output/duke3d-nwinter.z64 (see README)
#   make roms                   engine + the base game and every expansion in gamedata/
#
# ADDON_CON=NAME.CON picks the add-on's main CON script by hand, SOUNDFONT=path.sf2
# packs a smaller General MIDI soundfont.
ENGINE_DIR = $(BUILD_DIR)/prebuilt
PACK_ROM = python3 tools/pack_rom.py --engine-dir $(ENGINE_DIR) --tools-dir $(N64_BINDIR) \
           $(if $(SOUNDFONT),--soundfont $(SOUNDFONT))

all: engine
	$(PACK_ROM) $(if $(ADDON),$(ADDON) $(if $(ADDON_CON),--con $(ADDON_CON)),base)
roms: engine
	$(PACK_ROM)
.PHONY: all roms

# Chocolate Duke3D sources (see src/UPSTREAM.md), minus the SDL/desktop-only files
# that are replaced by the N64 platform layer in src/n64.
ENGINE_SRC = cache.c draw.c dummy_multi.c engine.c filesystem.c \
             fixedPoint_math.c network.c tiles.c
GAME_SRC   = actors.c animlib.c config.c console.c control.c cvar_defs.c cvars.c \
             dummy_audiolib.c game.c gamedef.c global.c keyboard.c menues.c player.c \
             premap.c rts.c scriplib.c sector.c sounds.c
# The sound effects are mixed on the RSP (src/n64/n64_fx.c): of the original
# CPU mixer (audiolib) only the pitch table is used.
AUDIO_SRC  = pitch.c
N64_SRC    = n64_display.c n64_fx.c n64_intro.c n64_music.c n64_savefs.c n64_system.c

SRCS = $(addprefix src/engine/,$(ENGINE_SRC)) \
       $(addprefix src/game/,$(GAME_SRC)) \
       $(addprefix src/game/audiolib/,$(AUDIO_SRC)) \
       $(addprefix src/n64/,$(N64_SRC))
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)

# Upstream note: starting a new game crashes if premap.c is optimized.
$(BUILD_DIR)/game/premap.o: CFLAGS += -O0

# The renderer: -O3 measured ~1 ms per frame better than -O2 in ares, which is
# about the noise of moving code around (instruction cache layout).
$(BUILD_DIR)/engine/engine.o $(BUILD_DIR)/engine/draw.o: CFLAGS += -O3

# 1996 code: keep warnings visible but never fatal.
#
# The code was written and tested where int32_t is 'int'; on the N64 toolchain
# it is 'long'. Both are 32 bits with the same calling convention, so make
# int32_t an 'int' to avoid hundreds of int/long prototype mismatches.
# Some audiolib files relied on SDL.h to pull in <inttypes.h>.
N64_CFLAGS += -U__INT32_TYPE__ -D__INT32_TYPE__=int \
              -U__UINT32_TYPE__ '-D__UINT32_TYPE__=unsigned int' \
              -include inttypes.h \
              -Isrc/n64 -Isrc/engine -Isrc/game \
              -std=gnu11 -fpermissive -fcommon -fno-strict-aliasing -fwrapv -Wno-error \
              -Wno-unused -Wno-pointer-sign -Wno-parentheses -Wno-char-subscripts \
              -Wno-missing-braces -Wno-maybe-uninitialized -Wno-implicit-function-declaration \
              -Wno-int-conversion -Wno-incompatible-pointer-types

N64_ROM_TITLE = "Duke Nukem 3D"
N64_ROM_SAVETYPE = flashram
N64_ROM_EXPANSIONPAK = required

$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS)

# ---------------------------------------------------------------------------
# Media: converted once here, packed as is into every ROM
# ---------------------------------------------------------------------------
# General MIDI soundfont for the music (assets/soundfont, see src/UPSTREAM.md).
# The game's songs are in the user's GRP: tools/pack_rom.py converts those.
DEFAULT_SOUNDFONT = assets/soundfont/GeneralUser-GS.sf2

$(BUILD_DIR)/media/soundfont.sf64: $(DEFAULT_SOUNDFONT)
	@mkdir -p $(BUILD_DIR)/sf64
	@echo "    [SF64] $<"
	$(N64_AUDIOCONV) -o $(BUILD_DIR)/sf64 $< >/dev/null
	@mkdir -p $(dir $@)
	mv $(BUILD_DIR)/sf64/$(basename $(notdir $<)).sf64 $@

# "Powered by libdragon" boot logo (see assets/intro/README.md).
INTRO_SPRITES = $(patsubst assets/intro/%.png,$(BUILD_DIR)/media/intro/%.sprite,$(wildcard assets/intro/*.png))

$(BUILD_DIR)/media/intro/%.sprite: assets/intro/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $<"
	$(N64_MKSPRITE) -f I8 -o $(dir $@) $<

# Main menu background of each expansion's ROM: assets/addons/<name>.png
# (320x200, 256 colors, made from any picture with tools/menubg.py).
MENUBG_SPRITES = $(patsubst assets/addons/%.png,$(BUILD_DIR)/media/menubg/%.sprite,$(wildcard assets/addons/*.png))

$(BUILD_DIR)/media/menubg/%.sprite: assets/addons/%.png
	@mkdir -p $(dir $@)
	@echo "    [MENUBG] $<"
	$(N64_MKSPRITE) -f CI8 -o $(dir $@) $<

# ---------------------------------------------------------------------------
# Prebuilt engine (no game data needed)
# ---------------------------------------------------------------------------
# Everything n64.mk's %.z64 rule does before packing the filesystem, plus the
# media above and the ROM header settings, so that tools/pack_rom.py stays in
# sync with this Makefile. CI publishes it in each Release
# (.github/workflows/release.yml, see tools/vendor/engine/README.md).
ENGINE_ELF = $(BUILD_DIR)/$(ROM_NAME).elf

# n64.mk only switches to the N64 toolchain for %.z64 targets.
engine: CC=$(N64_CC)
engine: CXX=$(N64_CXX)
engine: AS=$(N64_AS)
engine: LD=$(N64_LD)
engine: CFLAGS+=$(N64_CFLAGS)
engine: CXXFLAGS+=$(N64_CXXFLAGS)
engine: ASFLAGS+=$(N64_ASFLAGS)
engine: RSPASFLAGS+=$(N64_RSPASFLAGS)
engine: LDFLAGS+=$(N64_LDFLAGS)
engine: $(ENGINE_ELF) $(BUILD_DIR)/media/soundfont.sf64 $(INTRO_SPRITES) $(MENUBG_SPRITES)
	@echo "    [ENGINE] $(ENGINE_DIR)"
	@rm -rf $(ENGINE_DIR)
	@mkdir -p $(ENGINE_DIR)
	$(N64_SYM) --all $< $<.sym
	cp $< $<.stripped
	$(N64_STRIP) -s $<.stripped
	$(N64_ELFCOMPRESS) -o $(dir $<) -c $(N64_ROM_ELFCOMPRESS) $<.stripped
	cp $<.stripped $<.sym $(ENGINE_DIR)/
	cp -r $(BUILD_DIR)/media/. $(ENGINE_DIR)/
	$(if $(N64_TOOLFILES),cp $(N64_TOOLFILES) $(ENGINE_DIR)/)
	@printf 'title=%s\nsavetype=%s\nexpansionpak=%s\nregionfree=%s\n' \
		$(N64_ROM_TITLE) $(strip $(N64_ROM_SAVETYPE)) $(strip $(N64_ROM_EXPANSIONPAK)) \
		$(if $(strip $(N64_ROM_REGIONFREE)),1,0) > $(ENGINE_DIR)/rom.cfg
.PHONY: engine

# ---------------------------------------------------------------------------
# Development options (compiled into the engine)
# ---------------------------------------------------------------------------
# Per-section frame timings in the debug log: make PROFILE=1
ifneq ($(PROFILE),)
N64_CFLAGS += -DN64_PROFILE
endif

# Walls and sky drawn by the RDP (see src/n64/n64_columns.h), on by default:
# make RDP_COLUMNS=0 draws them on the CPU.
ifneq ($(RDP_COLUMNS),)
N64_CFLAGS += -DN64_RDP_COLUMNS=$(RDP_COLUMNS)
endif

# Options that change every object: rebuild everything when they change,
# instead of linking objects built with different settings.
# Test builds that go through the levels by themselves (memory log): make LEVELSKIP=10
ifneq ($(LEVELSKIP),)
N64_CFLAGS += -DN64_TEST_LEVELSKIP=$(LEVELSKIP)
endif
ifneq ($(GOD),)
N64_CFLAGS += -DN64_TEST_GOD
endif
# Earthquake on and off every QUAKE seconds (profile comparison): make QUAKE=5
ifneq ($(QUAKE),)
N64_CFLAGS += -DN64_TEST_QUAKE=$(QUAKE)
endif
# Sets off the C-9s of a hitag 8 s into the level: make DETONATE=253
ifneq ($(DETONATE),)
N64_CFLAGS += -DN64_TEST_DETONATE=$(DETONATE)
endif
ifneq ($(LEVELCYCLE),)
N64_CFLAGS += -DN64_TEST_LEVELCYCLE=$(LEVELCYCLE)
endif

BUILD_OPTIONS = profile=$(PROFILE) rdp_columns=$(RDP_COLUMNS) levelskip=$(LEVELSKIP) levelcycle=$(LEVELCYCLE) god=$(GOD) quake=$(QUAKE) detonate=$(DETONATE)
$(shell mkdir -p $(BUILD_DIR); echo '$(BUILD_OPTIONS)' | cmp -s - $(BUILD_DIR)/options.txt || echo '$(BUILD_OPTIONS)' > $(BUILD_DIR)/options.txt)
$(OBJS): $(BUILD_DIR)/options.txt

# Test command line baked into the ROM, e.g. make ARGS="/v1 /l1"
ifneq ($(ARGS),)
$(BUILD_DIR)/n64/n64_system.o: CFLAGS += '-DN64_ARGS="$(ARGS)"'
endif
# Scripted controller input for tests, e.g. make INPUT="8000:A 9500:A 12000-14000:U"
# (see apply_input_script in src/n64/n64_display.c)
ifneq ($(INPUT),)
$(BUILD_DIR)/n64/n64_display.o: CFLAGS += '-DN64_INPUT_SCRIPT="$(INPUT)"'
endif
# Always rebuilt, so a normal build never keeps a test ARGS/INPUT baked in.
$(BUILD_DIR)/n64/n64_system.o $(BUILD_DIR)/n64/n64_display.o: FORCE
FORCE:
.PHONY: FORCE

clean:
	rm -rf $(BUILD_DIR) output/*.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*/*.d $(BUILD_DIR)/*/*/*.d)
