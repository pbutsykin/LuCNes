/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2016 Pavel Butsykin
 */
#ifndef __CNES_CPU_MMAP_
#define __CNES_CPU_MMAP_

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

    struct {
        uint8_t* addr; /* $8000-$FFFF */
        uint16_t offsMask;
        uint8_t bank;
    } prg;
} MMap;

#endif /* __CNES_CPU_MMAP_ */
