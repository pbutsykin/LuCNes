/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Pavel Butsykin
 */
#define CNES_MAPPER_MMC1
#define MAPPER

#include <utils/utils.h>
#include <cpu/interface.h>
#include <ppu/interface.h>
#include <rom/rom.h>
#include <connector.h>

#include "mapper.h"

/*
 * https://www.nesdev.org/wiki/MMC1
 */
#define MMC1_SR_RESET 0x10

enum Mmc1Reg {
    MMC1_REG_CONTROL = 0,
    MMC1_REG_CHR0    = 1,
    MMC1_REG_CHR1    = 2,
    MMC1_REG_PRG     = 3,
};

enum Mmc1PrgBankMode {
    MMC1_PRG_32K_MODE0 = 0,
    MMC1_PRG_32K_MODE1 = 1,
    MMC1_PRG_16K_FIX_FIRST = 2,
    MMC1_PRG_16K_FIX_LAST  = 3,
};

enum Mmc1Mirror {
    MMC1_MIRROR_SINGLE_LO  = 0,
    MMC1_MIRROR_SINGLE_HI  = 1,
    MMC1_MIRROR_VERTICAL   = 2,
    MMC1_MIRROR_HORIZONTAL = 3,
};

typedef struct _MMC1MapperObj {
    MapperObj base;
    uint8_t shiftReg;
    union {
        uint8_t val;
        struct {
            uint8_t nameTab:2;
            uint8_t prgMode:2;
            uint8_t chrMode4K:1;
        };
    } control;
    union {
        uint8_t chrBank0;
        struct {
            uint8_t unused:1;
            uint8_t chrBank8K:4;
        };
    };
    uint8_t chrBank1;
    union {
        uint8_t val;
        uint8_t bank16:4;
        struct {
            uint8_t unused0:1;
            uint8_t bank32:3;
        };
    } prg;
} MMC1MapperObj;

typedef struct _LoadReg {
    union {
        uint8_t val;
        struct {
            uint8_t data:1;
            uint8_t unused:6;
            uint8_t reset:1;
        };
    };
} LoadReg;

static void Mmc1UpdatePrgMap(MMC1MapperObj* mmc1, MMap* mmap, const region_t* prg)
{
    const uint32_t prgMask = prg->size - 1;

    switch (mmc1->control.prgMode) {
        case MMC1_PRG_32K_MODE0:
        case MMC1_PRG_32K_MODE1: {
            uint32_t bankOffs = (mmc1->prg.bank32 << PRG_BANK_32K_SHIFT) & prgMask;
            MapperPrgSet32K(mmap, prg->data + bankOffs, PRG_BANK_NO_MASK);
            break;
        }
        case MMC1_PRG_16K_FIX_FIRST: {
            uint32_t bankOffs = (mmc1->prg.bank16 << PRG_BANK_16K_SHIFT) & prgMask;
            MapperPrgSet16K(mmap, PRG_BANK16K_WIN0, prg->data);
            MapperPrgSet16K(mmap, PRG_BANK16K_WIN1, prg->data + bankOffs);
            break;
        }
        case MMC1_PRG_16K_FIX_LAST: {
            uint32_t bankOffs = (mmc1->prg.bank16 << PRG_BANK_16K_SHIFT) & prgMask;
            MapperPrgSet16K(mmap, PRG_BANK16K_WIN0, prg->data + bankOffs);
            MapperPrgSet16K(mmap, PRG_BANK16K_WIN1, prg->data + prg->size - KB(16));
            break;
        }
    }
}

static void Mmc1UpdateMirroring(MMC1MapperObj* mmc1, PPUMMap* mmap)
{
    switch (mmc1->control.nameTab) {
        case MMC1_MIRROR_SINGLE_LO:
            MapperNameTableSingleScreenSwitch(mmap, MAPPER_BLK0 >> MAPPER_BLK_BITS);
            break;
        case MMC1_MIRROR_SINGLE_HI:
            MapperNameTableSingleScreenSwitch(mmap, MAPPER_BLK1 >> MAPPER_BLK_BITS);
            break;
        case MMC1_MIRROR_VERTICAL:
            MapperNameTableMirrorSwitch(mmap, true);
            break;
        case MMC1_MIRROR_HORIZONTAL:
            MapperNameTableMirrorSwitch(mmap, false);
            break;
    }
}

