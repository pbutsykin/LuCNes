/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_INTERFACE_
#define __CNES_MAPPER_INTERFACE_

typedef struct _MapperObj MapperObj;
typedef struct _PPUMMap PPUMMap;


MapperObj* MapperInit(uint8_t id, CNesConnector* con);
void MapperFree(MapperObj* mapper);

void MapperPrgBankInitTable(MapperObj* mapper, MMap* mmap, const region_t* prg);

void MapperPrgBankSwitch(MapperObj* mapper, uint16_t cpuAddr, uint8_t val);

void MapperInitMirroring(MapperObj* mapper, PPUMMap* mmap, bool vertical);

#endif /* __CNES_MAPPER_INTERFACE_ */
