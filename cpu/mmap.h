/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2016 Pavel Butsykin
 */
#ifndef __CNES_CPU_MMAP_
#define __CNES_CPU_MMAP_

#define PRG_BANK_SHIFT 13
#define PRG_BANK_MASK  ((1 << PRG_BANK_SHIFT) - 1)
#define PRG_BANK_WIN_MAX 4
#define PRG_BANK_WIN_MASK (PRG_BANK_WIN_MAX - 1)

typedef struct _MMap {
    uint8_t* ram;       /* $0000-$07FF */
    uint8_t* ppuReg;    /* $2000-$2007 */
    uint8_t* apuReg;    /* $4000-$4013 */
    uint8_t* dma;       /* $4014 */
    uint8_t* soundChan; /* $4015 */
    uint8_t* joy1;      /* $4016 */
    uint8_t* joy2;      /* $4017 */
    uint8_t* mmio4018;  /* $4018-$401F (disabled) */
    uint8_t* expROM;
    uint8_t* sRAM; /* $6000-$7FFF */

    uint8_t* prgBankTable[PRG_BANK_WIN_MAX]; /* 4 × 8KB = $8000-$FFFF */
} MMap;

static inline uint8_t* MMapPrgResolve(const MMap* mmap, uint16_t addr)
{

    return mmap->prgBankTable[(addr >> PRG_BANK_SHIFT) & PRG_BANK_WIN_MASK] + (addr & PRG_BANK_MASK);
}

#endif /* __CNES_CPU_MMAP_ */
