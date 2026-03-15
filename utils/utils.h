/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_UTILS_H_
#define __CNES_UTILS_H_

/* default includes */
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __linux__
#include <stdint.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef likely
#if __GNUC__ < 3
#define __builtin_expect(x, n) (x)
#endif

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

/* XXX: need move */
#define EMPTY_FUNC(...) do {} while (0)

/* XXX: need move */
#define STR(_def) #_def

/* XXX: need move */
#define IS_NEG_INT8(_v) ((_v) & 0x80)

/* XXX: need move */
typedef struct _region_t {
    uint32_t size;
    uint8_t* data;
} region_t;

/* XXX: need move */
#define OFFSET_OF(_type, _field) ((uintptr_t)&(((_type*)0)->_field))

#define CONTAINER_OF(_addr, _type, _field) \
    ((_type*)((uint8_t*)_addr - (uint8_t*)OFFSET_OF(_type, _field)))

#define StaticAssert(_assert, _msg) _Static_assert(_assert, _msg)

#define __CMP(x, y, op) ((x) op (y) ? (x) : (y))

#if !defined(MIN) && !defined(MAX)
#define MIN(x, y) __CMP(x, y, <)
#define MAX(x, y) __CMP(x, y, >)
#endif

#define BYTES_TO_BITS(_nb) ((_nb) << 3)
#define BYTE_BITS BYTES_TO_BITS(1)

#define CTZ(_b) __builtin_ctz(_b)

#define __maybe_unused __attribute__((unused))

#define fallthrough __attribute__((__fallthrough__))

static inline uint8_t FindFirstBit64(uint64_t data)
{
    return __builtin_ffsll((int64_t)data);
}

static inline __attribute__((const)) bool IsPowerOf2(unsigned long n)
{
    return (n != 0 && ((n & (n - 1)) == 0));
}

/* utils include */
#include <config.h>
#include "log.h"
#include "memory.h"
#include "file.h"

#endif /* __CNES_UTILS_H_ */ 
