/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_PPU_INTERFACE_
#define __CNES_PPU_INTERFACE_

#include <connector.h>
#include <ppu/mmap.h>

typedef struct _MMap MMap;
typedef struct _PPUConfig {
    bool noSpriteLimit;
} PPUConfig;

LuCNesPPU* PpuInit(LuCNesCPU* cpu, RomDesc* rdesc, MapperObj* mapper, void* con, const PPUConfig* cfg);
void PpuFree(LuCNesPPU* ppu);

PPUMMap* PpuMMap(LuCNesPPU* ppu);

void PpuTicksExecute(LuCNesPPU* ppu, const uint8_t cpuCycles);

void* PpuRegRead(void* ppu, MMap* mmap, uint8_t* addr);
void PpuRegWrite(void* ppu, MMap* mmap, uint8_t* addr, uint8_t val);

void* PpuDMARead(void* ppu, MMap* mmap, uint8_t* addr);
void PpuDMAWrite(void* ppu, MMap* mmap, uint8_t* addr, uint8_t val);

bool PpuCheckNMI(LuCNesPPU* ppu);

void PpuDebugDumpState(MMap* mmap);

#endif /* __CNES_PPU_INTERFACE_ */
