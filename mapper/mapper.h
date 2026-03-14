/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_
#define __CNES_MAPPER_

#include <connector.h>

typedef struct _PPUMMap PPUMMap;

enum {
    MAP_NROM    = 0,  /* No Mapper - All 32kb ROM + 8kb VROM */
    MAP_MMC1    = 1,  /* Nintendo MMC1 Chipset */
    MAP_UXROM   = 2,  /* ROM (PRG) Switch */
    MAP_CNROM   = 3,  /* VROM (CHR) Switch */
    MAP_MMC3    = 4,  /* Nintendo MMC3 Chipset */
    MAP_MMC5    = 5,  /* Nintendo MMC5 Chipset */
    MAP_F4XXX   = 6,  /* FFE F4XXX Games */ 
    MAP_AXROM   = 7,  /* 32kb ROM (PRG) Switch */
    MAP_F3XXX   = 8,  /* F3XXX Games off the FFE CD-ROM */
    MAP_MMC2    = 9,  /* Nintendo MMC2 Chipset */
    MAP_MMC4    = 10, /* Nintendo MMC4 Chipset */
    MAP_COLORD  = 11, /* Color Dreams Chipset */
    MAP_F6XXX   = 12, /* FFE F6XXX Games */
    MAP_NES015  = 15, /* 100-in-1 Cart Switch */
    MAP_BANDAI  = 16, /* Ban Dai Chipset */
    MAP_F8XXX   = 17, /* FFE F8XXX Games */
    MAP_SS88006 = 18, /* Jaleco SS8806 Chipset */
    MAP_NAMCOT  = 19, /* Namcot 106 Chipset */
    MAP_NES71   = 71, /* Mapper 071 is assigned to games developed by Codemasters and published by Camerica */
};

#define MAPPER_BLK_BITS 10

enum {
    MAPPER_BLK0 = 0 << MAPPER_BLK_BITS,
    MAPPER_BLK1 = 1 << MAPPER_BLK_BITS,

    MAPPER_BLK_MAX = 2 << MAPPER_BLK_BITS,
};

enum PrgBankWin {
    PRG_BANK8K_WIN0 = 0,
    PRG_BANK8K_WIN1 = 1,
    PRG_BANK8K_WIN2 = 2,
    PRG_BANK8K_WIN3 = 3,

    PRG_BANK16K_WIN0 = PRG_BANK8K_WIN0,
    PRG_BANK16K_WIN1 = PRG_BANK8K_WIN2,

    PRG_BANK32K_WIN  = PRG_BANK16K_WIN0,
};

#define PRG_BANK_NO_MASK ((uint32_t)-1)

typedef struct _MapperObj {
    uint8_t  id;
    uint8_t bShift;
    uint8_t pShift;
    char*   label;

    void (*initMirroring)(PPUMMap* mmap, bool vertical);
    void (*bankSwitch)(const MapperObj* mapper, CNesConnector* con, const region_t* prg, const uint8_t* addr, uint8_t val);
} MapperObj;

MapperObj* MapperLookupById(uint8_t id);

void MapperPrgSet32K(MMap* mmap, uint8_t* data, uint32_t mask);

#endif /* __CNES_MAPPER_ */
