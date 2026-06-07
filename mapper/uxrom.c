/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Pavel Butsykin
 */
#define CNES_MAPPER_UXROM
#define MAPPER

#include <utils/utils.h>
#include <cpu/interface.h>
#include <rom/rom.h>
#include <connector.h>

#include "mapper.h"

/*
 * https://www.nesdev.org/wiki/NES_2.0_submappers#002:_UxROM
 */
enum UxRomSubmapper {
    UXROM_SUBMAPPER_DEFAULT      = 0,
    UXROM_SUBMAPPER_NO_CONFLICT  = 1,
    UXROM_SUBMAPPER_BUS_CONFLICT = 2,
};

void UxRomPrgInit(MapperObj* _ __maybe_unused, MMap* mmap, const region_t* prg)
{
    MapperPrgSet16K(mmap, PRG_BANK16K_WIN0, prg->data);
    MapperPrgSet16K(mmap, PRG_BANK16K_WIN1, prg->data + prg->size - KB(16));
}

/*
 * https://www.nesdev.org/wiki/UxROM
 * The original boards have bus conflicts, so they are emulated unless a
 * submapper explicitly disable it.
 */
void UxRomBankSwitch(MapperObj* mapper, uint16_t cpuAddr, uint8_t bank)
{
    const MapperId* id = mapper->id;
    const CNesConnector* con = mapper->con;
    const region_t* prg = &con->rdesc->prg;

    if (con->rdesc->submapper != UXROM_SUBMAPPER_NO_CONFLICT) {
        uint8_t* addr = MMapPrgResolve(CpuMMap(con->cpu), cpuAddr);

        bank = *addr & bank;
        LogPrintDbg("Bus conflict: rom=%02X, result=%02X\n", *addr, bank);
    }

    if (unlikely(mapper->bank == bank))
        return;

    mapper->bank = bank;
    bank &= (prg->size >> id->bShift) - 1;
    LogPrintAssert((uint32_t)(bank << id->bShift) <= prg->size, "PRG overflow");

    MapperPrgSet16K(CpuMMap(con->cpu), PRG_BANK16K_WIN0, prg->data + (bank << id->bShift));
}