static void Mmc1UpdateChr(MMC1MapperObj* mmc1, const CNesConnector* con)
{
    PPUMMap* mmap = PpuMMap(con->ppu);
    const region_t* chr = &con->rdesc->chr;
    uint8_t* data = chr->size ? chr->data : mmap->chrRAM;
    const uint32_t chrMask = (chr->size ?: con->rdesc->chrRamSize) - 1;

    if (mmc1->control.chrMode4K) {
        mmap->ptMirrTable[0] = data + ((mmc1->chrBank0 << CHR_BANK_4K_SHIFT) & chrMask);
        mmap->ptMirrTable[1] = data + ((mmc1->chrBank1 << CHR_BANK_4K_SHIFT) & chrMask);
    } else { /* 8K mode */
        mmap->ptMirrTable[0] = data + ((mmc1->chrBank8K << CHR_BANK_8K_SHIFT) & chrMask);
        mmap->ptMirrTable[1] = mmap->ptMirrTable[0] + PPU_PATTERN_SIZE;
    }
}

void Mmc1NameTableInit(MapperObj* base, PPUMMap* ppuMMap, bool vertical)
{
    MMC1MapperObj* mapper = CONTAINER_OF(base, MMC1MapperObj, base);

    mapper->control.nameTab = vertical ? MMC1_MIRROR_VERTICAL : MMC1_MIRROR_HORIZONTAL;
    MapperNameTableMirrorSwitch(ppuMMap, vertical);
}

void Mmc1PrgInit(MapperObj* base, MMap* mmap, const region_t* prg)
{
    MMC1MapperObj* mapper = CONTAINER_OF(base, MMC1MapperObj, base);

    Mmc1UpdatePrgMap(mapper, mmap, prg);
}

static void Mmc1UpdatePrg(MMC1MapperObj* mmc1, const CNesConnector* con)
{
    Mmc1UpdatePrgMap(mmc1, CpuMMap(con->cpu), &con->rdesc->prg);
}

void Mmc1BankSwitch(MapperObj* base, uint16_t cpuAddr, uint8_t val)
{
    MMC1MapperObj* mapper = CONTAINER_OF(base, MMC1MapperObj, base);
    const CNesConnector* con = base->con;
    const LoadReg lReg = { .val = val, };
    bool shiftRegFilled;

    if (lReg.reset) {
        mapper->shiftReg = MMC1_SR_RESET;
        mapper->control.prgMode = MMC1_PRG_16K_FIX_LAST;
        Mmc1UpdatePrg(mapper, con);
        return;
    }

    shiftRegFilled = mapper->shiftReg & 1;
    mapper->shiftReg = (mapper->shiftReg >> 1) | (lReg.data << 4);
    if (likely(!shiftRegFilled))
        return;

    switch ((cpuAddr & PRG_BANK_32K_MASK) >> PRG_BANK_SHIFT) {
        case MMC1_REG_CONTROL:
            mapper->control.val = mapper->shiftReg;
            Mmc1UpdateMirroring(mapper, PpuMMap(con->ppu));
            Mmc1UpdateChr(mapper, con);
            Mmc1UpdatePrg(mapper, con);
            break;
        case MMC1_REG_CHR0:
            mapper->chrBank0 = mapper->shiftReg;
            Mmc1UpdateChr(mapper, con);
            break;
        case MMC1_REG_CHR1:
            mapper->chrBank1 = mapper->shiftReg;
            Mmc1UpdateChr(mapper, con);
            break;
        case MMC1_REG_PRG:
            mapper->prg.val = mapper->shiftReg;
            Mmc1UpdatePrg(mapper, con);
            break;
    }
    LogPrintDbg("MMC1: shiftReg=%x ctrl=%x chr=%x/%x prg=%x\n", mapper->shiftReg,
                mapper->control.val, mapper->chrBank0, mapper->chrBank1, mapper->prg.val);
    mapper->shiftReg = MMC1_SR_RESET;
}

MapperObj* Mmc1MapperInit(void)
{
    MMC1MapperObj* mapper = MemAlloc(sizeof(*mapper));

    *mapper = (MMC1MapperObj) {
        .shiftReg = MMC1_SR_RESET,
        .control.prgMode = MMC1_PRG_16K_FIX_LAST,
    };
    return &mapper->base;
}
