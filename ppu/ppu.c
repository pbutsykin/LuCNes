/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2016 Pavel Butsykin
 */
#define CNES_PPU
#define PPU

#include <utils/utils.h>
#include <cpu/interface.h>
#include <apu/interface.h>
#include <rom/rom.h>
#include <mapper/interface.h>
#include <video/interface.h>

#include "ppu.h"
#include "render.h"


PPUMMap* PpuMMap(LuCNesPPU* ppu)
{
    return &ppu->mmap;
}

static bool PpuNmiEdgeSet(bool val)
{
    static bool NMI_Signal;

    if (val ^ NMI_Signal) {
        NMI_Signal = val;
        return !val;
    }
    return val;
}

bool PpuCheckNMI(LuCNesPPU* ppu)
{
    if (unlikely(ppu->reg->status & PPU_NMI_PENDING_MASK)) {
         ppu->reg->status &= ~PPU_NMI_PENDING_MASK;
         return PpuNmiEdgeSet(true);
    }
    return PpuNmiEdgeSet(false);
}

#define PPU_VISIBLE_LINES     0 ... 239
#define PPU_POST_RENDER_LINE  240
#define PPU_VBLANK_LINE       241
#define PPU_VBLANK_IDLE_LINES 242 ... 260
#define PPU_PRE_RENDER_LINE   261
#define PPU_VBLANK_END        262

uint8_t* PpuStatusRead(LuCNesPPU* ppu, PPUReg* reg)
{
    static uint8_t oldPPUState;

    oldPPUState = reg->status;
    reg->status &= ~PPU_VBLANK_MASK;

    if (ppu->iRegs.w)
        ppu->iRegs.w = 0;

    if (unlikely(ppu->scanLine == PPU_VBLANK_LINE && ppu->dot == 1))
        reg->status |= PPU_VBLANK_READ_RC_MASK;

    if (unlikely(ppu->scanLine == PPU_VBLANK_LINE && (ppu->dot == 2 || ppu->dot == 3)))
        reg->status &= ~PPU_NMI_PENDING_MASK;

    return &oldPPUState;
}

void PpuTicksExecute(LuCNesPPU* ppu, const uint8_t cpuCycles)
{
#define CPU_TO_PPU_CYCLES(__cc) (((__cc) << 1) + (__cc))

    ppu->dot += CPU_TO_PPU_CYCLES(cpuCycles);

    do {
        switch (ppu->scanLine) {
            case PPU_VISIBLE_LINES:
                PpuVisibleLineRender(ppu);
                break;
            case PPU_POST_RENDER_LINE:
                /* IDLE line. So, let's show frame during this line */
                if (!ppu->render.lastDot)
                    VideoFrameFlush(ppu->video);

                ppu->render.lastDot = ppu->dot;
                break;
            case PPU_VBLANK_LINE:
                if (ppu->render.lastDot <= 1 && ppu->dot > 1) {
                    if (likely(!(ppu->reg->status & PPU_VBLANK_READ_RC_MASK))) {
                        ppu->reg->status |= PPU_VBLANK_MASK;

                        if (ppu->reg->ctrl & PPU_NMI_MASK) {
                            if (ppu->dot > 3)
                                PpuNmiEdgeSet(true);
                            else
                                ppu->reg->status |= PPU_NMI_PENDING_MASK;
                        }
                    } else
                        ppu->reg->status &= ~PPU_VBLANK_READ_RC_MASK;
                }
                ppu->render.lastDot = ppu->dot;
                break;
            case PPU_VBLANK_IDLE_LINES:
                ppu->render.lastDot = ppu->dot;
                break;
            case PPU_PRE_RENDER_LINE:
                PpuPreRenderLine(ppu);
                break;
            case PPU_VBLANK_END:
                ppu->scanLine = 0;
                ppu->oddFrame ^= true;
                LogPrintDbg("END_OF_VBLANK\n");
                break;
            default:
                LogPrintAssert(0, "Invalid scan line: %u\n", ppu->scanLine);
        }

        if (ppu->dot > PPU_CYCLES_PER_LINE) {
            ppu->scanLine++;
            //LogPrintDbg("Scan line: %u\n", ppu->scanLine);
            ppu->dot -= PPU_CYCLES_PER_LINE + 1;
            ppu->render.lastDot = 0;
            ppu->render.rendToggleDot = 0;
        }
    } while (ppu->render.lastDot != ppu->dot);
}

