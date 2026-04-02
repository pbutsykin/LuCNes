/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#define CNES_PPU
#define PPU_RENDER

#include <utils/utils.h>
#include <video/interface.h>

#include "ppu.h"

#define PPU_TILE_COLUMN_NUM (1 << PPU_TILE_COLUMN_BIT)
#define PPU_TILE_ROW_NUM    (1 << PPU_TILE_ROW_BIT)
#define PPU_TILE_ROW16_NUM  (PPU_TILE_ROW_NUM << 1)
#define PPU_LINE_TILES      (PPU_FRAME_WIDTH >> PPU_TILE_COLUMN_BIT)

#define PPU_COLORS_PER_PALETTE 4
#define PPU_BACKGROUND_PALETTE_NUM 4
#define PPU_SPRITE_PALETTE_NUM 4

#define PPU_SPRITES_PER_LINE 8

enum {
    TILE_PER_ROW_BIT = 5,
    TILE_PER_ROW = (1 << TILE_PER_ROW_BIT),
    TILE_PER_ROW_MASK = TILE_PER_ROW - 1,
    TILE_COLUMN_MASK = PPU_TILE_COLUMN_NUM - 1,
    TILE_ROW_MASK = PPU_TILE_ROW_NUM - 1,
    PPU_TILE_ROW16_MASK = PPU_TILE_ROW16_NUM - 1,
}; /* Tile constants */

typedef struct _PPUTile {
    uint8_t low[PPU_TILE_ROW_NUM];
    uint8_t high[PPU_TILE_ROW_NUM];
} __attribute__((packed)) PPUTile;

typedef struct _PPUPalette {
    struct {
        uint8_t value:6;
        uint8_t unused:2;
    } colors[PPU_COLORS_PER_PALETTE];
} __attribute__((packed)) PPUPalette;

typedef struct _PPUPaletteTable {
    union {
        uint8_t uniBgColor;
        PPUPalette bgPallets[PPU_BACKGROUND_PALETTE_NUM];
    };
    PPUPalette spPallets[PPU_SPRITE_PALETTE_NUM];
} __attribute__((packed)) PPUPaletteTable;

typedef struct _PPUNameTable {
    uint8_t tiles[PPU_NAMETAB_SIZE - PPU_ATTRTAB_SIZE];
    uint8_t attrs[PPU_ATTRTAB_SIZE];
} __attribute__((packed)) PPUNameTable;

typedef struct _SpriteObj {
    uint8_t y;
    union {
        uint8_t idx8x8;
        struct {
            uint8_t ptNum:1;   /* bit 0 of tile byte selects pattern table. */
            uint8_t idx8x16:7; /* bits 1-7 give the top tile index. */
        };
    } tile;
    union {
        uint8_t attr;
        struct {
            uint8_t paletteIdx:2;
            uint8_t unimplemented:3; /* The three unimplemented bits of each sprite's byte 2 do not
                                      * exist in the PPU and always read back as 0 */
            enum {
                SPRITE_PRIORITY_FOREGROUND = 0,
                SPRITE_PRIORITY_BACKGROUND = 1,
            } __attribute__((packed)) priority:1;
            uint8_t flipH:1;    /* Flip sprite horizontally */
            uint8_t flipV:1;    /* Flip sprite vertically */
        };
    };
    uint8_t x;
} __attribute__((packed)) SpriteObj;

#define PPU_SPRITE_SIZE_MASK 3
#define PPU_SPRITE_SIZE_BIT  2
#define PPU_SPRITE_SIZE (1<< PPU_SPRITE_SIZE_BIT)

StaticAssert(sizeof(SpriteObj) == PPU_SPRITE_SIZE, "Sprite object size cannot be changed.");

inline static uint8_t GetPixelColorIdx(uint8_t high, uint8_t low, uint8_t shift)
{
    return (((high >> shift) & 1) << 1) | ((low >> shift) & 1);
}

inline static uint8_t GetTilePixel(const PPUTile* tile, const uint8_t row, const uint8_t shift)
{
    return GetPixelColorIdx(tile->high[row], tile->low[row], shift);
}

static inline uint8_t attrIdxFromNameTableIdx(const uint16_t ntIdx)
{
#define TILES_PER_ATTR_BLOCK_ROW 128
#define ATTR_BLOCK_ROW_MASK ~(TILES_PER_ATTR_BLOCK_ROW - 1)
#define ATTR_BLOCK_ROW_BIT 4
#define ATTR_BLOCK_COLUMN_BIT 2

    return ((ntIdx & ATTR_BLOCK_ROW_MASK) >> ATTR_BLOCK_ROW_BIT) +
           ((ntIdx & TILE_PER_ROW_MASK) >> ATTR_BLOCK_COLUMN_BIT);
}

