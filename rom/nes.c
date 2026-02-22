/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_ROM
#define INES

#include <utils/utils.h>

#include "rom.h"

#define CHR_PAGE_SHIFT 13
#define PRG_PAGE_SHIFT 14
#define MAX_PRG_SIZE (256 << PRG_PAGE_SHIFT)

typedef struct _NesHeaderBase {
    uint8_t sign[4];    /* "NES"<EOF> */
    uint8_t prgRomSize; /* Number of 16384 byte program ROM pages */
    uint8_t chrRomSize; /* Number of 8192 byte character ROM pages (0 indicates CHR RAM) */
    union {
        uint8_t value;
        struct {
            uint8_t mirror:1;  /* Horizontal/Vertical  Mirroring ONLY */
            uint8_t sram:1;    /* Cartridge contains battery-backed PRG RAM ($6000-7FFF) or 
                                * other persistent memory */
            uint8_t trainer:1; /* 512-byte trainer at $7000-$71FF (stored before PRG data) */
            uint8_t v4scr:1;   /* 4 Screen VRAM */
            uint8_t low_map:4; /* Lower nybble of mapper number (0-15) */
        };
    } flags6;
    union {
        uint8_t value;
        struct {
            uint8_t vsu:1;    /* VS Unisystem */
            uint8_t pc10:1;   /* PlayChoice-10 (8KB of Hint Screen data stored after CHR data) */
            uint8_t nes2:2;  /* If equal to 2, flags 8-15 are in NES 2.0 format */
            uint8_t up_map:4; /* Upper nybble of mapper number (16-?) */
        };
    } flags7;
} __attribute__((packed)) NesHeaderBase;

typedef struct _INES {
    NesHeaderBase hdr;
    uint8_t reserve[8]; /* 8-15 Must be zeros for iNES */
    uint8_t data[];
} INESFmt;

typedef struct _NES2Fmt {
    NesHeaderBase hdr;
    union {
        uint8_t value;
        struct {
            uint8_t mbs:4;
            uint8_t submapper:4;
        };
    } mapper;
    uint8_t prgRomSizeMsb:4;
    uint8_t chrRomSizeMsb:4;
    uint8_t prgRamSize:4;
    uint8_t eepromSize:4;
    uint8_t chrRamSize:4;
    uint8_t chrNVRamSize:4;
    uint8_t timing:2;
    uint8_t reserved0:6;
    union {
        struct {
            uint8_t extConsoleType:4;
            uint8_t reserved1:4;
        };
        struct {
            uint8_t ppuType:4;
            uint8_t hwType:4;
        };
    };
    uint8_t miscRom:2;
    uint8_t reserved2:6;
    uint8_t defaultExpDevice:6;
    uint8_t reserved3:2;
    uint8_t data[];
} __attribute__((packed)) NES2Fmt;

enum CpuPpuTimig {
    NTSC        = 0,
    LicensedPAL = 1,
    Multiple    = 2,
    Dendy       = 3
};

enum DefaultExpansionDevice {
    DED_UNSPECIFIED = 0,
    DED_STANDARD_NES_CONTROLLERS = 1,
};

static inline uint32_t SetRomSegment(region_t* reg, uint8_t* entry, uint32_t offs, uint32_t val)
{
    assert(entry != NULL);
    assert(reg != NULL);

    reg->size = val;
    reg->data = entry + offs;

    return offs + val;
}

/*
 * Calculate NES 2.0 ROM size from LSB and MSB nibble.
 * https://www.nesdev.org/wiki/NES_2.0#PRG-ROM_Area
 */
static uint32_t CalcNes2RomSize(uint8_t lsb, uint8_t msb, uint32_t unitShift)
{
    if (msb == 0xF) {
        /* Exponent-multiplier notation */
        uint8_t E = lsb >> 2;
        uint8_t MM = lsb & 0x3;
        return (1U << E) * ((MM << 1) + 1);
    } else {
        uint32_t units = lsb | (msb << 8);
        return units << unitShift;
    }
}

static void ParseNesHeaderBase(MFile* file, NesHeaderBase *hdr, RomDesc* rdesc, uint32_t offs)
{
    /* PlayChoice INST-ROM, if present (0 or 8192 bytes) */
    offs = SetRomSegment(&rdesc->pc10, file->data, offs, hdr->flags7.pc10 ? KB(8) : 0);

    /* Some ROM additionally contain a 128-byte (or sometimes 127-byte) title at the end of file. */
    offs = SetRomSegment(&rdesc->title, file->data, offs, file->size - offs);

    LogPrintAssert(rdesc->title.size == 128 ||
                   rdesc->title.size == 127 ||
                   rdesc->title.size == 0, "wrong title size: %u\n", rdesc->title.size);

    /* get mapper type */
    rdesc->mapperId = (hdr->flags7.up_map << 4) | hdr->flags6.low_map;

    LogPrintAssert(!hdr->flags6.v4scr, "Four-Screen mirroring is not implemented.\n");

    rdesc->mirrorType = hdr->flags6.mirror ? MIRRORING_PPU_NAMETAB_VERTICAL :
                                               MIRRORING_PPU_NAMETAB_HORIZONTAL;
    rdesc->sram = hdr->flags6.sram;

    LogPrintDbg("\nRom Info:\nprg: %ukb, chr: %ukb"
                ", trainer: %ub, pc10: %ukb, title: %ub, sram: %d\n"
                "mapper num: %u, mirroring: %s\n",
                rdesc->prg.size >> KB_BIT, rdesc->chr.size >> KB_BIT,
                rdesc->trainer.size, rdesc->pc10.size, rdesc->title.size, rdesc->sram,
                rdesc->mapperId, hdr->flags6.mirror ? "Vertical" : "Horizontal");
}

