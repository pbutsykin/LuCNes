/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_UTILS_FILE_
#define __CNES_UTILS_FILE_

#define KB_BIT 10
#define MB_BIT 20
#define GB_BIT 30
#define KB(_n) (_n << KB_BIT)
#define MB(_n) (_n << MB_BIT)
#define GB(_n) (_n << GB_BIT)

#define MFile_MAX_SIZE MB(1)

typedef struct _MFile {
    uint8_t* data;
    uint32_t size;
    char*    name;
    char*    path;
} MFile;

#define MFILE_TYPE_READ  O_RDONLY | O_BINARY
#define MFILE_TYPE_WRITE O_WRONLY | O_BINARY
#define MFILE_TYPE_RW    O_RDWR   | O_BINARY

MFile* MfileGet(char* path, int32_t type);

void MfileClose(MFile* file);

/* DEBUG */
#if LOG_LEVEL >= LOG_LVL_DEBUG
static inline void MfilePrint(MFile* file)
{
    PRINT_FIELD_STRING(file->path);
    PRINT_FIELD_STRING(file->name);
    PRINT_FIELD_SIZE(file->size);
    PRINT_FIELD_POINTER(file->data);
    PRINT_FIELD_DATA_4(file->data);
}
#else
#define MfilePrint EMPTY_FUNC
#endif

#endif /* __CNES_UTILS_FILE_ */
