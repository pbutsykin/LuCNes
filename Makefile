# Makefile

CC = gcc
BUILD ?= release
LUCNES_BIN = lucnes
LUCNES_TEST_BIN = lucnes_test
VIDEO_BACKEND ?= sdl2# sdl2, vt, empty
AUDIO_BACKEND ?= sdl2# sdl2, pipe, empty
INPUT_BACKEND ?= sdl2# sdl2, empty
AUDIO_RL ?= 0
NPROC ?= $(shell nproc --all || echo 1)

# Backend compatibility checks
ifeq ($(AUDIO_BACKEND),sdl2)
ifneq ($(VIDEO_BACKEND),sdl2)
$(error SDL2 audio backend requires SDL2 video backend. \
VIDEO_BACKEND=$(VIDEO_BACKEND) AUDIO_BACKEND=$(AUDIO_BACKEND))
endif
endif

CFLAGS_COMMON = --std=c99 -mno-80387 -mno-sse -Wall -Wextra -Werror
SANITIZERS = -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize-recover=all
ifeq ($(BUILD),debug)
	CFLAGS = -g -Og $(CFLAGS_COMMON) -DLOG_LEVEL=3 $(SANITIZERS)
else ifeq ($(BUILD),release0)
	CFLAGS = -O2 $(CFLAGS_COMMON) -DNDEBUG -DLOG_LEVEL=0
else
	CFLAGS = -g -O2 $(CFLAGS_COMMON) -DNDEBUG -DLOG_LEVEL=2
endif

TEST_DEFS = -DCNES_TEST
CFLAGS_TEST = -g -O2 $(CFLAGS_COMMON) -DLOG_LEVEL=0 -DOPCODE_TRACE=1 $(SANITIZERS) $(TEST_DEFS)

SRC_UTILS = utils/file.c utils/memory.c
SRC_ROM   = rom/rom.c rom/nes.c
SRC_CPU   = cpu/cpu.c cpu/emulate.c
SRC_PPU   = ppu/ppu.c ppu/render.c
SRC_APU   = apu/apu.c
SRC_CTL   = controller/controller.c
SRC_MAP   = mapper/mapper.c mapper/axrom.c mapper/cnrom.c mapper/mmc1.c
SRC_VIDIO = video/$(VIDEO_BACKEND)_backend.c
SRC_AUDIO = audio/$(AUDIO_BACKEND)_backend.c
SRC_INPUT = input/$(INPUT_BACKEND)_backend.c

ifeq ($(AUDIO_RL),1)
	CFLAGS += -DAUDIO_RL
	SRC_AUDIO += audio/rate_limiter.c
endif

SOURCES = $(SRC_UTILS) $(SRC_ROM) $(SRC_CPU) $(SRC_PPU) $(SRC_APU) $(SRC_CTL) $(SRC_MAP)

SOURCES_MAIN = main.c $(SOURCES) $(SRC_VIDIO) $(SRC_AUDIO) $(SRC_INPUT)
SOURCES_TEST = tests/test.c $(SOURCES) video/empty_backend.c audio/empty_backend.c input/empty_backend.c

INCLUDE_COMMON = -I`pwd` -I`pwd`/utils
INCLUDE = $(INCLUDE_COMMON)

ifneq (,$(filter sdl2,$(VIDEO_BACKEND) $(AUDIO_BACKEND) $(INPUT_BACKEND)))
	INCLUDE += $(shell pkg-config --cflags sdl2)
	LIBS += $(shell pkg-config --libs sdl2)
endif

ifeq ($(VIDEO_BACKEND),vt)
	LIBS += video/vtrenderlib/libvtrenderlib.a
	LIBS += -lm
endif

