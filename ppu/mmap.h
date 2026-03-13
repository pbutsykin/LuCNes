/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2020 Pavel Butsykin
 */
#ifndef __CNES_PPU_MMAP_
#define __CNES_PPU_MMAP_

#define PPU_NAMETAB_MAX_PAGES 4
#define PPU_PATTERNTAB_MAX_PAGES 2

typedef struct _PPUPaletteTable PPUPaletteTable;
typedef struct _PPUNameTable PPUNameTable;
typedef struct _PPUTile PPUTile;

typedef struct _PPUMMap {
    union {
        struct {
            uint8_t* pattern0; /* $0000-$0FFF */
            uint8_t* pattern1; /* $1000-$1FFF */
        };
        uint8_t* ptMirrTable[PPU_PATTERNTAB_MAX_PAGES];
        PPUTile* ptTileMirrTable[PPU_PATTERNTAB_MAX_PAGES];
    };

    uint8_t* chrRAM;
    uint8_t* chrROM;
    uint32_t chrSize;

    union {
        uint8_t* vram;
        uint8_t* name; /* $2000-$2FFF, base of PPUVRamSpace allocation */
    };

    uint8_t* mirror1;  /* $3000-$3EFF */
    union {
        uint8_t* palette; /* $3F00-$3F1F */
        PPUPaletteTable* paletteTable;
    };
    uint8_t* mirror2;    /* $3F20-$3FFF */

    union {
        uint8_t* nameMirrTable[PPU_NAMETAB_MAX_PAGES];
        PPUNameTable* ntMirrTable[PPU_NAMETAB_MAX_PAGES];
    };
} PPUMMap;

enum {
    PPU_NAME_SCREEN0 = 0,
    PPU_NAME_SCREEN1 = 1,
    PPU_NAME_SCREEN2 = 2,
    PPU_NAME_SCREEN3 = 3,
};

enum {
    PPU_PATTERN0_ADDR = 0x0000,
    PPU_PATTERN1_ADDR = 0x1000,
    PPU_NAME0_ADDR    = 0x2000,
    PPU_NAME1_ADDR    = 0x2400,
    PPU_NAME2_ADDR    = 0x2800,
    PPU_NAME3_ADDR    = 0x2C00,
    PPU_MIRROR1_ADDR  = 0x3000,
    PPU_PALETTE_ADDR  = 0x3F00,
    PPU_MIRROR2_ADDR  = 0x3F20,
    PPU_MAX_ADDR      = 0x3FFF,
};

enum {
    PPU_PATTERN_SIZE = 4096,
    PPU_NAMETAB_SIZE = 1024,
    PPU_ATTRTAB_SIZE = 64,
    PPU_PALETTE_SIZE = 32,
};

#endif /* __CNES_PPU_MMAP_ */
