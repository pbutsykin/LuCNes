/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define UTILS
#define UTILS_MEMORY

#include <stdlib.h>
#include <utils/utils.h>

void* MemAlloc(size_t size)
{
#ifdef ZMEMORY
    void* ptr = calloc(1, size);
#else
    void* ptr = malloc(size);
#endif
    if (ptr)
        return ptr;

    LogPrintErr("OOM: malloc(%zu) failed: %s\n", size, strerror(errno));
    exit(1);
}

void MemFree(void* ptr)
{
    free(ptr);
}
