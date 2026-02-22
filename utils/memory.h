/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_UTILS_MEMORY_
#define __CNES_UTILS_MEMORY_

void* MemAlloc(size_t size);
void MemFree(void* ptr);

#endif /* __CNES_UTILS_MEMORY_ */
