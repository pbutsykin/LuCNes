/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_AXROM_
#define __CNES_MAPPER_AXROM_

#define AXROM_PRG_WIN_SIZE 15 /* Size in bits */
#define AXROM_CHR_WIN_SIZE 0

void AxRomNameTableInit(PPUMMap* mmap, bool);
uint8_t* AxRoomMapperBankSwitch(const MapperObj* mapper, CNesConnector* con, const region_t* reg, const uint8_t* addr, uint8_t val);

#endif /* __CNES_MAPPER_AXROM_ */
