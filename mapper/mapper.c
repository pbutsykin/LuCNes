/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_MAPPER
#define MAPPER

#include <utils/utils.h>
#include <cpu/interface.h>
#include <ppu/mmap.h>
#include <rom/rom.h>

#include "mapper.h"
#include "mmc1.h"
#include "cnrom.h"
#include "axrom.h"

#define MAPPER_INIT(_id, _size, _offs, _init, _prg_init, _nm_init, _map_reload) \
    {_id, _size, _offs, #_id, _init, _prg_init, _nm_init, _map_reload}

#define MAPPER_UNDEFINED(_id) \
    {_id, 0xff, 0xff, #_id, NULL, NULL, NULL, NULL}

#define MAP_PARAMS_UNDEFINE 0xff

#define NROM_PRG_WIN_SIZE 15
#define NROM_CHR_WIN_SIZE 0

static void NameTableMirrorInit(MapperObj* _ __maybe_unused, PPUMMap* mmap, bool vertical)
{
    uint8_t* CIRAM = mmap->name;
    uint8_t** nameTab = mmap->nameMirrTable;

    if (vertical) {
        nameTab[PPU_NAME_SCREEN0] = nameTab[PPU_NAME_SCREEN2] = &CIRAM[MAPPER_BLK0];
        nameTab[PPU_NAME_SCREEN1] = nameTab[PPU_NAME_SCREEN3] = &CIRAM[MAPPER_BLK1];
    } else {
        nameTab[PPU_NAME_SCREEN0] = nameTab[PPU_NAME_SCREEN1] = &CIRAM[MAPPER_BLK0];
        nameTab[PPU_NAME_SCREEN2] = nameTab[PPU_NAME_SCREEN3] = &CIRAM[MAPPER_BLK1];
    }
}

static void MapperBankSwitchDefault(MapperObj* mapper, uint16_t cpuAddr __maybe_unused, uint8_t bank)
{
    const MapperId* id = mapper->id;
    const CNesConnector* con = mapper->con;
    const region_t* prg = &con->rdesc->prg;

    if (unlikely(mapper->bank == bank))
        return;

    LogPrintAssert((uint32_t)(bank << id->bShift) <= prg->size, "pool overflow! prg->size: %d, bank: %d\n", prg->size, bank);

    mapper->bank = bank;
    MapperPrgSet32K(CpuMMap(con->cpu), prg->data + (bank << id->bShift), mapper->bankMask);
}

static void MapperPrgBankInitTableDefault(MapperObj* mapper, MMap* mmap, const region_t* prg)
{
    MapperPrgSet32K(mmap, prg->data, mapper->bankMask);
}

static const MapperId MapperList[] = {
    {(uint8_t)-1, (uint8_t)-1, (uint8_t)-1, NULL, NULL, NULL, NULL, NULL},
    MAPPER_INIT(MAP_NROM, NROM_PRG_WIN_SIZE, NROM_CHR_WIN_SIZE, NULL,
                MapperPrgBankInitTableDefault, NameTableMirrorInit, MapperBankSwitchDefault),
    MAPPER_INIT(MAP_MMC1, MMC1_PRG_WIN_SIZE, MMC1_CHR_WIN_SIZE, Mmc1MapperInit,
                Mmc1PrgBankInitTable, Mmc1InitMirroring, Mmc1BankSwitch),
    MAPPER_UNDEFINED(MAP_UXROM),
    MAPPER_INIT(MAP_CNROM, CNROM_PRG_WIN_SIZE, CNROM_CHR_WIN_SIZE, NULL,
                MapperPrgBankInitTableDefault, NameTableMirrorInit, CnRomBankSwitch),
    MAPPER_UNDEFINED(MAP_MMC3),
    MAPPER_UNDEFINED(MAP_MMC5),
    MAPPER_UNDEFINED(MAP_F4XXX),
    MAPPER_INIT(MAP_AXROM, AXROM_PRG_WIN_SIZE, AXROM_CHR_WIN_SIZE, NULL,
                MapperPrgBankInitTableDefault, AxRomNameTableInit, AxRoomMapperBankSwitch),
    MAPPER_UNDEFINED(MAP_F3XXX),
    MAPPER_UNDEFINED(MAP_MMC2),
    MAPPER_UNDEFINED(MAP_MMC4),
    MAPPER_UNDEFINED(MAP_COLORD),
    MAPPER_UNDEFINED(MAP_F6XXX),
    MAPPER_UNDEFINED(MAP_NES015),
    MAPPER_UNDEFINED(MAP_BANDAI),
    MAPPER_UNDEFINED(MAP_F8XXX),
    MAPPER_UNDEFINED(MAP_SS88006),
    MAPPER_UNDEFINED(MAP_NAMCOT),
    MAPPER_UNDEFINED(MAP_NES71)
};

static void MapperDebugInfo(const MapperId* mId __maybe_unused)
{
    LogPrintDbg("mapper info:\n");
    PRINT_FIELD_UINT32(mId->id);
    PRINT_FIELD_UINT32(mId->bShift);
    PRINT_FIELD_UINT32(mId->pShift);
    PRINT_FIELD_STRING(mId->label);
}

static inline int8_t MapperCmpId(uint8_t id1, uint8_t id2)
{
    return (id1 < id2) ? -1 : (id1 > id2) ? 1 : 0;
}

/* Binary search */
/* There is no real reason to use a binary search here, but I like this bitwise implementation,
 * so let it be.
 */
static const MapperId* MapperLookupById(uint8_t id)
{
    uint8_t end = sizeof(MapperList) / sizeof(MapperId);
    uint8_t range = end >> 1, rest;
    uint8_t idx = range;

    do {
        const MapperId* mId = &MapperList[idx];
        int8_t ret = MapperCmpId(id, mId->id);
        if (!ret) {
            MapperDebugInfo(mId);
            if (mId->bShift == MAP_PARAMS_UNDEFINE)
                return NULL;

            return mId;
        } else if(!range)
            break;

        rest = range & 1;
        range >>= 1;
        idx += IS_NEG_INT8(ret) ? -(range + rest) : (range + rest);
    } while (idx < end);

    LogPrintErr("Mapper id = %u - not found\n", id);
    return NULL;
}

void MapperPrgSet16K(MMap* mmap, enum PrgBankWin idx, uint8_t* data)
{
    LogPrintAssert(idx == PRG_BANK16K_WIN0 || idx == PRG_BANK16K_WIN1,
                   "Invalid prg 16k bank index: %d\n", idx);

    mmap->prgBankTable[idx] = data;
    mmap->prgBankTable[idx + 1] = data + KB(8);
}

void MapperPrgSet32K(MMap* mmap, uint8_t* data, uint32_t mask)
{
    uint8_t** prgTab = mmap->prgBankTable;

    prgTab[PRG_BANK32K_WIN] = data;
    prgTab[PRG_BANK32K_WIN + 1] = data + (KB(8)  & mask);
    prgTab[PRG_BANK32K_WIN + 2] = data + (KB(16) & mask);
    prgTab[PRG_BANK32K_WIN + 3] = data + (KB(24) & mask);
}

void MapperPrgBankInitTable(MapperObj* mapper, MMap* mmap, const region_t* prg)
{
    uint32_t bankSize = 1U << mapper->id->bShift;

    mapper->bankMask = MIN(prg->size - 1, bankSize - 1);

    LogPrintAssert(bankSize == KB(16) || bankSize == KB(32), "Invalid bank size: %u\n", bankSize);

    mapper->id->initPrgBankTable(mapper, mmap, prg);
}

void MapperPrgBankSwitch(MapperObj* mapper, uint16_t cpuAddr, uint8_t val)
{
    LogPrintDbg("Update bank: %d <- %d\n",  mapper->bank, val);

    mapper->id->bankSwitch(mapper, cpuAddr, val);
}

void MapperInitMirroring(MapperObj* mapper, PPUMMap* mmap, bool vertical)
{
    return mapper->id->initMirroring(mapper, mmap, vertical);
}

MapperObj* MapperInit(uint8_t id, CNesConnector* con)
{
    MapperObj* mapper;
    const MapperId* mId = MapperLookupById(id);
    if (!mId)
        return NULL;

    mapper = mId->init ? mId->init() : MemAlloc(sizeof(*mapper));
    *mapper = (MapperObj) {
        .id = mId,
        .con = con,
    };
    return mapper;
}

void MapperFree(MapperObj* mapper)
{
    MemFree(mapper);
}
