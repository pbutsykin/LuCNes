/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_CPU_INTERFACE_
#define __CNES_CPU_INTERFACE_

#include <connector.h>
#include <cpu/mmap.h>

typedef struct _CNesCPU LuCNesCPU;
typedef struct _CNesCPUTestState CNesCPUTestState;

#ifdef CNES_TEST
struct _CNesCPUTestState {
    uint16_t offs;
    uint16_t cycles;
    uint32_t maxCycles;
};
#endif

LuCNesCPU* CpuInit(RomDesc* rdesc, MapperObj* mapper, void* connector, CNesCPUTestState* test);
void CpuFree(LuCNesCPU* cpu);

int32_t CpuMainLoop(LuCNesCPU* cpu);

uint64_t CpuReadCycles(LuCNesCPU* cpu);
void CpuWriteCycles(LuCNesCPU* cpu, uint64_t cycles);

MMap* CpuMMap(LuCNesCPU* cpu);

#endif /* __CNES_CPU_INTERFACE_ */