static inline uint8_t PaletteIdxFromAttr(const uint8_t attr, const uint16_t ntIdx)
{
#define TILES_ATTR_BLOCK_ROW_MASK  (TILES_PER_ATTR_BLOCK_ROW - 1)
#define TILES_METATILE_COLUMN_MASK 3

#define METATILE_V_BIT 6
#define METATILE_H_BIT 1
#define METATILE_MASK  3 /* 1 attribute has 4 meta tiles, 0b11 mask is used to take one */

    const uint8_t metaTileIdx = (((ntIdx & TILES_ATTR_BLOCK_ROW_MASK) >> METATILE_V_BIT) << 1) |
                                ((ntIdx & TILES_METATILE_COLUMN_MASK) >> METATILE_H_BIT);
    const uint8_t attrShift = metaTileIdx << 1;

    return (attr >> attrShift) & METATILE_MASK;
}

static inline uint8_t getBgPaletteIdx(const PPUNameTable* nt, uint16_t const ntIdx)
{
    const uint8_t atrIdx = attrIdxFromNameTableIdx(ntIdx);

    return PaletteIdxFromAttr(nt->attrs[atrIdx], ntIdx);
}

static inline PPUPalette* LookupBgPalette(const LuCNesPPU* ppu, const uint8_t idx)
{
    LogPrintAssert(idx < PPU_BACKGROUND_PALETTE_NUM, "Invalid back ground palette index: %u\n", idx);

    return &ppu->mmap.paletteTable->bgPallets[idx];
}

static inline PPUPalette* LookupSpPalette(const LuCNesPPU* ppu, const uint8_t idx)
{
    LogPrintAssert(idx < PPU_SPRITE_PALETTE_NUM, "Invalid sprite palette index: %u\n", idx);

    return &ppu->mmap.paletteTable->spPallets[idx];
}

/* Toggling rendering takes effect approximately 3-4 dots after the write.
 * https://www.nesdev.org/wiki/PPU_registers#PPUMASK
 */
static inline bool PpuRenderingEnabled(const LuCNesPPU* ppu)
{
    const uint16_t currentDot = ppu->render.lastDot;

    if (likely(!ppu->render.rendToggleDot) || currentDot - ppu->render.rendToggleDot > 3)
        return ppu->reg->mask & PPU_RENDERING_ENABLED;

    LogPrintDbg("Rendering toggle suppressed. Delay: %d\n", currentDot - ppu->render.rendToggleDot);

    return !(ppu->reg->mask & PPU_RENDERING_ENABLED);
}

static inline void SpriteZeroHitUpdate(LuCNesPPU* ppu, uint8_t pixelX)
{
    if (ppu->render.sp0.hit || (ppu->reg->status & PPU_SPRITE0_MASK))
        return;

    /* https://wiki.nesdev.com/w/index.php?title=PPU_OAM#Sprite_zero_hits
     * Sprite 0 hit does not happen:
     * At x=255, for an obscure reason related to the pixel pipeline.
     */
    if (pixelX == 255)
        return;

    LogPrintAssert(pixelX >= PPU_MASK_LEFTMOST_PIXELS ||
                  ((ppu->reg->mask & PPU_BACKGROUND_LEFT_ENABLE_MASK) &&
                   (ppu->reg->mask & PPU_SPRITE_LEFT_ENABLE_MASK)),
                "Sprite 0 hit does not happen: At x=0 to x=7 if the left-side clipping window is enabled.");

    ppu->render.sp0.hit = true;
    ppu->render.sp0.x = pixelX;
    LogPrintDbg("SpriteZeroHit detected at x = %u\n", pixelX);
}

typedef struct _PixelSpDataSet {
    union {
        struct {
            uint8_t colorIdx:2;
            uint8_t paletteIdx:2;
            uint8_t sp0:1;
            uint8_t fgFlag:1;
            uint8_t unused:2;
        };
        uint8_t byte;
    };
} __attribute__((packed)) PixelSpDataSet;

typedef struct _PixelBgDataSet {
    uint8_t colorIdx;
    uint8_t paletteIdx;
} PixelBgDataSet;

