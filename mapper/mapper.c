/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_MAPPER
#define MAPPER

#include <utils/utils.h>
#include <ppu/mmap.h>

#include "mapper.h"
#include "axrom.h"
#include "cnrom.h"

#define MAPPER_INIT(_id, _size, _offs, _nm_init, _map_reload) \
    {_id, _size, _offs, #_id, _nm_init, _map_reload}

#define MAPPER_UNDEFINED(_id) \
    {_id, 0xff, 0xff, #_id, NULL, NULL}

#define MAP_PARAMS_UNDEFINE 0xff

#define NROM_PRG_WIN_SIZE 15
#define NROM_CHR_WIN_SIZE 0

static void NameTableMirrorInit(PPUMMap* mmap, bool vertical)
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

static uint8_t* MapperBankSwitchDefault(const MapperObj* mapper, CNesConnector* con __maybe_unused,
                                        const region_t* reg, const uint8_t* addr __maybe_unused, uint8_t bank)
{
    LogPrintAssert((uint32_t)(bank << mapper->bShift) <= reg->size, "pool overflow! reg->size: %d, bank: %d\n", reg->size, bank);
    return reg->data + (bank << mapper->bShift);
}

static MapperObj MapperList[] = {
    {(uint8_t)-1, (uint8_t)-1, (uint8_t)-1, NULL, NULL, NULL},
    MAPPER_INIT(MAP_NROM, NROM_PRG_WIN_SIZE, NROM_CHR_WIN_SIZE, NameTableMirrorInit, MapperBankSwitchDefault),
    MAPPER_UNDEFINED(MAP_MMC1),
    MAPPER_UNDEFINED(MAP_UXROM),
    MAPPER_INIT(MAP_CNROM, CNROM_PRG_WIN_SIZE, CNROM_CHR_WIN_SIZE, NameTableMirrorInit, CnRomBankSwitch),
    MAPPER_UNDEFINED(MAP_MMC3),
    MAPPER_UNDEFINED(MAP_MMC5),
    MAPPER_UNDEFINED(MAP_F4XXX),
    MAPPER_INIT(MAP_AXROM, AXROM_PRG_WIN_SIZE, AXROM_CHR_WIN_SIZE, AxRomNameTableInit, AxRoomMapperBankSwitch),
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

static void MapperDebugInfo(__maybe_unused MapperObj* mObj)
{
    LogPrintDbg("mapper info:\n");
    PRINT_FIELD_UINT32(mObj->id);
    PRINT_FIELD_UINT32(mObj->bShift);
    PRINT_FIELD_UINT32(mObj->pShift);
    PRINT_FIELD_STRING(mObj->label);
}

static inline int8_t MapperCmpId(uint8_t id1, uint8_t id2)
{
    return (id1 < id2) ? -1 : (id1 > id2) ? 1 : 0;
}

/* Binary search */
MapperObj* MapperLookupById(uint8_t id)
{
    uint8_t end = sizeof(MapperList) / sizeof(MapperObj);
    uint8_t range = end >> 1, rest;
    uint8_t idx = range;

    do {
        MapperObj* mObj = &MapperList[idx];
        int8_t ret = MapperCmpId(id, mObj->id);
        if (!ret) {
            MapperDebugInfo(mObj);
            if (mObj->bShift == MAP_PARAMS_UNDEFINE)
                return NULL;

            return mObj;
        } else if(!range)
            break;

        rest = range & 1;
        range >>= 1;
        idx += IS_NEG_INT8(ret) ? -(range + rest) : (range + rest);
    } while (idx < end);

    LogPrintErr("Mapper id = %u - not found\n", id);
    return NULL;
}

uint16_t MapperPrgOffsetMask(const MapperObj* mapper, uint32_t prgSize)
{
    uint32_t bankSize = 1U << mapper->bShift;

    LogPrintAssert(bankSize == KB(16) || bankSize == KB(32), "Invalid bank size: %u\n", bankSize);

    /* If the PRG ROM is smaller than the window, we should mirror the upper address range. */
    return bankSize > prgSize ? prgSize - 1 : bankSize - 1;
}

uint8_t* MapperPrgBankSwitch(CNesConnector* con, const region_t* prg, const uint8_t* addr, uint8_t val)
{
    const MapperObj* mapper = con->mapper;

    return mapper->bankSwitch(mapper, con, prg, addr, val);
}

void MapperInitMirroring(const MapperObj* mapper, PPUMMap* mmap, bool vertical)
{
    return mapper->initMirroring(mmap, vertical);
}
