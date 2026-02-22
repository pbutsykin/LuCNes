/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_ROM_
#define __CNES_ROM_


typedef enum {
    MIRRORING_PPU_NAMETAB_HORIZONTAL = 0,
    MIRRORING_PPU_NAMETAB_VERTICAL   = 1,
} __attribute__((packed)) MirroringPPUType;

typedef struct _RomDesc {
    region_t trainer;
    region_t prg;
    region_t chr;
    region_t pc10;
    region_t title;

    uint16_t mapperId;
    uint8_t  submapper;
    uint16_t chrRamSize;
    MirroringPPUType mirrorType;
    bool sram;

    MFile* rfile;
} RomDesc;

RomDesc* LoadRomFile(char* fname);
void UnloadRomFile(RomDesc* rdesc);

#endif /* __CNES_ROM_ */
