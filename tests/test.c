/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#include "config.h"
#include <utils/utils.h>
#include <stdlib.h>
#include <getopt.h>

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

struct arguments {
    char *file_path;
    uint16_t cpu_addr;
    uint16_t start_cycle;
    uint32_t max_cycles;
};

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [--cpu_addr OFFSET] [--start_cycle CYCLE] [--max_cycles CYCLES] ROM_FILE\n", prog);
    exit(1);
}

static void parse_args(int argc, char **argv, struct arguments *args)
{
    static struct option options[] = {
        {"cpu_addr",    required_argument, NULL, 'o'},
        {"start_cycle", required_argument, NULL, 's'},
        {"max_cycles",  required_argument, NULL, 'm'},
        {NULL, 0, NULL, 0}
    };
    int opt;

    while ((opt = getopt_long(argc, argv, "o:s:m:", options, NULL)) != -1) {
        switch (opt) {
            case 'o':
                args->cpu_addr = strtol(optarg, NULL, 16);
                break;
            case 's':
                args->start_cycle = strtol(optarg, NULL, 16);
                break;
            case 'm':
                args->max_cycles = strtol(optarg, NULL, 16);
                break;
            default:
                usage(argv[0]);
        }
    }

    if (optind >= argc)
        usage(argv[0]);
    args->file_path = argv[optind];
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
    struct arguments args = {
        .file_path = NULL,
    };
    CNesConnector connector = {
        .pins = 0,
    };

    parse_args(argc, argv, &args);

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
