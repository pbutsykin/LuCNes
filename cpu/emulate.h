/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_EMULATE_
#define __CNES_EMULATE_

int32_t CpuOpcodeExecute(uint8_t currOpcode, CpuReg* reg, MMap* mmap);

void CpuExecuteNMI(CpuReg* reg, MMap* mmap);
void CpuExecuteIRQ(CpuReg* reg, MMap* mmap);

#endif /* __CNES_EMULATE_ */