static inline void IRegIncX(VRAMAddrReg* current)
{
    uint8_t tileX = current->tileX + 1;

    current->tileX = tileX & TILE_PER_ROW_MASK;
    current->ntX ^= !!(tileX & TILE_PER_ROW);
}

static inline void IRegIncY(VRAMAddrReg* current)
{
    uint8_t fineY = current->fineY + 1;

    if (unlikely(fineY & PPU_TILE_ROW_NUM)) {
        uint8_t tileY = (current->tileY + 1) & TILE_PER_ROW_MASK;

        if (unlikely(tileY == PPU_FRAME_HEIGHT >> PPU_TILE_COLUMN_BIT)) {
            tileY = 0;
            current->ntY ^= 1;
        }
        current->tileY = tileY;
    }
    current->fineY = fineY & TILE_ROW_MASK;
}

static PixelBgDataSet GetBackgroundPixel(const LuCNesPPU* ppu, const PPUTile* bgPtTable, uint8_t x)
{
    VRAMAddrReg cur = ppu->iRegs.cur;
    if ((x ^ (x + ppu->iRegs.fineX)) & ~TILE_COLUMN_MASK)
        IRegIncX(&cur);

    PPUNameTable* nt = ppu->mmap.ntMirrTable[cur.nt];
    uint16_t ntIdx = (ppu->iRegs.cur.tileY << TILE_PER_ROW_BIT) + (cur.tileX & TILE_PER_ROW_MASK);
    uint16_t tileIdx = nt->tiles[ntIdx];
    uint8_t pixelColorIdx = GetTilePixel(&bgPtTable[tileIdx], ppu->iRegs.cur.fineY,
                                         ~(x + ppu->iRegs.fineX) & TILE_COLUMN_MASK);

    LogPrintAssert(pixelColorIdx < PPU_COLORS_PER_PALETTE, "Invalid color index for palette\n");
    return (PixelBgDataSet) {
        .colorIdx = pixelColorIdx,
        .paletteIdx = pixelColorIdx ? getBgPaletteIdx(nt, ntIdx) : 0,
    };
}

static inline void DrawBgPixelColor(const LuCNesPPU* ppu, const uint8_t y, const uint8_t x,
                                    const PixelBgDataSet* pixel)
{
    PPUPalette* palette = LookupBgPalette(ppu, pixel->paletteIdx);
    VideoSetPixel(ppu->video, y, x, palette->colors[pixel->colorIdx].value);
}

static inline void DrawSpPixelColor(const LuCNesPPU* ppu, const uint8_t y, const uint8_t x,
                                    const PixelSpDataSet* pixel)
{
    PPUPalette* palette = LookupSpPalette(ppu, pixel->paletteIdx);
    VideoSetPixel(ppu->video, y, x, palette->colors[pixel->colorIdx].value);
}

static inline void DrawBackDropColor(const LuCNesPPU* ppu, const uint8_t y, const uint8_t x)
{
    PPUPalette* palette = LookupBgPalette(ppu, 0);
    VideoSetPixel(ppu->video, y, x, palette->colors[0].value);
}

static void PpuRenderTiles(LuCNesPPU* ppu, const PPUTile* bgPtTable, uint8_t y, uint8_t endTileX)
{
    const PPUReg* reg = ppu->reg;
    uint16_t* lastTileX = &ppu->render.lastTileX;
    bool bgMask = reg->mask & PPU_BACKGROUND_ENABLE_MASK,
         spMask = reg->mask & PPU_SPRITE_ENABLE_MASK;

    for (; *lastTileX < endTileX; (*lastTileX)++) {
        uint8_t x = *lastTileX << PPU_TILE_COLUMN_BIT;
        bool bgEnabled = bgMask && (*lastTileX || (reg->mask & PPU_BACKGROUND_LEFT_ENABLE_MASK)),
             spEnabled = spMask && (*lastTileX || (reg->mask & PPU_SPRITE_LEFT_ENABLE_MASK));
        for (uint8_t offs = 0; offs < PPU_TILE_COLUMN_NUM; offs++, x++) {
            if (likely(bgEnabled)) {
                PixelBgDataSet bgPixel = GetBackgroundPixel(ppu, bgPtTable, x);
                PixelSpDataSet spPixel = { .byte = ppu->render.lineSprites[x] };
                if (unlikely(spEnabled && spPixel.byte && spPixel.colorIdx)) {
                    if (spPixel.sp0 && bgPixel.colorIdx)
                        SpriteZeroHitUpdate(ppu, x);

                    if (!bgPixel.colorIdx || spPixel.fgFlag) {
                        DrawSpPixelColor(ppu, y, x, &spPixel);
                        continue;
                    }
                }
                DrawBgPixelColor(ppu, y, x, &bgPixel);
                continue;
            } else if (spEnabled) {
                PixelSpDataSet spPixel = { .byte = ppu->render.lineSprites[x] };
                if (unlikely(spPixel.byte && spPixel.colorIdx)) {
                    DrawSpPixelColor(ppu, y, x, &spPixel);
                    continue;
                }
            }
            DrawBackDropColor(ppu, y, x);
        }
        IRegIncX(&ppu->iRegs.cur);
    }
}

