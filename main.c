/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_MAIN

#include "config.h"
#include <utils/utils.h>
#include <getopt.h>

#include "connector.h"
#include <rom/rom.h>
#include <cpu/interface.h>
#include <ppu/interface.h>
#include <apu/interface.h>
#include <mapper/interface.h>
#include <controller/interface.h>

typedef struct _LuCNeConfig {
    const char* romPath;
    PPUConfig ppu;
} LuCNeConfig;

static void Usage(const char* prog)
{
    LogPrintErr("Usage: %s [--no-sprite-limit] <rom file>\n", prog);
}

static bool ParseArgs(int argc, char* const argv[], LuCNeConfig* cfg)
{
    static const struct option options[] = {
        {"no-sprite-limit", no_argument, NULL, 's'},
        {NULL, 0, NULL, 0}
    };
    int opt;

    while ((opt = getopt_long(argc, argv, "s", options, NULL)) != -1) {
        switch (opt) {
            case 's':
                cfg->ppu.noSpriteLimit = true;
                break;
            default:
                return false;
        }
    }

    if (optind + 1 != argc)
        return false;

    cfg->romPath = argv[optind];
    return true;
}

int main(int argc, char* const argv[])
{
    CNesConnector con = {
        .pins = 0,
    };
    LuCNeConfig cfg = {
        .ppu.noSpriteLimit = false,
    };
    int ret = -1;

    if (!ParseArgs(argc, argv, &cfg)) {
        Usage(argv[0]);
        return ret;
    }

    con.rdesc = LoadRomFile(cfg.romPath);
    if (!con.rdesc) {
        LogPrintErr("Rom open failed\n");
        return ret;
    }

    con.mapper = MapperInit(con.rdesc->mapperId, &con);
    if (!con.mapper) {
        LogPrintErr("Unsupported mapper: %u\n", con.rdesc->mapperId);
        goto fail0;
    }

    con.cpu = CpuInit(con.rdesc, con.mapper, &con, NULL);
    if (!con.cpu)
        goto fail1;

    con.ppu = PpuInit(con.cpu, con.rdesc, con.mapper, &con, &cfg.ppu);
    if (!con.ppu)
        goto fail2;

    con.apu = ApuInit(con.cpu, &con);
    if (!con.apu)
        goto fail3;

    con.ctl = ControllerInit(con.cpu, &con);
    if (!con.ctl)
        goto fail4;

    ret = CpuMainLoop(con.cpu);

    ControllerFree(con.ctl);
fail4:
    ApuFree(con.apu);
fail3:
    PpuFree(con.ppu);
fail2:
    CpuFree(con.cpu);
fail1:
    MapperFree(con.mapper);
fail0:
    UnloadRomFile(con.rdesc);
    return ret;
}
