/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_NES_FMT_
#define __CNES_NES_FMT_

bool CheckINESFormat(void* header);
bool CheckNES2Format(void* header);

int INESOpen(MFile* file, RomDesc* rom);
int NES2Open(MFile* file, RomDesc* rdesc);

#endif /* __CNES_NES_FMT_ */