static void PreRenderSpriteTile(LuCNesPPU* ppu, SpriteObj* sprite, uint8_t high, uint8_t low, bool sp0)
{
    const uint8_t spriteX = sprite->x;

    for (int8_t x = 0; x < PPU_TILE_COLUMN_NUM && spriteX + x < PPU_FRAME_WIDTH; x++) {
        uint8_t tile_x = sprite->flipH ? x : ~x & TILE_COLUMN_MASK;
        uint8_t colorIdx = GetPixelColorIdx(high, low, tile_x);
        PixelSpDataSet pixel;

        if (!colorIdx) /* Transparent pixel */
            continue;

        LogPrintAssert(colorIdx < PPU_COLORS_PER_PALETTE, "Invalid color index for palette\n");

        if (ppu->render.lineSprites[spriteX + x])
            continue;

        pixel = (PixelSpDataSet) {
            .colorIdx = colorIdx,
            .paletteIdx = sprite->paletteIdx,
            .fgFlag = (sprite->priority == SPRITE_PRIORITY_FOREGROUND),
            .sp0 = sp0,
        };
        ppu->render.lineSprites[spriteX + x] = pixel.byte;
    }
}

static void PreRenderSpriteLine(LuCNesPPU* ppu, const uint8_t y)
{
    PPUReg* reg = ppu->reg;
    uint8_t oamIdxShift = reg->oam.addr >> PPU_SPRITE_SIZE_BIT, spritesNum = 0;
    const uint8_t spriteHeight = reg->ctrl & PPU_SPRITE_HEIGHT_MASK ?
                                    PPU_TILE_ROW16_NUM - 1: PPU_TILE_ROW_NUM - 1;

    if (unlikely(y >= PPU_FRAME_HEIGHT))
        return;

    LogPrintAssert(!(reg->mask & PPU_SPRITE_ENABLE_MASK) || !(reg->oam.addr & PPU_SPRITE_SIZE_MASK),
        "Unalligned OAMADDR %x\n", reg->oam.addr);

    memset(ppu->render.lineSprites, 0, sizeof(ppu->render.lineSprites));

    for (uint8_t spIdx = 0; spIdx < (PPU_OAM_SIZE >> PPU_SPRITE_SIZE_BIT) - oamIdxShift; spIdx++) {
        SpriteObj* sprite = &ppu->sprites[spIdx + oamIdxShift];
        PPUTile* tile;
        uint8_t tile_y;
        uint8_t high, low;

        if (sprite->y >= PPU_FRAME_HEIGHT)
            continue;

        if (y > sprite->y + spriteHeight || y < sprite->y)
            continue;

        /* XXX: Sprite overflow has hardware bug. Is it worth emulating this bug as well?
         * https://wiki.nesdev.org/w/index.php?title=PPU_sprite_evaluation#Sprite_overflow_bug
         */
        if (++spritesNum > PPU_SPRITES_PER_LINE) {
            reg->status |= PPU_SPRITE_OVERFLOW_MASK;
            LogPrintDbg("Sprite Overflow: y: %u, x: %u\n", sprite->y, sprite->x);
            break;
        }
        tile_y = y - sprite->y;
        if (unlikely(sprite->flipV))
            tile_y = spriteHeight - tile_y;

        if (unlikely(ppu->reg->ctrl & PPU_SPRITE_HEIGHT_MASK)) {
            PPUTile* ptTable = ppu->mmap.ptTileMirrTable[sprite->tile.ptNum];
            uint8_t topTileIdx = sprite->tile.idx8x16 << 1;

            /* An 8x16 tile is just two 8x8 tiles. So here, within the 8x16 tile, we pick either
             * the first or the second 8x8 tile and convert tile_y into 8 row form so we can reuse
             * GetTilePixel().
             */
            tile = &ptTable[topTileIdx + (tile_y >> PPU_TILE_ROW_BIT)];
            tile_y &= TILE_ROW_MASK;
        } else {
            PPUTile* ptTable = ppu->mmap.ptTileMirrTable[!!(reg->ctrl & PPU_SPRITE_TILE_MASK)];
            tile = &ptTable[sprite->tile.idx8x8];
        }

        high = tile->high[tile_y];
        low = tile->low[tile_y];

        if (high || low)
            PreRenderSpriteTile(ppu, sprite, high, low, !spIdx);
    }
}

