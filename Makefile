# Makefile

CC = gcc
BUILD ?= release
LUCNES_BIN = lucnes
LUCNES_TEST_BIN = lucnes_test
AUDIO_BACKEND ?= sdl2# sdl2, pipe, empty
AUDIO_RL ?= 0

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
SRC_MAP   = mapper/mapper.c mapper/axrom.c mapper/cnrom.c
SRC_VIDIO = video/sdl2_backend.c
SRC_AUDIO = audio/$(AUDIO_BACKEND)_backend.c
SRC_INPUT = input/sdl2_backend.c

ifeq ($(AUDIO_RL),1)
    CFLAGS += -DAUDIO_RL
    SRC_AUDIO += audio/rate_limiter.c
endif

SOURCES = $(SRC_UTILS) $(SRC_ROM) $(SRC_CPU) $(SRC_PPU) $(SRC_APU) $(SRC_CTL) $(SRC_MAP)

SOURCES_MAIN = main.c $(SOURCES) $(SRC_VIDIO) $(SRC_AUDIO) $(SRC_INPUT)
SOURCES_TEST = tests/test.c $(SOURCES) video/empty_backend.c audio/empty_backend.c input/empty_backend.c

INCLUDE = -I`pwd` -I`pwd`/utils `pkg-config --cflags sdl2`

LIBS = `pkg-config --libs sdl2`

###############
RED=\033[0;31m
GREEN=\033[92m
NC=\033[0m

UPPER = $(shell echo '$1' | tr '[:lower:]' '[:upper:]')

define run_test
	@TEST_NAME="$(strip $1)"; \
	TEST_ARGS="$(strip $2) $(strip $3) $(strip $4)"; \
	TOUT=$$(diff <(./$(LUCNES_TEST_BIN) tests/$$TEST_NAME.nes $$TEST_ARGS) <(xzcat tests/$$TEST_NAME.trace.xz) | sed -n 2p); \
	NAME=$$(echo "$$TEST_NAME" | tr '[:lower:]' '[:upper:]'); \
	if [ -z "$$TOUT" ]; then \
		printf "$$NAME: ${GREEN}PASSED${NC}\n"; \
	else \
		printf "$$NAME: ${RED}FAILED${NC}\n $$TOUT\n"; \
	fi
endef


all: clean lucnes

clean:
	rm -f $(LUCNES_BIN).* $(LUCNES_BIN) $(LUCNES_TEST_BIN) $(LUCNES_TEST_BIN).*

test: $(LUCNES_TEST_BIN)
	$(call run_test, ultimate_nes_cpu_test, --cpu_addr 0xc000, --start_cycle 7)
	$(call run_test, cpu_interrupts/1-cli_latency, --max_cycles 0x74b46)

	$(call run_test, ppu_vbl_nmi/01-vbl_basics, --max_cycles 0x408cf2)
	$(call run_test, ppu_vbl_nmi/02-vbl_set_time, --max_cycles 0x515d32)
	$(call run_test, ppu_vbl_nmi/03-vbl_clear_time, --max_cycles 0x4be936)
	$(call run_test, ppu_vbl_nmi/04-nmi_control, --max_cycles 0xf04e5)
	$(call run_test, ppu_vbl_nmi/05-nmi_timing, --max_cycles 0x62a1c4)
	$(call run_test, ppu_vbl_nmi/06-suppression, --max_cycles 0x63fec0)
	$(call run_test, ppu_vbl_nmi/07-nmi_on_timing, --max_cycles 0x58a27a)
	$(call run_test, ppu_vbl_nmi/08-nmi_off_timing, --max_cycles 0x638a6a)
	$(call run_test, ppu_vbl_nmi/09-even_odd_frames, --max_cycles 0x221acb)
	$(call run_test, ppu_vbl_nmi/10-even_odd_timing, --max_cycles 0x3fa447)
	$(call run_test, ppu_sprite_overflow/Basics, --max_cycles 0x1e650)
	$(call run_test, ppu_sprite_overflow/Details, --max_cycles 0x2e175)
	$(call run_test, ppu_sprite_overflow/Emulator, --max_cycles 0x20ead)
	$(call run_test, ppu_sprite_hit/01.basics, --max_cycles 0x14d5be)
	$(call run_test, ppu_sprite_hit/02.alignment, --max_cycles 0x13ed19)
	$(call run_test, ppu_sprite_hit/03.corners, --max_cycles 0x10bec6)
	$(call run_test, ppu_sprite_hit/04.flip, --max_cycles 0xe791f)
	$(call run_test, ppu_sprite_hit/05.left_clip, --max_cycles 0x130470)
	$(call run_test, ppu_sprite_hit/06.right_edge, --max_cycles 0x104a72)
	$(call run_test, ppu_sprite_hit/07.screen_bottom, --max_cycles 0x11331c)
	$(call run_test, ppu_sprite_hit/08.double_height, --max_cycles 0xf61ca)
	$(call run_test, ppu_sprite_hit/09.timing_basics, --max_cycles 0x2b19f3)
	$(call run_test, ppu_sprite_hit/10.timing_order, --max_cycles 0x1dec5e)
	$(call run_test, ppu_sprite_hit/11.edge_timing, --max_cycles 0x1a49ba)

	$(call run_test, apu/1-len_ctr, --max_cycles 0x91c9c)
	$(call run_test, apu/2-len_table, --max_cycles 0x6d6f0)
	$(call run_test, apu/3-irq_flag, --max_cycles 0x91ca1)
	$(call run_test, apu/4-jitter, --max_cycles 0x91ca0)
	$(call run_test, apu/5-len_timing, --max_cycles 0x34480b)
	$(call run_test, apu/6-irq_flag_timing, --max_cycles 0xa0540)
	$(call run_test, apu/7-dmc_basics, --max_cycles 0xb6249)
	$(call run_test, apu/8-dmc_rates, --max_cycles 0xd339b)

	$(call run_test, submapper/3_test_1, --max_cycles 0x13843b)
	$(call run_test, submapper/3_test_2, --max_cycles 0x13843b)
	$(call run_test, submapper/7_test_1, --max_cycles 0x11b099)
	$(call run_test, submapper/7_test_2, --max_cycles 0x11b099)

	$(call run_test, joy/count_errors_fast, --max_cycles 0x16e445)

$(LUCNES_TEST_BIN):
	$(CC) $(CFLAGS_TEST) $(INCLUDE) $(SOURCES_TEST) $(LIBS) -o $(LUCNES_TEST_BIN)

lucnes: $(LUCNES_TEST_BIN)
	$(CC) $(CFLAGS) $(INCLUDE) $(SOURCES_MAIN) $(LIBS) -o $(LUCNES_BIN)
ifeq ($(BUILD),release)
	objcopy --only-keep-debug $(LUCNES_BIN) $(LUCNES_BIN).dbg
	strip -s $(LUCNES_BIN)
	objcopy --add-gnu-debuglink=$(LUCNES_BIN).dbg $(LUCNES_BIN)
endif
ifeq ($(BUILD),release0)
	strip -s $(LUCNES_BIN)
endif