###############
RED=\033[0;31m
GREEN=\033[92m
NC=\033[0m

UPPER = $(shell echo '$1' | tr '[:lower:]' '[:upper:]')

define run_test
	@TEST_NAME="$(strip $1)"; \
	TEST_ARGS="$(strip $2) $(strip $3) $(strip $4)"; \
	TOUT=$$( \
		t1=$$(mktemp); \
		./$(LUCNES_TEST_BIN) tests/$$TEST_NAME.nes $$TEST_ARGS >"$$t1"; \
		xzcat tests/$$TEST_NAME.trace.xz | diff "$$t1" - | sed -n 2p;   \
		rm -f "$$t1" \
	); \
	NAME=$$(echo "$$TEST_NAME" | tr '[:lower:]' '[:upper:]'); \
	if [ -z "$$TOUT" ]; then \
		printf "$$NAME: ${GREEN}PASSED${NC}\n"; \
	else \
		printf "$$NAME: ${RED}FAILED${NC}\n $$TOUT\n"; \
	fi
endef

define mktest
ALL_TESTS += test-$(subst /,-,$(strip $1))
test-$(subst /,-,$(strip $1)): $$(LUCNES_TEST_BIN)
	$$(call run_test, $1, $2, $3, $4)
endef


all: clean lucnes

$(eval $(call mktest, ultimate_nes_cpu_test, --cpu_addr 0xc000, --start_cycle 7))
$(eval $(call mktest, cpu_interrupts/1-cli_latency, --max_cycles 0x74b46))

$(eval $(call mktest, ppu_vbl_nmi/01-vbl_basics, --max_cycles 0x408cf2))
$(eval $(call mktest, ppu_vbl_nmi/02-vbl_set_time, --max_cycles 0x515d32))
$(eval $(call mktest, ppu_vbl_nmi/03-vbl_clear_time, --max_cycles 0x4be936))
$(eval $(call mktest, ppu_vbl_nmi/04-nmi_control, --max_cycles 0xf04e5))
$(eval $(call mktest, ppu_vbl_nmi/05-nmi_timing, --max_cycles 0x62a1c4))
$(eval $(call mktest, ppu_vbl_nmi/06-suppression, --max_cycles 0x63fec0))
$(eval $(call mktest, ppu_vbl_nmi/07-nmi_on_timing, --max_cycles 0x58a27a))
$(eval $(call mktest, ppu_vbl_nmi/08-nmi_off_timing, --max_cycles 0x638a6a))
$(eval $(call mktest, ppu_vbl_nmi/09-even_odd_frames, --max_cycles 0x221acb))
$(eval $(call mktest, ppu_vbl_nmi/10-even_odd_timing, --max_cycles 0x3fa447))
$(eval $(call mktest, ppu_sprite_overflow/Basics, --max_cycles 0x1e650))
$(eval $(call mktest, ppu_sprite_overflow/Details, --max_cycles 0x2e175))
$(eval $(call mktest, ppu_sprite_overflow/Emulator, --max_cycles 0x20ead))
$(eval $(call mktest, ppu_sprite_hit/01.basics, --max_cycles 0x14d5be))
$(eval $(call mktest, ppu_sprite_hit/02.alignment, --max_cycles 0x13ed19))
$(eval $(call mktest, ppu_sprite_hit/03.corners, --max_cycles 0x10bec6))
$(eval $(call mktest, ppu_sprite_hit/04.flip, --max_cycles 0xe791f))
$(eval $(call mktest, ppu_sprite_hit/05.left_clip, --max_cycles 0x130470))
$(eval $(call mktest, ppu_sprite_hit/06.right_edge, --max_cycles 0x104a72))
$(eval $(call mktest, ppu_sprite_hit/07.screen_bottom, --max_cycles 0x11331c))
$(eval $(call mktest, ppu_sprite_hit/08.double_height, --max_cycles 0xf61ca))
$(eval $(call mktest, ppu_sprite_hit/09.timing_basics, --max_cycles 0x2b19f3))
$(eval $(call mktest, ppu_sprite_hit/10.timing_order, --max_cycles 0x1dec5e))
$(eval $(call mktest, ppu_sprite_hit/11.edge_timing, --max_cycles 0x1a49ba))

$(eval $(call mktest, apu/1-len_ctr, --max_cycles 0x91c9c))
$(eval $(call mktest, apu/2-len_table, --max_cycles 0x6d6f0))
$(eval $(call mktest, apu/3-irq_flag, --max_cycles 0x91ca1))
$(eval $(call mktest, apu/4-jitter, --max_cycles 0x91ca0))
$(eval $(call mktest, apu/5-len_timing, --max_cycles 0x34480b))
$(eval $(call mktest, apu/6-irq_flag_timing, --max_cycles 0xa0540))
$(eval $(call mktest, apu/7-dmc_basics, --max_cycles 0xb6249))
$(eval $(call mktest, apu/8-dmc_rates, --max_cycles 0xd339b))

$(eval $(call mktest, mapper/cnrom_0, --max_cycles 0xe259))

$(eval $(call mktest, submapper/3_test_1, --max_cycles 0x13843b))
$(eval $(call mktest, submapper/3_test_2, --max_cycles 0x13843b))
$(eval $(call mktest, submapper/7_test_1, --max_cycles 0x11b099))
$(eval $(call mktest, submapper/7_test_2, --max_cycles 0x11b099))

$(eval $(call mktest, joy/count_errors_fast, --max_cycles 0x16e445))

clean:
	rm -f $(LUCNES_BIN).* $(LUCNES_BIN) $(LUCNES_TEST_BIN) $(LUCNES_TEST_BIN).*
ifeq ($(VIDEO_BACKEND),vt)
	$(MAKE) -C video/vtrenderlib clean
endif

.PHONY: test $(ALL_TESTS)
test: $(LUCNES_TEST_BIN)
	@$(MAKE) --no-print-directory -j$(NPROC) -Otarget $(ALL_TESTS)

$(LUCNES_TEST_BIN):
	$(CC) $(CFLAGS_TEST) $(INCLUDE_COMMON) $(SOURCES_TEST) -o $(LUCNES_TEST_BIN)

lucnes: $(LUCNES_TEST_BIN)
ifeq ($(VIDEO_BACKEND),vt)
	$(MAKE) -C video/vtrenderlib CC="$(CC)" BUILD=$(BUILD) SANITIZERS="$(SANITIZERS)"
endif
	$(CC) $(CFLAGS) $(INCLUDE) $(SOURCES_MAIN) $(LIBS) -o $(LUCNES_BIN)
ifeq ($(BUILD),release)
	objcopy --only-keep-debug $(LUCNES_BIN) $(LUCNES_BIN).dbg
	strip -s $(LUCNES_BIN)
	objcopy --add-gnu-debuglink=$(LUCNES_BIN).dbg $(LUCNES_BIN)
endif
ifeq ($(BUILD),release0)
	strip -s $(LUCNES_BIN)
endif
