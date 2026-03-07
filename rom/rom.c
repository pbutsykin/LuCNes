/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_ROM
#define ROM

#include <utils/utils.h>

#include "rom.h"
#include "nes.h"

enum {
    ROM_FORMAT_UNKNOWN = 0,
    ROM_FORMAT_INES    = 1,
    ROM_FORMAT_NES2    = 2,
    ROM_FORMAT_MAX
};

typedef bool (FmtExt)(void* header);
typedef int32_t (RomLoad)(MFile* file, RomDesc* rom);

typedef struct _RomFmtObj {
    uint32_t type;
    uint8_t* sign;
    uint32_t size;
    FmtExt*  extension;
    RomLoad* load;
} RomFmtObj;

__maybe_unused static char* rtype_to_str(uint32_t type)
{
    static char* str_rom[ROM_FORMAT_MAX] = {
                        STR(ROM_FORMAT_UNKNOWN),
                        STR(ROM_FORMAT_INES),
                        STR(ROM_FORMAT_INES20)
    };

    if (type >= ROM_FORMAT_MAX) {
        return "Invalid format of the rom file";
    }
    return str_rom[type];
}

static RomFmtObj g_rom_formats[];

static RomFmtObj* GetRomFormatObj(MFile* file)
{
    uint32_t n = 0;
    uint8_t* header = file->data;
    RomFmtObj* fmt;

    while((fmt = &g_rom_formats[n++])->sign != NULL) {
        if (memcmp(header, fmt->sign, fmt->size) == 0) {
            if (fmt->extension == NULL) {
                return fmt;
            }
            if (fmt->extension(header)) {
                return fmt;
            }
        }
    }
    return fmt;
}

void UnloadRomFile(RomDesc* rdesc)
{
    assert(rdesc != NULL);
    MfileClose(rdesc->rfile);
    MemFree(rdesc);
}

RomDesc* LoadRomFile(char* fname)
{
    assert(fname != NULL);

    RomFmtObj* rom;
    RomDesc* rdesc;
    MFile* file = MfileGet(fname, MFILE_TYPE_READ);
    if (file == NULL) {
        return NULL;
    }
    MfilePrint(file);

    rom = GetRomFormatObj(file);
    LogPrintDbg("type rom: %s\n", rtype_to_str(rom->type));
    if (rom->type == ROM_FORMAT_UNKNOWN)
        goto fail1;

    rdesc = MemAlloc(sizeof(*rdesc));
    if (rom->load(file, rdesc) < 0)
        goto fail2;

    rdesc->rfile = file;

    return rdesc;

fail2:
    MemFree(rdesc);
fail1:
    MfileClose(file);
    return NULL;
}

#define ADD_FORMAT_ROM(_type, _sign, _ext, _open) \
    {                                      \
        .type = _type,                     \
        .sign = (uint8_t*)_sign,           \
        .size = sizeof(_sign) - 1,         \
        .extension = &_ext,                \
        .load = &_open,                    \
    }

static RomFmtObj g_rom_formats[] = {
    ADD_FORMAT_ROM(ROM_FORMAT_INES, "NES\x1a", CheckINESFormat, INESOpen),
    ADD_FORMAT_ROM(ROM_FORMAT_NES2, "NES\x1a", CheckNES2Format, NES2Open),
    {ROM_FORMAT_UNKNOWN, NULL, 0, NULL, NULL} /* end */
};
