/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_AXROM_
#define __CNES_MAPPER_AXROM_

#define AXROM_PRG_WIN_SIZE 15 /* Size in bits */
#define AXROM_CHR_WIN_SIZE 0

void AxRomNameTableInit(MapperObj* mapper, PPUMMap* mmap, bool);
void AxRoomMapperBankSwitch(MapperObj* mapper, uint16_t cpuAddr, uint8_t val);

#endif /* __CNES_MAPPER_AXROM_ */
