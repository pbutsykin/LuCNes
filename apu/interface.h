/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 Pavel Butsykin
 */
#ifndef __CNES_APU_INTERFACE_
#define __CNES_APU_INTERFACE_

#include <connector.h>

typedef struct _MMap MMap;

void ApuTicksExecute(LuCNesAPU* apu, const uint8_t cpuCycles);

void ApuRegWrite(void* apu, MMap* mmap, uint8_t* addr, uint8_t val);
void* ApuRegRead(void* apu, MMap* mmap, uint8_t* addr);

bool ApuCheckIRQ(LuCNesAPU* apu);

LuCNesAPU* ApuInit(LuCNesCPU* cpu, void* connector);
void ApuFree(LuCNesAPU* apu);

#endif /* __CNES_APU_INTERFACE_ */