static inline bool IsSpriteZeroHit(const LuCNesPPU* ppu, uint16_t end)
{
    uint8_t x = ppu->render.sp0.x;
    return ppu->render.sp0.hit && x >= ppu->render.lastDot && x < end;
}

static void VisibleScanLineNoRender(LuCNesPPU* ppu)
{
    if (ppu->dot > PPU_PIXELS_PER_LINE)
        return;

    // assert vadrr > $3F00 && vaddr < $3FFF
    /* https://wiki.nesdev.com/w/index.php/PPU_palettes#The_background_palette_hack
     * XXX: implement background palette hack
     */
}

static inline void ResetOAMAddr(PPUReg* reg)
{
    if (likely(reg->mask & PPU_RENDERING_ENABLED))
        reg->oam.addr = 0;
}

#define PPU_RENDER_TILE_PIXELS 0 ... 255
#define PPU_RENDER_INC_Y       256
#define PPU_RENDER_ASSIGN_X    257
#define PPU_RENDER_FETCH       258 ... PPU_CYCLES_PER_LINE
#define PPU_OAMADDR_SET_ZERO_BEGIN 257
#define PPU_OAMADDR_SET_ZERO_END   320

void PpuVisibleLineRender(LuCNesPPU* ppu)
{
    PPUReg* reg = ppu->reg;

    do {
        LogPrintAssert(ppu->render.lastDot <= ppu->dot,
            "Invalid dot state: dot: %u, last: %u\n", ppu->dot, ppu->render.lastDot);

        switch (ppu->render.lastDot) {
            case PPU_RENDER_TILE_PIXELS: {
                uint16_t nextCycleStage = MIN(PPU_RENDER_INC_Y, ppu->dot);

                if (likely(PpuRenderingEnabled(ppu))) {
                    PPUTile* bgPtTable = ppu->mmap.ptTileMirrTable[!!(reg->ctrl & PPU_BACKGROUND_TILE_MASK)];
                    const uint8_t endTileX = DIV_ROUND_UP(nextCycleStage, PPU_TILE_COLUMN_NUM);

                    PpuRenderTiles(ppu, bgPtTable, ppu->scanLine, MIN(endTileX + 2, PPU_LINE_TILES));
                } else
                    VisibleScanLineNoRender(ppu);

                if (IsSpriteZeroHit(ppu, nextCycleStage)) {
                    LogPrintDbg("SpriteZeroHit set: %d\n", ppu->render.lastDot);
                    ppu->reg->status |= PPU_SPRITE0_MASK;
                }
                ppu->render.lastDot = nextCycleStage;
            }
                break;
            case PPU_RENDER_INC_Y:
                /* Sprite data is delayed by one scanline and sprites are never displayed on the
                 * first line. However, OAM stores sprite Y starting at 0, so scanning on scanLine
                 * 0 is fine.
                 */
                if (likely(PpuRenderingEnabled(ppu))) {
                    PreRenderSpriteLine(ppu, ppu->scanLine);
                    IRegIncY(&ppu->iRegs.cur);
                }
                ppu->render.lastTileX = 0;
                ppu->render.sp0.hit = false;
                ppu->render.lastDot++;
                break;
            case PPU_RENDER_ASSIGN_X:
                if (likely(PpuRenderingEnabled(ppu))) {
                    ppu->iRegs.cur.tileX = ppu->iRegs.tmp.tileX;
                    ppu->iRegs.cur.ntX = ppu->iRegs.tmp.ntX;
                    reg->oam.addr = 0;
                }
                ppu->render.lastDot++;
                break;
            case PPU_RENDER_FETCH:
                /* XXX: (321 - 335) On a real device this part is used to fetch first two tiles
                 * on the next scan line.
                 */
                if (ppu->render.lastDot <= PPU_OAMADDR_SET_ZERO_END)
                    ResetOAMAddr(reg);

                ppu->render.lastDot = ppu->dot;
                return;
            default:
                LogPrintAssert(0, "Invalid lastDot value: %u\n", ppu->render.lastDot);
        }
    } while (ppu->render.lastDot != ppu->dot);
}

