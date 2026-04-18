/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_CPU_
#define __CNES_CPU_

#include "mmap.h"

enum {
    CFLAG_SHIFT = 0, /* Carry */
    ZFLAG_SHIFT = 1, /* Zero */
    IFLAG_SHIFT = 2, /* Interrupt Disable */
    DFLAG_SHIFT = 3, /* Decimal */
    BFLAG_SHIFT = 4,
    UFLAG_SHIFT = 5,
    VFLAG_SHIFT = 6, /* Overflow */
    NFLAG_SHIFT = 7, /* Negative */
};

enum {
    CFLAG_MASK = 1 << CFLAG_SHIFT,
    ZFLAG_MASK = 1 << ZFLAG_SHIFT,
    IFLAG_MASK = 1 << IFLAG_SHIFT,
    DFLAG_MASK = 1 << DFLAG_SHIFT,
    BFLAG_MASK = 1 << BFLAG_SHIFT,
    UFLAG_MASK = 1 << UFLAG_SHIFT,
    VFLAG_MASK = 1 << VFLAG_SHIFT,
    NFLAG_MASK = 1 << NFLAG_SHIFT,
};

enum {
    INTERRUPT_NMI   = 0xFFFA,
    INTERRUPT_RESET = 0xFFFC,
    INTERRUPT_IRQ   = 0xFFFE
};

typedef struct _CpuReg {
    uint16_t PC; /*  Instruction Pointer */
    uint8_t  A;
    uint8_t  X;
    uint8_t  Y;
    uint8_t  S; /* stack */
    union {
        uint8_t val;
        struct {
            uint8_t C:1;
            uint8_t Z:1;
            uint8_t I:1;
            uint8_t D:1;
            uint8_t B:1;
            uint8_t P5:1; /* unused */
            uint8_t V:1;
            uint8_t N:1;
        };
    } P;
} CpuReg;

typedef struct _CNesCPU {
    CpuReg reg;
    MMap mmap;
    uint64_t cycles;
    uint8_t ioInsnCycles;
    bool irqDisabled;

    CNesConnector* con;
#ifdef CNES_TEST
    uint32_t maxCycles;
#endif
} LuCNesCPU;

uint8_t CpuMemRead8(MMap* mmap, uint16_t addr);
uint16_t CpuMemRead16(MMap* mmap, uint16_t addr);
void CpuMemWrite8(MMap* mmap, uint16_t addr, uint8_t val);

void CpuDebugDumpState(LuCNesCPU* cpu);

#endif /* __CNES_CPU_ */
