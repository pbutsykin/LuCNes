/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2016 Pavel Butsykin
 */
#ifndef __CNES_PPU_
#define __CNES_PPU_

typedef struct _CNesConnector CNesConnector;

#include "mmap.h"

#define PPU_TILE_COLUMN_BIT 3
#define PPU_TILE_ROW_BIT 3

#define PPU_FRAME_WIDTH  256
#define PPU_FRAME_HEIGHT 240

#define PPU_IDLE_CYCLES 1
#define PPU_CYCLES_PER_LINE 340
#define PPU_PIXELS_PER_LINE 256

#define PPU_MAX_PAGES 16

enum {
    PPU_INCREMENT_MODE_SHIFT  = 2,
    PPU_SPRITE_TILE_SHIFT     = 3,
    PPU_BACKGROUND_TILE_SHIFT = 4,
    PPU_SPRITE_HEIGHT_SHIFT   = 5,
    PPU_MS_SELECTION_SIFT     = 6, /* unused */
    PPU_NMI_SHIFT             = 7
}; /* PPU_CTRL_BITS */

enum {
    PPU_NAMETABLE_ADDR_MASK  = 1 | 2,
    PPU_INCREMENT_MODE_MASK  = 1 << PPU_INCREMENT_MODE_SHIFT,
    PPU_SPRITE_TILE_MASK     = 1 << PPU_SPRITE_TILE_SHIFT,
    PPU_BACKGROUND_TILE_MASK = 1 << PPU_BACKGROUND_TILE_SHIFT,
    PPU_SPRITE_HEIGHT_MASK   = 1 << PPU_SPRITE_HEIGHT_SHIFT,
    PPU_NMI_MASK             = 1 << PPU_NMI_SHIFT
}; /* PPU_CTRL_MASK */

enum {
    PPU_GREYSCALE_SHIFT              = 0,
    PPU_BACKGROUND_LEFT_ENABLE_SHIFT = 1,
    PPU_SPRITE_LEFT_ENABLE_SHIFT     = 2,
    PPU_BACKGROUND_ENABLE_SHIFT      = 3,
    PPU_SPRITE_ENABLE_SHIFT          = 4,
    PPU_EMPHAS_RED_SHIFT             = 5,
    PPU_EMPHAS_GREEN_SHIFT           = 6,
    PPU_EMPHAS_BLUE_SHIFT            = 7
}; /* PPU_MASK_BITS */

#define PPU_MASK_LEFTMOST_PIXELS 8

enum {
    PPU_GREYSCALE_MASK              = 1 << PPU_GREYSCALE_SHIFT,
    PPU_BACKGROUND_LEFT_ENABLE_MASK = 1 << PPU_BACKGROUND_LEFT_ENABLE_SHIFT,
    PPU_SPRITE_LEFT_ENABLE_MASK     = 1 << PPU_SPRITE_LEFT_ENABLE_SHIFT,
    PPU_BACKGROUND_ENABLE_MASK      = 1 << PPU_BACKGROUND_ENABLE_SHIFT,
    PPU_SPRITE_ENABLE_MASK          = 1 << PPU_SPRITE_ENABLE_SHIFT
}; /* PPU_MASK_MASK */

#define PPU_RENDERING_ENABLED (PPU_BACKGROUND_ENABLE_MASK | PPU_SPRITE_ENABLE_MASK)

enum {
    PPU_VBLANK_READ_HW_RACE   = 0, /* This flag doesn’t exist. Let's use this bit for emulating
                                    * race condition hardware bug on reading the status register.
                                    * https://www.nesdev.org/wiki/PPU_registers#Vblank_flag
                                    */
    PPU_NMI_PENDING_SHIFT      = 1, /* This flag doesn’t exist. Let's use this bit for emulating
                                     * pending NMI.
                                     */

    PPU_SPRITE_OVERFLOW_SHIFT = 5,
    PPU_SPRITE0_SHIFT         = 6,
    PPU_VBLANK_SHIFT          = 7
}; /* PPU_STATUS_BITS */

enum {
    PPU_VBLANK_READ_RC_MASK =  1 << PPU_VBLANK_READ_HW_RACE,
    PPU_NMI_PENDING_MASK    =  1 << PPU_NMI_PENDING_SHIFT,

    PPU_SPRITE_OVERFLOW_MASK = 1 << PPU_SPRITE_OVERFLOW_SHIFT,
    PPU_SPRITE0_MASK         = 1 << PPU_SPRITE0_SHIFT,
    PPU_VBLANK_MASK          = 1 << PPU_VBLANK_SHIFT
}; /* PPU_STATUS_MASK */

typedef struct _PPUMTab {
    uint8_t* (*ppuAddrLookup)(void* ctx, const uint16_t addr);
    void* ctx;
} PPUMTab;

typedef struct _PPUReg {
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    struct {
        uint8_t addr;
        uint8_t data;
    } oam;
    uint8_t scroll;
    uint8_t addr;
    uint8_t data;
} __attribute__((packed)) PPUReg;

typedef union _VRAMAddrReg {
    uint16_t value;
    union {
        struct {
            uint16_t tileX:5;
            uint16_t tileY:5;
            uint16_t nt:2;
            uint16_t fineY:3;
        };
        struct {
            uint16_t _:10;
            uint16_t ntX:1;
            uint16_t ntY:1;
        };
    };

    uint16_t addr;
    struct {
        uint16_t low:8;
        uint16_t high:6;
        uint16_t Z:1;
    };
} VRAMAddrReg;

typedef struct _SpriteObj SpriteObj;
typedef struct _PPUConfig PPUConfig;

typedef struct _CNesPPU {
    PPUReg* reg;
    const PPUConfig* cfg;
    PPUMMap mmap;
    PPUMTab mtab[PPU_MAX_PAGES];
    union {
        uint8_t* oam; /* Object Attribute Memory */
        SpriteObj* sprites;
    };
    struct _InternalRegs {
        VRAMAddrReg cur; /* Current VRAM address (15 bits) */
        VRAMAddrReg tmp; /* Temporary VRAM address (15 bits) */
        uint8_t fineX;
        uint8_t w;
    } iRegs;

    uint16_t scanLine, dot;
    bool oddFrame;
    struct _PPURender {
        uint16_t lastDot;
        uint16_t rendToggleDot; /* PPU dot when rendering was last toggled */
        uint8_t lineSprites[PPU_PIXELS_PER_LINE];
        uint16_t lastTileX;
        struct {
            bool hit;
            uint8_t x;
        } sp0;
    } render;

    VideoBackend* video;
    CNesConnector* con;
} LuCNesPPU;

/* OAM contains list of up to 64 sprites, where each sprite's information occupies 4 bytes. */
#define PPU_OAM_SIZE 0x100

#endif