#define PPU_PRERENDER_ZERO_CYCLE  0
#define PPU_PRERENDER_CLEAR_FLAGS 1
#define PPU_PRERENDER_UNUSED_TILE_FETCH 2 ... 256
#define PPU_PRERENDER_ASSIGN_X          257
#define PPU_PRERENDER_IDLE_CYCLES       258 ... 279
#define PPU_PRERENDER_ASSIGN_Y_BEGIN 280
#define PPU_PRERENDER_ASSIGN_Y_END   304
#define PPU_PRERENDER_ASSIGN_Y       PPU_PRERENDER_ASSIGN_Y_BEGIN ... PPU_PRERENDER_ASSIGN_Y_END
#define PPU_PRERENDER_ODD_SKIP       338 /* https://www.nesdev.org/wiki/PPU_frame_timing#Even/Odd_Frames */
#define PPU_PRERENDER_FETCH          PPU_PRERENDER_ASSIGN_Y_END + 1 ... PPU_PRERENDER_ODD_SKIP - 1
#define PPU_PRERENDER_DUMMY_FETCH    PPU_PRERENDER_ODD_SKIP + 1 ... PPU_CYCLES_PER_LINE

void PpuPreRenderLine(LuCNesPPU* ppu)
{
    PPUReg* reg = ppu->reg;

    do {
        switch (ppu->render.lastDot) {
            case PPU_PRERENDER_ZERO_CYCLE:
                ppu->render.lastDot++;
                break;
            case PPU_PRERENDER_CLEAR_FLAGS:
                reg->status &= ~PPU_VBLANK_MASK;
                reg->status &= ~PPU_SPRITE0_MASK;
                reg->status &= ~PPU_SPRITE_OVERFLOW_MASK;

                memset(ppu->render.lineSprites, 0, sizeof(ppu->render.lineSprites));
                ppu->render.lastDot++;
                break;
            case PPU_PRERENDER_UNUSED_TILE_FETCH:
                ppu->render.lastDot = MIN(PPU_PRERENDER_ASSIGN_X, ppu->dot);
                break;
            case PPU_PRERENDER_ASSIGN_X:
                if (likely(reg->mask & PPU_RENDERING_ENABLED)) {
                    ppu->iRegs.cur.tileX = ppu->iRegs.tmp.tileX;
                    ppu->iRegs.cur.ntX = ppu->iRegs.tmp.ntX;
                    reg->oam.addr = 0;
                }
                ppu->render.lastDot++;
                break;
            case PPU_PRERENDER_IDLE_CYCLES:
                ppu->render.lastDot = MIN(PPU_PRERENDER_ASSIGN_Y_BEGIN, ppu->dot);
                ResetOAMAddr(reg);
                break;
            case PPU_PRERENDER_ASSIGN_Y:
                if (likely(reg->mask & PPU_RENDERING_ENABLED)) {
                    ppu->iRegs.cur.tileY = ppu->iRegs.tmp.tileY;
                    ppu->iRegs.cur.ntY = ppu->iRegs.tmp.ntY;
                    ppu->iRegs.cur.fineY = ppu->iRegs.tmp.fineY;
                    reg->oam.addr = 0;
                }
                ppu->render.lastDot = MIN(PPU_PRERENDER_ASSIGN_Y_END + 1, ppu->dot);
                break;
            case PPU_PRERENDER_FETCH:
                /* XXX: (321 - 335) On a real device this part is used to fetch first two tiles
                 * on the next scan line.
                 */
                if (ppu->render.lastDot <= PPU_OAMADDR_SET_ZERO_END)
                    ResetOAMAddr(reg);

                ppu->render.lastDot = MIN(PPU_PRERENDER_ODD_SKIP, ppu->dot);
                break;
            case PPU_PRERENDER_ODD_SKIP:
                if (ppu->oddFrame && (reg->mask & PPU_RENDERING_ENABLED))
                    ppu->dot++; /* Skip last idle ppu cycle for odd frame */
                fallthrough;
            case PPU_PRERENDER_DUMMY_FETCH:
                ppu->render.lastDot = ppu->dot;
                return;
            default:
                LogPrintAssert(0, "Invalid lastDot value: %u\n", ppu->render.lastDot);
        }
    } while (ppu->render.lastDot != ppu->dot);
}
