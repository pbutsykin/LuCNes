/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_UXROM_
#define __CNES_MAPPER_UXROM_

#define UXROM_PRG_WIN_SIZE 14  /* 16KB switchable prg bank */
#define UXROM_CHR_WIN_SIZE 0   /* 8KB fixed CHR */

void UxRomPrgInit(MapperObj* mapper, MMap* mmap, const region_t* prg);
void UxRomBankSwitch(MapperObj* mapper, uint16_t cpuAddr, uint8_t val);

#endif /* __CNES_MAPPER_UXROM_ */

