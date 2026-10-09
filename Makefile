PROJECT_NAME = N64FlashcartMenu

.DEFAULT_GOAL := all

SOURCE_DIR = src
ASSETS_DIR = assets
FILESYSTEM_DIR = filesystem
BUILD_DIR = build
OUTPUT_DIR = output

# uncomment the following line to support preview API's in libDragon.
# LIBDRAGON_PREVIEW = 2

MENU_VERSION ?= "Preview release"
BUILD_TIMESTAMP = "$(shell TZ='UTC' date "+%Y-%m-%d %H:%M:%S %:z")"

include $(N64_INST)/include/n64.mk

N64_ROM_SAVETYPE = none
N64_ROM_RTC = 1
N64_ROM_REGIONFREE = 1
N64_ROM_REGION = E
N64_ROM_EXPANSIONPAK = recommended

# Fast rebooting has proved to be unreliable for the moment, so it is disabled in favour of the menu autoload.
# If you want to enable fast rebooting, comment out the following line.
FLAGS ?= -DFEATURE_AUTOLOAD_ROM_ENABLED

N64_CFLAGS += -iquote $(SOURCE_DIR) -iquote $(ASSETS_DIR) -I $(SOURCE_DIR)/libs -isystem $(SOURCE_DIR)/libs/miniz -flto=auto $(FLAGS)
N64_CFLAGS += -isystem $(SOURCE_DIR)/libs/libjpeg-turbo -isystem $(SOURCE_DIR)/libs/libjpeg-turbo/libjpeg-turbo-src/src

JPEG_DIR = $(SOURCE_DIR)/libs/libjpeg-turbo
JPEG_LIB = $(JPEG_DIR)/build/libjpeg.a

SRCS = \
	main.c \
	boot/boot.c \
	boot/cheats.c \
	boot/cic.c \
	boot/reboot.S \
	flashcart/64drive/64drive_ll.c \
	flashcart/64drive/64drive.c \
	flashcart/flashcart_utils.c \
	flashcart/ed64/ed64_vseries.c \
	flashcart/ed64/ed64_vseries_ll.c \
	flashcart/ed64/ed64_xseries.c \
	flashcart/ed64/ed64_xseries_ll.c \
	flashcart/ed64/ed64_pseudo_state.c \
	flashcart/flashcart.c \
	flashcart/sc64/sc64_ll.c \
	flashcart/sc64/sc64.c \
	libs/libspng/spng/spng.c \
	libs/miniz/miniz_tdef.c \
	libs/miniz/miniz_tinfl.c \
	libs/miniz/miniz_zip.c \
	libs/miniz/miniz.c \
	menu/ini_parser.c \
	menu/actions.c \
	menu/audio_player.c \
	menu/bookkeeping.c \
	menu/cart_load.c \
	menu/datel_codes.c \
	menu/disk_info.c \
	menu/fonts.c \
	menu/hdmi.c \
	menu/id3_parser.c \
	menu/jpeg_decoder.c \
	menu/menu.c \
	menu/path.c \
	menu/png_decoder.c \
	menu/rom_info.c \
	menu/settings.c \
	menu/theme.c \
	menu/sound.c \
	menu/sprites.c \
	menu/zip_entry_count.c \
	menu/ui_components/background.c \
	menu/ui_components/boxart.c \
	menu/ui_components/common.c \
	menu/ui_components/context_menu.c \
	menu/ui_components/file_info.c \
	menu/ui_components/file_list.c \
	menu/ui_components/tabs.c \
	menu/usb_comm.c \
	menu/views/browser.c \
	menu/views/credits.c \
	menu/views/datel_code_editor.c \
	menu/views/error.c \
	menu/views/extract_file.c \
	menu/views/fault.c \
	menu/views/file_info.c \
	menu/views/grid.c \
	menu/views/history_favorites.c \
	menu/views/image_viewer.c \
	menu/views/text_viewer.c \
	menu/views/load_disk.c \
	menu/views/load_emulator.c \
	menu/views/load_rom.c \
	menu/views/music_player.c \
	menu/views/startup.c \
	menu/views/system_info.c \
	menu/views/settings_editor.c \
	menu/views/settings_theme.c \
	menu/views/settings_tab.c \
	menu/views/rtc.c \
	menu/views/flashcart_info.c \
	menu/views/cpakfs_manager.c \
	menu/views/cpak_dump_info.c \
	menu/views/cpak_note_dump_info.c \
	utils/cpakfs_utils.c \
	utils/fs.c \
	utils/utf_converter.c \

FONTS = \
	Firple-Bold.ttf

SOUNDS_WAV = \
	cursorsound.wav \
	back.wav \
	bgm.wav \
	bgm_alt.wav \
	enter.wav \
	error.wav \
	settings.wav

SOUNDS_XM ?=

IMAGES = \
	filetype_compressed.png \
	filetype_folder.png \
	filetype_n64cart.png \
	filetype_n64disk.png \
	filetype_music.png \
	filetype_text.png \
	filetype_image.png \
	filetype_save.png \
	filetype_unknown.png

OBJS = $(addprefix $(BUILD_DIR)/, $(addsuffix .o,$(basename $(SRCS))))
MINIZ_OBJS = $(filter $(BUILD_DIR)/libs/miniz/%.o,$(OBJS))
SPNG_OBJS = $(filter $(BUILD_DIR)/libs/libspng/%.o,$(OBJS))
DEPS = $(OBJS:.o=.d)