enum {
    PPU_REG_CTRL     = 0,
    PPU_REG_MASK     = 1,
    PPU_REG_STATUS   = 2,
    PPU_REG_OAM_ADDR = 3,
    PPU_REG_OAM_DATA = 4,
    PPU_REG_SCROLL   = 5,
    PPU_REG_ADDR     = 6,
    PPU_REG_DATA     = 7,

#define PPU_REG_MIRRORS 8 ... 0x1FFF
} PPU_REG_OFFS;

#define PPU_REG_TRACE false
#if PPU_REG_TRACE
#define RegTraceFmt(_rw, _reg, _cycles, _rval, _fmt, ...) \
    do {                                                  \
        printf("%c: %s: %02x"_fmt"\t(dot:%u)\n", (_rw) ? 'w' : 'r',  \
               (_reg), (_rw) ? (val) : (_rval), ##__VA_ARGS__, _cycles); \
        fflush(stdout);                                   \
    } while(false)
#else
    #define RegTraceFmt(...) do {} while(0)
#endif

#define RegTrace(_rw, _reg, _cycles, _rval, ...) \
    RegTraceFmt(_rw, _reg, _cycles, _rval, "", ##__VA_ARGS__)

#define PPU_PAGE_SIZE_BIT 10

static inline uint8_t* PpuAddrPattern(void* ctx, const uint16_t addr)
{
    uint8_t** ptEntry = ctx;
    return *ptEntry + addr;
}

static inline uint8_t* PpuAddrNameTable(void* ctx, const uint16_t addr)
{
#define PPU_PAGE_SIZE_MASK ((1 << PPU_PAGE_SIZE_BIT) - 1)

    uint8_t** nameMirrTable = ctx;
    return nameMirrTable[addr >> PPU_PAGE_SIZE_BIT] + (addr & PPU_PAGE_SIZE_MASK);
}

static inline uint8_t* PpuAddrMirror1(void* ctx, const uint16_t addr)
{
    return PpuAddrNameTable(ctx, addr);
}

static inline uint8_t* PpuAddrPalette(void* ctx, const uint16_t addr)
{
#define PPU_MIRROR1 0xC00 ... 0xEFF
#define PPU_PALETTE_BASE 0xF00
#define PPU_PALETTE PPU_PALETTE_BASE ... 0xF1F
#define PPU_MIRROR2_BASE 0xF20
#define PPU_MIRROR2 PPU_MIRROR2_BASE ... 0xFFF
#define PPU_PALETTE_UNIQ_MIRROR_MASK 15

    const PPUMMap* map = ctx;
    switch (addr) {
        case PPU_MIRROR1:
            return PpuAddrNameTable((uint8_t*)map->nameMirrTable, addr);
        case PPU_PALETTE: {
            uint16_t offs = addr - PPU_PALETTE_BASE;
            switch (offs) { /* Addresses $3F10/$3F14/$3F18/$3F1C are mirrors of $3F00/$3F04/$3F08/$3F0C. */
                case 0x10:  /* Note that this goes for writing as well as reading. */
                case 0x14:
                case 0x18:  /* Looks like we don't need to sync data with real mirrored offsets, because */
                case 0x1C:  /* ppu render doesn't read mirrored unique data at $3F10/$3F14/$3F18/$3F1C */
                    return map->palette + (offs & PPU_PALETTE_UNIQ_MIRROR_MASK);
            }
            return map->palette + offs;
        }
        case PPU_MIRROR2:
            return map->palette + (addr - PPU_MIRROR2_BASE);
        default:
            LogPrintAssert(0, "Invalid ppu address: %x\n", addr);
    }
    return NULL;
}

static inline bool PpuInVblank(LuCNesPPU* ppu)
{
    return ppu->scanLine > PPU_POST_RENDER_LINE && ppu->scanLine < PPU_PRE_RENDER_LINE;
}

static inline uint8_t* PpuAddrLookup(LuCNesPPU* ppu)
{
#define PPU_BASE_ADDR_MASK (4096 - 1)

    const PPUMTab* memEntry = &ppu->mtab[ppu->iRegs.cur.addr >> PPU_PAGE_SIZE_BIT];
    return memEntry->ppuAddrLookup(memEntry->ctx, ppu->iRegs.cur.addr & PPU_BASE_ADDR_MASK);
}

static inline bool PpuAddrWithinChrRom(const PPUMMap* mmap, const uint8_t* ppuAddr)
{
    return ppuAddr >= mmap->chrROM && ppuAddr < mmap->chrROM + mmap->chrSize;
}

static uint8_t* PpuRegHandle(LuCNesPPU* ppu, PPUReg* reg, uint8_t* addr, uint8_t val, bool write)
{
    LogPrintAssert(addr >= (uint8_t*)reg, "Wrong ppu addr: %p\n", addr);
    const uint16_t regOffs = addr - (uint8_t*)reg;
    uint8_t* retPtr = NULL;

    switch (regOffs) {
        case PPU_REG_CTRL: {
            RegTrace(write, "ctrl", ppu->dot, reg->ctrl);
            LogPrintAssert(write, "Not implemented(read)!\n");

            /* At scanline 261 dot 1 PPU_VBLANK_MASK is cleared by PpuPreRenderLine().
             * Force-clear it here so that enabling NMI at this exact dot doesn't trigger it.
             */
            if (unlikely(ppu->scanLine == PPU_PRE_RENDER_LINE && ppu->dot == 1))
                reg->status  &= ~PPU_VBLANK_MASK;

            /* If the PPU is currently in vertical blank, and the PPUSTATUS ($2002) vblank flag is
             * still set (1), changing the NMI flag in bit 7 of $2000 from 0 to 1 will immediately
             * generate an NMI.
             * Writes to $2000 on the last cpu cycle delays NMI by one instruction.
             */
            if ((reg->ctrl ^ val) & PPU_NMI_MASK) {
                if ((val & PPU_NMI_MASK) && (reg->status & PPU_VBLANK_MASK))
                    reg->status |= PPU_NMI_PENDING_MASK;
                else if (unlikely(reg->status & PPU_NMI_PENDING_MASK))
                    reg->status &= ~PPU_NMI_PENDING_MASK; /* NMI went low and high within one CPU cycle */
            }
            reg->ctrl = val;
            ppu->iRegs.tmp.nt = val & PPU_NAMETABLE_ADDR_MASK;
            break;
        }
        case PPU_REG_MASK: {
            RegTrace(write, "mask", ppu->dot, reg->mask);
            LogPrintAssert(write, "Not implemented(read)!\n");

            /* Toggling rendering takes effect (in ppu render) approximately 3-4 dots after
             * the write. https://www.nesdev.org/wiki/PPU_registers
             * Use ppu->render.rendToggleDot to track this transition and return the old state
             * during the delay window.
             */
            if ((val ^ reg->mask) & PPU_RENDERING_ENABLED)
                ppu->render.rendToggleDot = ppu->dot;
            reg->mask = val;
            break;
        }
        case PPU_REG_STATUS: {
            if (unlikely(write))
                break; // TODO: Open bus behavior (see https://www.nesdev.org/wiki/PPU_registers#MMIO_registers)

            retPtr = PpuStatusRead(ppu, reg);

            RegTrace(write, "status", ppu->dot, *retPtr);
            break;
        }
        case PPU_REG_OAM_ADDR: {
            RegTrace(write, "oam.addr", ppu->dot, reg->oam.addr);
            LogPrintAssert(write, "Not implemented(read)!\n");
            reg->oam.addr = val;
            break;
        }
        case PPU_REG_OAM_DATA: {
            RegTrace(write, "oam.data", ppu->dot, reg->oam.data);
            if (unlikely((reg->mask & PPU_RENDERING_ENABLED) && !PpuInVblank(ppu))) {
                /* XXX: For emulation purposes, it is probably best to completely ignore writes
                 * during rendering. (https://wiki.nesdev.com/w/index.php/PPU_registers#OAMDATA)
                 * But maybe it will nice to implement exact behavior of hardware.
                 */
                LogPrintAssert(!write, "Write to OAM DATA during rendering!\n");
            }

            /* XXX: The readability was added on the RP2C02G, found on most NESes and later Famicoms. */
            LogPrintAssert(write, "Not implemented(read)!\n");

            /* XXX: In the 2C07, sprite evaluation can never be fully disabled, and will always
             * start 20 scanlines after the start of vblank[8] (same as when the prerender scanline
             * would have been on the 2C02). As such, you must upload anything to OAM that you
             * intend to within the first 20 scanlines after the 2C07 signals vertical blanking.
             */
             ppu->oam[reg->oam.addr++] = reg->oam.data = val;
             break;
        }
        case PPU_REG_SCROLL: {
            RegTrace(write, "scroll", ppu->dot, reg->scroll);
            LogPrintAssert(write, "Not implemented(read)!\n");

            union {
                uint8_t val;
                struct {
                    uint8_t low:3;
                    uint8_t high:5;
                };
            } scrollValue = {
                .val = val,
            };

            reg->scroll = val;

            if (ppu->iRegs.w^=1) {
                ppu->iRegs.tmp.tileX = scrollValue.high;
                ppu->iRegs.fineX = scrollValue.low;
            } else {
                ppu->iRegs.tmp.tileY = scrollValue.high;
                ppu->iRegs.tmp.fineY = scrollValue.low;
            }

            LogPrintDbg("scroll temp: y: %u, x: %u\n",
                (ppu->iRegs.tmp.tileY << PPU_TILE_COLUMN_BIT) | ppu->iRegs.tmp.fineY,
                (ppu->iRegs.tmp.tileX << PPU_TILE_ROW_BIT) | ppu->iRegs.fineX);
            LogPrintDbg("scroll current: y: %u, x: %u\n",
                (ppu->iRegs.cur.tileY << PPU_TILE_COLUMN_BIT) | ppu->iRegs.cur.fineY,
                (ppu->iRegs.cur.tileX << PPU_TILE_ROW_BIT) | ppu->iRegs.fineX);
            break;
        }
        case PPU_REG_ADDR: {
            RegTrace(write, "addr", ppu->dot, reg->addr);
            LogPrintAssert(write, "Not implemented(read)!\n");

            reg->addr = val;

            if (ppu->iRegs.w^=1) {
                ppu->iRegs.tmp.high = val;
                ppu->iRegs.tmp.Z = 0;
            } else {
                ppu->iRegs.tmp.low = val;
                ppu->iRegs.cur = ppu->iRegs.tmp;
            }
            break;
        }
        case PPU_REG_DATA: {
            RegTraceFmt(write, "data", ppu->dot, ppu->mmap.ram[ppu->iRegs.cur.addr],
                        " %s %04x", write ? "->" : "<-", ppu->iRegs.cur.addr);
            if (unlikely((reg->mask & PPU_RENDERING_ENABLED) && !PpuInVblank(ppu)))
                LogPrintAssert(!write, "Write to PPUDATA during rendering!\n");

            uint8_t* ppuAddr = PpuAddrLookup(ppu);

            if (likely(write)) {
                reg->data = val;
                if (ppu->mmap.chrRAM || likely(!PpuAddrWithinChrRom(&ppu->mmap, ppuAddr)))
                    *ppuAddr = val;
            } else {
                static uint8_t internalBuff; /* post-fetch */

                if (ppu->iRegs.cur.addr < PPU_PALETTE_ADDR) {
                    reg->data = internalBuff;
                    internalBuff = *ppuAddr;
                } else 
                    reg->data = internalBuff = *ppuAddr;
                /* XXX: Reading the palettes still updates the internal buffer though,
                 *      but the data placed in it is the mirrored nametable data that would
                 *      appear "underneath".
                 */
                retPtr = &reg->data;
            }
            ppu->iRegs.cur.addr += (reg->ctrl & PPU_INCREMENT_MODE_MASK) ? 32 : 1;
            break;
        }
        default:
            LogPrintAssert(0, "Wrong ppu register offset: %d\n", regOffs);
            break;
    }
    return retPtr;
}

void* PpuRegRead(void* ppu, MMap* mmap, uint8_t* addr)
{
    return PpuRegHandle(ppu, (PPUReg*)mmap->ppuReg, addr, 0, false);
}
void PpuRegWrite(void* ppu, MMap* mmap, uint8_t* addr, uint8_t val)
{
    PpuRegHandle(ppu, (PPUReg*)mmap->ppuReg, addr, val, true);
    return;
}

void PpuDMAWrite(void* ctx, MMap* mmap, __maybe_unused uint8_t* addr, uint8_t val)
{
#define VAL_TO_CPU_OFFS(_val) ((_val) << 8)
    LogPrintAssert(mmap->dma == addr, "DMA address corrupted(%p != %p)\n", mmap->dma, addr);

    LuCNesPPU* ppu = ctx;
    CNesConnector* con = ppu->con;
    uint64_t cpuCycles = CpuReadCycles(con->cpu);
    uint8_t oddCycles = cpuCycles & 1, opCycles = 2;

    RegTraceFmt(true, "dma", ppu->dot, 0, ": copy from 0x%04x", VAL_TO_CPU_OFFS(val));
    *mmap->dma = val;

    LogPrintAssert(!ppu->reg->oam.addr, "OAMDMA is not allowed from non-zero oam addr: %x.\n", ppu->reg->oam.addr);
    do {
        ppu->oam[ppu->reg->oam.addr] = *(mmap->ram + VAL_TO_CPU_OFFS(val) + ppu->reg->oam.addr);

        cpuCycles += opCycles;
        PpuTicksExecute(ppu, opCycles);
        ApuTicksExecute(con->apu, opCycles);
    } while (ppu->reg->oam.addr++ != PPU_OAM_SIZE - 1);

    opCycles = 1 + oddCycles;
    cpuCycles += opCycles;

    CpuWriteCycles(con->cpu, cpuCycles);
    PpuTicksExecute(ppu, opCycles);
    ApuTicksExecute(con->apu, opCycles);
#undef VAL_TO_CPU_OFFS
}

void* PpuDMARead(__maybe_unused void* ppu, MMap* mmap, __maybe_unused uint8_t* addr)
{
    LogPrintAssert(0, "Read DMA register not allowed.\n");
    return mmap->dma;
}

void PpuDebugDumpState(MMap* mmap)
{
    __maybe_unused PPUReg* reg = (PPUReg*)mmap->ppuReg;
    LogPrintDbg("ppu dump:\n");

    PRINT_FIELD_HEX32(reg->ctrl);
    PRINT_FIELD_HEX32(reg->mask);
    PRINT_FIELD_HEX32(reg->status);
    PRINT_FIELD_HEX32(reg->oam.addr);
    PRINT_FIELD_HEX32(reg->oam.data);
    PRINT_FIELD_HEX32(reg->scroll);
    PRINT_FIELD_HEX32(reg->addr);
    PRINT_FIELD_HEX32(reg->data);
}

static PPUReg* PpuRegInit(void* mem)
{
    PPUReg* reg = mem;

    /* https://wiki.nesdev.com/w/index.php/PPU_power_up_state */
    *reg = (PPUReg) {
        .status = PPU_VBLANK_MASK | PPU_SPRITE_OVERFLOW_MASK, /* +0+x xxxx */
    };
    /* XXX:
        odd frame - no,
        OAM - pattern,
        NT RAM (external, in Control Deck) - 0xFF,
        CHR RAM (external, in Game Pak) - unspecified pattern,
    */
    return reg;
}


typedef struct _PPUChrSpace {
    uint8_t PatternTable0[KB(4)];  /* $0000-$0FFF, mapped by the cartridge */
    uint8_t PatternTable1[KB(4)];  /* $1000-$1FFF, mapped by the cartridge */
} __attribute__((packed)) PPUChrSpace;

typedef struct _PPUVRamSpace {
    uint8_t NameTable0[KB(1)]; /* $2000-$23FF */
    uint8_t NameTable1[KB(1)]; /* $2400-$27FF */
                               /* NameTable2 - $2800-$2BFF */
                               /* NameTable3 - $2C00-$2FFF */
                               /* $3000-$3EFF, Mirrors of $2000-$2EFF */
    uint8_t PaletteRAM[PPU_PALETTE_SIZE];    /* $3F00-$3F1F */
                                             /* $3F20-$3FFF, Mirrors of $3F00-$3F1F */
} __attribute__((packed)) PPUVRamSpace;

#define GET_VRAM_SPACE_ADDR(_P) ((PPUVRamSpace*)_P)
#define GET_VCHR_SPACE_ADDR(_P) ((PPUChrSpace*)_P)

static void PpuMMapInit(RomDesc* const rdesc, MapperObj* const mapper,
                        PPUMMap* mmap, PPUMTab (*mtab)[PPU_MAX_PAGES])
{
    uint8_t* virtMem;

    if (rdesc->chr.size) {
        LogPrintAssert(rdesc->chr.size <= MB(1) && rdesc->chr.size >= KB(8),
                       "CHR-ROM wrong size: %u.\n", rdesc->chr.size);

        virtMem = mmap->vram = MemAlloc(sizeof(PPUVRamSpace));
        mmap->chrROM = rdesc->chr.data;
        mmap->chrSize = rdesc->chr.size;
        mmap->pattern0 = GET_VCHR_SPACE_ADDR(mmap->chrROM)->PatternTable0;
        mmap->pattern1 = GET_VCHR_SPACE_ADDR(mmap->chrROM)->PatternTable1;
    } else {
        /* It's ok that cartridge may not have CHR-ROM,
         * in this case CHR-RAM will be used.
         */
        LogPrintDbg("No CHR-ROM\n");
        LogPrintAssert(rdesc->chrRamSize >= KB(8), "CHR-RAM wrong size: %u.\n", rdesc->chrRamSize);

        mmap->chrSize = sizeof(PPUChrSpace) + sizeof(PPUVRamSpace);
        virtMem = mmap->chrRAM = MemAlloc(mmap->chrSize);
        mmap->pattern0 = GET_VCHR_SPACE_ADDR(mmap->chrRAM)->PatternTable0;
        mmap->pattern1 = GET_VCHR_SPACE_ADDR(mmap->chrRAM)->PatternTable1;
        virtMem += sizeof(PPUChrSpace);
    }

    /* XXX: use virtual address maping to optimize memory access*/
    /* Horizontal:
       [ A ] [ a ]
       [ B ] [ b ]

       Vertical:
       [ A ] [ B ]
       [ a ] [ b ]
    */
    mmap->name = GET_VRAM_SPACE_ADDR(virtMem)->NameTable0;
    MapperNameTableInit(mapper, mmap, rdesc->mirrorType == MIRRORING_PPU_NAMETAB_VERTICAL);
    mmap->mirror1 = mmap->name;    /* $3000-$3EFF, Mirrors of $2000-$2EFF */
    mmap->palette = GET_VRAM_SPACE_ADDR(virtMem)->PaletteRAM;
    mmap->mirror2 = mmap->palette; /* $3F20-$3FFF, Mirrors of $3F00-$3F1F */

    PPUMTab mtabInit[PPU_MAX_PAGES] = {
        {PpuAddrPattern, &mmap->ptMirrTable[0]}, /* 0x0000 */
        {PpuAddrPattern, &mmap->ptMirrTable[0]}, /* 0x0400 */
        {PpuAddrPattern, &mmap->ptMirrTable[0]}, /* 0x0800 */
        {PpuAddrPattern, &mmap->ptMirrTable[0]}, /* 0x0C00 */
        {PpuAddrPattern, &mmap->ptMirrTable[1]}, /* 0x1000 */
        {PpuAddrPattern, &mmap->ptMirrTable[1]}, /* 0x1400 */
        {PpuAddrPattern, &mmap->ptMirrTable[1]}, /* 0x1800 */
        {PpuAddrPattern, &mmap->ptMirrTable[1]}, /* 0x1C00 */
        {PpuAddrNameTable, mmap->nameMirrTable}, /* 0x2000 */
        {PpuAddrNameTable, mmap->nameMirrTable}, /* 0x2400 */
        {PpuAddrNameTable, mmap->nameMirrTable}, /* 0x2800 */
        {PpuAddrNameTable, mmap->nameMirrTable}, /* 0x2C00 */
        {PpuAddrMirror1, mmap->nameMirrTable}, /* 0x3000 */
        {PpuAddrMirror1, mmap->nameMirrTable}, /* 0x3400 */
        {PpuAddrMirror1, mmap->nameMirrTable}, /* 0x3800 */
        {PpuAddrPalette, mmap},                /* 0x3C00 */
    };
    StaticAssert(sizeof(*mtab) == sizeof(mtabInit), "PPUMTab structure size mismatch\n");
    memcpy(mtab, &mtabInit, sizeof(*mtab));
}

LuCNesPPU* PpuInit(LuCNesCPU* cpu, RomDesc* rdesc, MapperObj* mapper, void* connector)
{
    LuCNesPPU* ppu = MemAlloc(sizeof(*ppu));
    *ppu = (LuCNesPPU) {
        .reg = PpuRegInit(CpuMMap(cpu)->ppuReg),
        .oam = MemAlloc(PPU_OAM_SIZE),
        /* https://wiki.nesdev.com/w/index.php/PPU_power_up_state
         * It is conjectured that the first VBL flag setting will be
         * close to 241 * 341/3.2 cycles (241 PAL scanlines); ... ©
         * (However, for some unknown reason, most emulators set 240 on the reset).
         */
        .scanLine = PPU_PRE_RENDER_LINE - 1,
    };

    ppu->video = VideoInit(PPU_FRAME_WIDTH, PPU_FRAME_HEIGHT);
    if (!ppu->video) {
        LogPrintErr("Can not initialize video subsystem.\n");
        goto fail1;
    }

    PpuMMapInit(rdesc, mapper, &ppu->mmap, &ppu->mtab);
    ppu->con = connector;

    return ppu;

fail1:
    MemFree(ppu->oam);
    MemFree(ppu);

    return NULL;
}

void PpuFree(LuCNesPPU* ppu)
{
    VideoFree(ppu->video);
    if (ppu->mmap.chrRAM)
        MemFree(ppu->mmap.chrRAM);
    else
        MemFree(ppu->mmap.vram);
    MemFree(ppu->oam);
    MemFree(ppu);
}
