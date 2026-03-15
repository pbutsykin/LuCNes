/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_MMC1_
#define __CNES_MAPPER_MMC1_

#define MMC1_PRG_WIN_SIZE 15  /* 32kb window (16kb banking internally) */
#define MMC1_CHR_WIN_SIZE 12  /* 4kb chr */

MapperObj* Mmc1MapperInit(void);
void Mmc1PrgBankInitTable(MapperObj* base, MMap* mmap, const region_t* prg);
void Mmc1InitMirroring(MapperObj* base, PPUMMap* mmap, bool vertical);
void Mmc1BankSwitch(MapperObj* base, uint16_t cpuAddr, uint8_t val);

#endif /* __CNES_MAPPER_MMC1_ */
