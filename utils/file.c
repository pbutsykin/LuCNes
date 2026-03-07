/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define UTILS
#define UTILS_FILE

#include <sys/stat.h>
#include <utils/utils.h>

#if HAVE_MMAP
#include <sys/mman.h>
#endif

static inline const char* PathBaseName(const char* p)
{
    const char* base = p;
    while (*p) {
        if (*p++ == '/')
            base = p;
    }
    return base;
}

static inline uint64_t GetFileSize(int fd)
{
    struct stat st;
    if (fstat(fd, &st)) {
        LogPrintErr("fstat failed: %s\n", strerror(errno));
        return 0;
    }
    return (uint64_t)st.st_size;
}

MFile* MfileGet(const char* path, int32_t type)
{
    int fd;
    uint32_t size;
    MFile* file;

    if ((fd = open(path, type)) < 0) {
        LogPrintErr("Cant't open file: \'%s\' (%s)\n", path, strerror(errno));
        return NULL;
    }

    if (!(size = (uint32_t)GetFileSize(fd))) {
        LogPrintErr("File is empty: \'%s\'\n", path);
        close(fd);
        return NULL;
    }

    file = MemAlloc(sizeof(*file));
    *file = (MFile) {
        .size = size,
        .name = PathBaseName(path),
        .path = path,
    };
#if HAVE_MMAP
    file->data = mmap(NULL, file->size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file->data == MAP_FAILED) {
        LogPrintErr("Failed to map file: \'%s\' (%s)\n", path, strerror(errno));
        goto fail;
    }
#else
    file->data = MemAlloc(file->size);
    if (read(fd, file->data, file->size) != file->size) {
        LogPrintErr("Can't read file: \'%s\' (%s)\n", path, strerror(errno));
        MemFree(file->data);
        goto fail;
    }
#endif /* HAVE_MMAP */

    close(fd);
    return file;
fail:
    close(fd);
    MemFree(file);
    return NULL;
}

void MfileClose(MFile* file)
{
#if HAVE_MMAP
    munmap(file->data, file->size);
#else
    MemFree(file->data);
#endif
    MemFree(file);
}
