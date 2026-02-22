/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_INTERFACE_
#define __CNES_MAPPER_INTERFACE_

typedef struct _MapperObj MapperObj;
typedef struct _PPUMMap PPUMMap;

MapperObj* MapperLookupById(uint8_t id);

uint16_t MapperPrgOffsetMask(const MapperObj* mapper, uint32_t prgSize);

uint8_t* MapperPrgBankSwitch(CNesConnector* con, const region_t* prg,
                             const uint8_t* addr, uint8_t val);

void MapperInitMirroring(const MapperObj* mapper, PPUMMap* mmap, bool vertical);

#endif /* __CNES_MAPPER_INTERFACE_ */
