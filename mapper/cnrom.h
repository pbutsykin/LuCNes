/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#ifndef __CNES_MAPPER_CNROM_
#define __CNES_MAPPER_CNROM_

#define CNROM_PRG_WIN_SIZE 15  /* 32KB fixed PRG */
#define CNROM_CHR_WIN_SIZE 13  /* 8KB CHR bank switching */

void CnRomBankSwitch(const MapperObj* mapper, CNesConnector* con, const region_t* prg, const uint8_t* addr, uint8_t val);

#endif /* __CNES_MAPPER_CNROM_ */
