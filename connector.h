/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_CONNECTOR_
#define __CNES_CONNECTOR_


typedef struct _RomDesc RomDesc;
typedef struct _CNesCPU LuCNesCPU;
typedef struct _CNesPPU LuCNesPPU;
typedef struct _CNesAPU LuCNesAPU;
typedef struct _MapperObj MapperObj;
typedef struct _CNesController LuCNesController;

/* http://wiki.nesdev.com/w/index.php/Cartridge_connector
 *
 * This interface acts as cartridge connector, in particular for the emulator
 * it's a link between subsystems.
 */
typedef struct _CNesConnector {
    struct {
        uint32_t CIRAM_A10:1;
    } pins;

    RomDesc* rdesc;
    MapperObj* mapper;

    LuCNesCPU* cpu;
    LuCNesPPU* ppu;
    LuCNesAPU* apu;
    LuCNesController* ctl;
} CNesConnector;

#endif /* __CNES_CONNECTOR_ */
