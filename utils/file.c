/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define UTILS
#define UTILS_FILE

#include <libgen.h>
#include <utils/utils.h>

static inline uint64_t GetFileSize(int32_t fd)
{
    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    return (uint64_t)end;
}

MFile* MfileGet(char* path, int32_t type, bool fd_close)
{
    uint32_t size;
    int32_t fd;
    MFile* file;

    if ((fd = open(path, type)) < 0) {
        LogPrintErr("Cant't open file: \'%s\' (%s)\n", path, strerror(errno));
        return NULL;
    }

    size = (uint32_t)GetFileSize(fd);
    if (size == 0) {
        LogPrintErr("File is empty: \'%s\'\n", path);
        goto fail;
    }

    file = MemAlloc(sizeof(*file));
    *file = (MFile) {
        .fd = fd,
        .name = basename(path),
        .path = path,
        .data = MemAlloc(size),
        .close = fd_close,
    };
    if ((int32_t)(file->size = read(fd, file->data, size)) < 0) {
        LogPrintErr("Can't read file: \'%s\' (%s)\n", path, strerror(errno));
        MemFree(file->data);
        MemFree(file);
        goto fail;
    }
    LogPrintAssert(file->size == size, "file->size: %u, size: %u\n", file->size, size);

    if (fd_close) {
        close(fd);
        file->fd = -1;
    }
    return file;

fail:
    close(fd);
    return NULL;
}

void MfileClose(MFile* file)
{
    if(!file->close)
        close(file->fd);
    MemFree(file->data);
    MemFree(file);
}