bool CheckINESFormat(void* header)
{
    NesHeaderBase* h = header;
    return h->flags7.nes2 == 0;
}

int INESOpen(MFile* file, RomDesc* rdesc)
{
    INESFmt* ines = (void*)file->data;
    uint32_t offs = sizeof(*ines);

    if (memcmp(ines->reserve, "\0\0\0\0\0\0\0\0", sizeof(ines->reserve))) {
        LogPrintErr("iNES legacy extension is not supported.\n");
        return -1;
    }

    /* Trainer, if present (0 or 512 bytes) */
    offs = SetRomSegment(&rdesc->trainer, file->data, offs, ines->hdr.flags6.trainer ? 512 : 0);

    /* PRG ROM data (16384 * x bytes) */
    offs = SetRomSegment(&rdesc->prg, file->data, offs,
                         ines->hdr.prgRomSize ? (uint32_t)ines->hdr.prgRomSize << PRG_PAGE_SHIFT : MAX_PRG_SIZE);

    /* CHR ROM data, if present (8192 * y bytes) */
    offs = SetRomSegment(&rdesc->chr, file->data, offs, (uint32_t)ines->hdr.chrRomSize << CHR_PAGE_SHIFT);
    rdesc->chrRamSize = ines->hdr.chrRomSize ? 0 : KB(8);
    rdesc->submapper = 0;

    ParseNesHeaderBase(file, &ines->hdr, rdesc, offs);
    return 0;
}

bool CheckNES2Format(void* header)
{
#define NES20_FLAG7_VALUE 2

    NesHeaderBase* h = header;
    return h->flags7.nes2 == NES20_FLAG7_VALUE;
}

int NES2Open(MFile* file, RomDesc* rdesc)
{
    NES2Fmt* nes2 = (void*)file->data;
    uint32_t offs = sizeof(*nes2);
    uint32_t prgRomSize = CalcNes2RomSize(nes2->hdr.prgRomSize, nes2->prgRomSizeMsb, PRG_PAGE_SHIFT);
    uint32_t chrRomSize = CalcNes2RomSize(nes2->hdr.chrRomSize, nes2->chrRomSizeMsb, CHR_PAGE_SHIFT);

    /* Trainer, if present (0 or 512 bytes) */
    offs = SetRomSegment(&rdesc->trainer, file->data, offs, nes2->hdr.flags6.trainer ? 512 : 0);

    /* PRG ROM data */
    offs = SetRomSegment(&rdesc->prg, file->data, offs, prgRomSize ? prgRomSize : MAX_PRG_SIZE);

    /* CHR ROM data, if present */
    offs = SetRomSegment(&rdesc->chr, file->data, offs, chrRomSize);

    ParseNesHeaderBase(file, &nes2->hdr, rdesc, offs);

    rdesc->mapperId = rdesc->mapperId | (nes2->mapper.mbs << 8);
    rdesc->submapper = nes2->mapper.submapper;
    rdesc->chrRamSize = nes2->chrRamSize ? 64 << nes2->chrRamSize : 0;

    LogPrintAssert(!nes2->prgRamSize, "NES2: PRG-RAM is not supported. nes2->prgRamSize: %d\n", nes2->prgRamSize);
    LogPrintAssert(!nes2->eepromSize, "NES2: EEPROM is not supported. nes2->eepromSize: %d\n", nes2->eepromSize);
    LogPrintAssert(!nes2->chrNVRamSize, "NES2: CHR-NVRAM is not supported. nes2->chrNVRamSize: %d\n", nes2->chrNVRamSize);
    LogPrintAssert(nes2->timing == NTSC, "NES2: Timing: %d is not supported.\n", nes2->timing);
    LogPrintAssert(!(nes2->hdr.flags7.vsu || nes2->hdr.flags7.pc10), "NES2: Vs. System Type or Extended Console Type is not supported.\n");
    LogPrintAssert(!nes2->miscRom, "NES2: Miscellaneous ROM areas is not supported. miscRom: %d\n", nes2->miscRom);
    LogPrintAssert(nes2->defaultExpDevice == DED_UNSPECIFIED || nes2->defaultExpDevice == DED_STANDARD_NES_CONTROLLERS,
                   "NES2: Default Expansion Device is not supported. defaultExpDevice: %d\n", nes2->defaultExpDevice);

    return 0;
}
