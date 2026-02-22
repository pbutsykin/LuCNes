/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_MAIN

#include "config.h"
#include <utils/utils.h>

#include "connector.h"
#include <rom/rom.h>
#include <cpu/interface.h>
#include <ppu/interface.h>
#include <apu/interface.h>
#include <mapper/interface.h>
#include <controller/interface.h>

int main(int argc, char const *argv[])
{
    CNesConnector con = {
        .pins = 0,
    };
    int ret = -1;

    if (argc != 2) {
        LogPrintErr("Usage: %s <rom file>\n", argv[0]);
        return ret;
    }

    con.rdesc = LoadRomFile((char*)argv[1]);
    if (!con.rdesc) {
        LogPrintErr("Rom open failed\n");
        return ret;
    }

    con.mapper = MapperLookupById(con.rdesc->mapperId);
    if (!con.mapper) {
        LogPrintErr("Unsupported mapper: %u\n", con.rdesc->mapperId);
        goto fail0;
    }

    con.cpu = CpuInit(con.rdesc, con.mapper, &con, NULL);
    if (!con.cpu)
        goto fail0;

    con.ppu = PpuInit(con.cpu, con.rdesc, con.mapper, &con);
    if (!con.ppu)
        goto fail1;

    con.apu = ApuInit(con.cpu, &con);
    if (!con.apu)
        goto fail2;

    con.ctl = ControllerInit(con.cpu, &con);
    if (!con.ctl)
        goto fail3;

    ret = CpuMainLoop(con.cpu);

    ControllerFree(con.ctl);
fail3:
    ApuFree(con.apu);
fail2:
    PpuFree(con.ppu);
fail1:
    CpuFree(con.cpu);
fail0:
    UnloadRomFile(con.rdesc);
    return ret;
}
