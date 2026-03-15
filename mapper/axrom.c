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

void AxRomNameTableInit(MapperObj* _ __maybe_unused, PPUMMap* mmap, bool __ __maybe_unused)
{
    SingleScreenBlkSwitch(mmap->name, mmap->nameMirrTable, MAPPER_BLK0 >> MAPPER_BLK_BITS);
}

void AxRoomMapperBankSwitch(MapperObj* mapper, uint16_t cpuAddr, uint8_t bank)
{
    const MapperId* id = mapper->id;
    CNesConnector* con = mapper->con;
    const region_t* prg = &con->rdesc->prg;
    const uint32_t prgMask = prg->size - 1;
    PPUMMap* ppuMMap = PpuMMap(con->ppu);
    MMap* cpuMMap = CpuMMap(con->cpu);

    if (con->rdesc->submapper == AXROM_SUBMAPPER_BUS_CONFLICT) {
        uint8_t* addr = MMapPrgResolve(cpuMMap, cpuAddr);
        /* Bus conflict: the written value is ANDed with the ROM value at the write address. */
        bank = *addr & bank;
        LogPrintDbg("Bus conflict: rom=%02X, result=%02X\n", *addr, bank);
    }

    if (unlikely(mapper->bank == bank))
        return;

    mapper->bank = bank;
    con->pins.CIRAM_A10 = !!(bank & CIRAM_BLK_SWITCH_MASK);
    SingleScreenBlkSwitch(ppuMMap->name, ppuMMap->nameMirrTable, con->pins.CIRAM_A10);

    LogPrintAssert(IsPowerOf2(prg->size), "Invalid prg size for AxROM: must be a power of 2.\n");
    LogPrintDbg("NTable switch to %u, prg: %x (shift: %x)\n", con->pins.CIRAM_A10,
                (bank << id->bShift) & prgMask, id->bShift);

    MapperPrgSet32K(cpuMMap, prg->data + ((bank << id->bShift) & prgMask), PRG_BANK_NO_MASK);
}
