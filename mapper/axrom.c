/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#define CNES_MAPPER_AXROM
#define MAPPER

#include <utils/utils.h>
#include <cpu/interface.h>
#include <ppu/interface.h>
#include <rom/rom.h>
#include <connector.h>

#include "mapper.h"

#define CIRAM_BLK_SWITCH_MASK 0x10
#define PRG_ROM_BANK_MASK 0x7

/*
 * https://www.nesdev.org/wiki/NES_2.0_submappers#007:_0,_1,_2_AxROM
 */
enum AxRomSubmapper {
    AXROM_SUBMAPPER_DEFAULT      = 0,
    AXROM_SUBMAPPER_NO_CONFLICT  = 1,
    AXROM_SUBMAPPER_BUS_CONFLICT = 2,
};

static inline void SingleScreenBlkSwitch(uint8_t CIRAM[MAPPER_BLK_MAX],
                                         uint8_t* nameTab[PPU_NAMETAB_MAX_PAGES], uint8_t blkN)
{
    nameTab[PPU_NAME_SCREEN0] = nameTab[PPU_NAME_SCREEN1] =
    nameTab[PPU_NAME_SCREEN2] = nameTab[PPU_NAME_SCREEN3] = &CIRAM[blkN << MAPPER_BLK_BITS];
}

void AxRomNameTableInit(PPUMMap* mmap, bool _ __maybe_unused)
{
    SingleScreenBlkSwitch(mmap->name, mmap->nameMirrTable, MAPPER_BLK0 >> MAPPER_BLK_BITS);
}

void AxRoomMapperBankSwitch(const MapperObj* mapper, CNesConnector* con, const region_t* prg,
                            const uint8_t* addr, uint8_t bank)
{
    PPUMMap* ppuMMap = PpuMMap(con->ppu);

    if (con->rdesc->submapper == AXROM_SUBMAPPER_BUS_CONFLICT) {
        /* Bus conflict: the written value is ANDed with the ROM value at the write address. */
        bank = *addr & bank;
        LogPrintDbg("Bus conflict: rom=%02X, result=%02X\n", *addr, bank);
    }

    con->pins.CIRAM_A10 = !!(bank & CIRAM_BLK_SWITCH_MASK);
    SingleScreenBlkSwitch(ppuMMap->name, ppuMMap->nameMirrTable, con->pins.CIRAM_A10);
    bank &= PRG_ROM_BANK_MASK;

    LogPrintAssert((uint32_t)(bank << mapper->bShift) <= prg->size, "PRG overflow");
    LogPrintDbg("NTable switch to %u, prg: %x\n", con->pins.CIRAM_A10, (bank << mapper->bShift));

    MapperPrgSet32K(CpuMMap(con->cpu), prg->data + (bank << mapper->bShift), PRG_BANK_NO_MASK);
}
