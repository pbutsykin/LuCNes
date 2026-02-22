/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#ifndef __CNES_CONTROLLER_INTERFACE_
#define __CNES_CONTROLLER_INTERFACE_

void ControllerRegWrite(void* ctl, MMap* mmap, uint8_t* addr, uint8_t val);
void* ControllerRegRead(void* ctl, MMap* mmap, uint8_t* addr);

LuCNesController* ControllerInit(LuCNesCPU* cpu, void* connector);
void ControllerFree(LuCNesController* ctl);

#endif /* __CNES_CONTROLLER_INTERFACE_ */
