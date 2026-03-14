/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#include "config.h"
#include <utils/utils.h>
#include <stdlib.h>
#include <argp.h>

#include "connector.h"
#include <rom/rom.h>
#include <cpu/interface.h>
#include <ppu/interface.h>
#include <apu/interface.h>
#include <mapper/interface.h>
#include <controller/interface.h>
#include <cpu/cpu.h>

#if LOG_LEVEL >= LOG_LVL_DEBUG
#error "FAIL"
#endif

static char doc[] = "LuCNes Test - NES emulator test runner";
static char args_doc[] = "ROM_FILE";

static struct argp_option options[] = {
    {"cpu_addr",     'o', "OFFSET", 0, "CPU address offset (hex)", 0},
    {"start_cycle",  's', "CYCLE",  0, "Start cycle (hex)", 0},
    {"max_cycles",   'm', "CYCLES", 0, "Maximum cycles to run (hex)", 0},
    {0}
};

struct arguments {
    char *file_path;
    uint16_t cpu_addr;
    uint16_t start_cycle;
    uint32_t max_cycles;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;

    switch (key) {
        case 'o':
            args->cpu_addr = strtol(arg, NULL, 16);
            break;
        case 's':
            args->start_cycle = strtol(arg, NULL, 16);
            break;
        case 'm':
            args->max_cycles = strtol(arg, NULL, 16);
            break;
        case ARGP_KEY_ARG:
            if (state->arg_num >= 1)
                argp_usage(state); /* Too many args */
            args->file_path = arg;
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 1)
                argp_usage(state); /* Not enough args */
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static void PrintTestMessage(MMap* mmap)
{
#define BLARGG_SIGNATURE "\xDE\xB0\x61"

    uint16_t addr = 0x6000;
    uint8_t status = CpuMemRead8(mmap, addr++);
    uint8_t signature[3] = {
        CpuMemRead8(mmap, addr++),
        CpuMemRead8(mmap, addr++),
        CpuMemRead8(mmap, addr++),
    };
    uint8_t c;

    if (memcmp(signature, BLARGG_SIGNATURE, sizeof(signature))) {
        LogPrintDbg("Invalid signature: %X%X%X\n",  signature[0],  signature[1], signature[2]);
        return;
    }

    while ((c = CpuMemRead8(mmap, addr++)))
        putchar(c);

    printf("\nStatus: %x\n", status);
}

int main(int argc, char **argv)
{
    struct argp argp = {
        .options = options,
        .parser = parse_opt,
        .args_doc = args_doc,
        .doc = doc,
    };
    struct arguments args = {
        .file_path = NULL,
    };
    CNesConnector connector = {
        .pins = 0,
    };

    argp_parse(&argp, argc, argv, 0, 0, &args);

    connector.rdesc = LoadRomFile(args.file_path);
    if (!connector.rdesc) {
        LogPrintErr("Rom open fail\n");
        return -1;
    }

    connector.mapper = MapperInit(connector.rdesc->mapperId, &connector);
    assert(connector.mapper != NULL);

    CNesCPUTestState test = {
        .offs = args.cpu_addr,
        .cycles = args.start_cycle,
        .maxCycles = args.max_cycles,
    };
    connector.cpu = CpuInit(connector.rdesc, connector.mapper, &connector, &test);
    assert(connector.cpu != NULL);

    connector.ppu = PpuInit(connector.cpu, connector.rdesc, connector.mapper, &connector);
    assert(connector.ppu != NULL);

    connector.apu = ApuInit(connector.cpu, &connector);
    assert(connector.apu != NULL);

    connector.ctl = ControllerInit(connector.cpu, &connector);
    assert(connector.ctl != NULL);

    CpuMainLoop(connector.cpu);

    PrintTestMessage(&connector.cpu->mmap);

    ControllerFree(connector.ctl);
    ApuFree(connector.apu);
    PpuFree(connector.ppu);
    CpuFree(connector.cpu);
    MapperFree(connector.mapper);
    UnloadRomFile(connector.rdesc);
    return 0;
}
