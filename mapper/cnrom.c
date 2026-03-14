/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_MAPPER_CNROM
#define MAPPER

#include <utils/utils.h>
#include <ppu/interface.h>
#include <rom/rom.h>
#include <connector.h>

#include "mapper.h"

/*
 * https://www.nesdev.org/wiki/NES_2.0_submappers#003:_CNROM
 */
enum CnRomSubmapper {
    CNROM_SUBMAPPER_DEFAULT      = 0,
    CNROM_SUBMAPPER_NO_CONFLICT  = 1,
    CNROM_SUBMAPPER_BUS_CONFLICT = 2,
};

/*
 * https://www.nesdev.org/wiki/CNROM
 * CNROM: 32KB fixed PRG, writes to $8000-$FFFF select 8KB CHR bank.
 */
void CnRomBankSwitch(MapperObj* mapper, const uint8_t* addr, uint8_t bank)
{
    const MapperId* id = mapper->id;
    const CNesConnector* con = mapper->con;
    PPUMMap* ppuMMap = PpuMMap(con->ppu);
    uint8_t* bankBase;

    if (con->rdesc->submapper == CNROM_SUBMAPPER_BUS_CONFLICT) {
        bank = *addr & bank;
        LogPrintDbg("Bus conflict: rom=%02X, result=%02X\n", *addr, bank);
    }

    bank &= (con->rdesc->chr.size >> id->pShift) - 1;
    LogPrintAssert((uint32_t)(bank << id->pShift) <= con->rdesc->chr.size, "CHR overflow");

    bankBase = con->rdesc->chr.data + (bank << id->pShift);
    ppuMMap->ptMirrTable[0] = bankBase;
    ppuMMap->ptMirrTable[1] = bankBase + PPU_PATTERN_SIZE;
}