FILESYSTEM = \
	$(addprefix $(FILESYSTEM_DIR)/menu/fonts/, $(notdir $(FONTS:%.ttf=%.font64))) \
	$(FILESYSTEM_DIR)/menu/fonts/digits/Firple-Bold.font64 \
	$(addprefix $(FILESYSTEM_DIR)/menu/sounds/, $(notdir $(SOUNDS_WAV:%.wav=%.wav64))) \
	$(addprefix $(FILESYSTEM_DIR)/menu/sounds/, $(notdir $(SOUNDS_XM:%.xm=%.xm64))) \
	$(addprefix $(FILESYSTEM_DIR)/menu/sprites/, $(notdir $(IMAGES:%.png=%.sprite)))

$(MINIZ_OBJS): N64_CFLAGS+=-Wno-unused-function -fcompare-debug-second
$(SPNG_OBJS): N64_CFLAGS+=-DSPNG_USE_MINIZ -fcompare-debug-second
$(FILESYSTEM_DIR)/menu/fonts/Firple-Bold.font64: MKFONT_FLAGS+=--compress 1 --outline 1 --size 15 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1
$(FILESYSTEM_DIR)/menu/sounds/%.wav64: AUDIOCONV_FLAGS=--wav-compress 1
# Large digits for Grid group counts. mkfont names its output after the source,
# and needs the ellipsis glyph (.) even though the counts never truncate.
$(FILESYSTEM_DIR)/menu/fonts/digits/Firple-Bold.font64: $(ASSETS_DIR)/fonts/Firple-Bold.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@)
	@$(N64_MKFONT) --compress 1 --outline 3 --size 24 --range 2E-2E --range 30-39 -o $(dir $@) "$<"

$(@info $(shell mkdir -p ./$(FILESYSTEM_DIR) &> /dev/null))

$(FILESYSTEM_DIR)/menu/fonts/%.font64: $(ASSETS_DIR)/fonts/%.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@)
	@$(N64_MKFONT) $(MKFONT_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEM_DIR)/menu/sounds/%.wav64: $(ASSETS_DIR)/sounds/%.wav
	@echo " [AUDIO WAV] $@"
	@mkdir -p $(dir $@)
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEM_DIR)/menu/sounds/%.xm64: $(ASSETS_DIR)/sounds/%.xm
	@echo " [AUDIO XM] $@"
	@mkdir -p $(dir $@)
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEM_DIR)/menu/sprites/%.sprite: $(ASSETS_DIR)/images/%.png
	@echo " [SPRITE] $@"
	@mkdir -p $(dir $@)
	@$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o $(dir $@) "$<"

$(BUILD_DIR)/$(PROJECT_NAME).dfs: $(FILESYSTEM)

$(BUILD_DIR)/menu/views/credits.o: .FORCE
$(BUILD_DIR)/menu/views/credits.o: FLAGS+=-DMENU_VERSION=\"$(MENU_VERSION)\" -DBUILD_TIMESTAMP=\"$(BUILD_TIMESTAMP)\"

$(JPEG_LIB): .FORCE
	$(MAKE) -C $(JPEG_DIR)

$(BUILD_DIR)/$(PROJECT_NAME).elf: $(OBJS) $(JPEG_LIB)

disassembly: $(BUILD_DIR)/$(PROJECT_NAME).elf
	@$(N64_OBJDUMP) -S $< > $(BUILD_DIR)/$(PROJECT_NAME).lst
.PHONY: disassembly

$(PROJECT_NAME).z64: N64_ROM_TITLE=$(PROJECT_NAME)
$(PROJECT_NAME).z64: N64_ROM_METADATA=metadata/metadata.ini
$(PROJECT_NAME).z64: $(BUILD_DIR)/$(PROJECT_NAME).dfs

$(@info $(shell mkdir -p ./$(OUTPUT_DIR) &> /dev/null))

$(OUTPUT_DIR)/$(PROJECT_NAME).n64: $(PROJECT_NAME).z64
	@mv $< $@

64drive: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/menu.bin
.PHONY: 64drive

ed64: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/OS64.v64
.PHONY: ed64

ed64-clone: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/OS64P.v64
.PHONY: ed64-clone

sc64: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/sc64menu.n64
.PHONY: sc64

all: $(OUTPUT_DIR)/$(PROJECT_NAME).n64 64drive ed64 ed64-clone sc64
.PHONY: all

clean:
	@rm -f ./$(FILESYSTEM)
	@find ./$(FILESYSTEM_DIR)/menu/ -type d -empty -delete
	@rm -rf ./$(BUILD_DIR) ./$(OUTPUT_DIR)
	@$(MAKE) -C $(JPEG_DIR) clean
.PHONY: clean

format:
	@find ./$(SOURCE_DIR) \
		-path \./$(SOURCE_DIR)/libs -prune \
		-o -iname *.c -print \
		-o -iname *.h -print \
		| xargs clang-format -i

run: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat
else
	./remotedeploy.sh
endif
.PHONY: run

run-debug: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /d
else
	./remotedeploy.sh -d
endif
.PHONY: run-debug

run-debug-reboot: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /dr
else
	./remotedeploy.sh -dr
endif
.PHONY: run-debug-reboot

run-debug-upload: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /dur
else
	./remotedeploy.sh -dur
endif
.PHONY: run-debug-upload

# test:
#   TODO: run tests

.FORCE:

-include $(DEPS)
