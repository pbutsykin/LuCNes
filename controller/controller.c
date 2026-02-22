/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_CONTROLLER
#define CONTROLLER

#include <utils/utils.h>
#include <cpu/interface.h>
#include <input/interface.h>

typedef union _PortRead {
    uint8_t val;
    struct {
        uint8_t D0:1;
        uint8_t D1:1;
        uint8_t D2:1;
        uint8_t D3:1;
        uint8_t D4:1;
        uint8_t openBus:3; /* TODO: implement open bus behavior */
    };
} __attribute__((packed)) PortRead;

typedef union _PortWrite {
    uint8_t val;
    struct {
        uint8_t strobe:1;
        uint8_t expLatch:2;
        uint8_t unused0:5;
    };
} PortWrite;

typedef struct _ControllerReg {
    PortRead joy1;
    PortRead joy2;
} __attribute__((packed)) ControllerReg;

typedef struct _ShiftReg {
    uint8_t val;
    uint8_t cnt;
} ShiftReg;

typedef struct _CNesController {
    ControllerReg* reg;
    PortWrite signal;
    ShiftReg shift1;
    ShiftReg shift2;
    void* input;
} LuCNesController;

void ControllerRegWrite(void* controller, __maybe_unused MMap* mmap,
                        __maybe_unused uint8_t* addr, uint8_t val)
{
    LuCNesController* ctl = controller;

    LogPrintAssert(addr == mmap->joy1, "Wrong controller addr write: %td\n", addr - mmap->joy1);

    LogPrintDbg("write value: %x\n", val);

    if (!((PortWrite)val).strobe && ctl->signal.strobe) {
        ctl->shift1.cnt = ctl->shift2.cnt = 0;
        ctl->shift1.val = InputRead(ctl->input, INPUT_PORT_JOY1);
        ctl->shift2.val = InputRead(ctl->input, INPUT_PORT_JOY2);
    }
    ctl->signal.val = val;
}

void* ControllerRegRead(void* controller, MMap* mmap, uint8_t* addr)
{
    PortRead* joy = (PortRead*)addr;
    LuCNesController* ctl = controller;
    bool joy1 = (addr == mmap->joy1);
    ShiftReg* shift = joy1 ? &ctl->shift1 : &ctl->shift2;

    LogPrintAssert(addr == mmap->joy1 || addr == mmap->joy2,
                   "Wrong controller addr read: %td\n", addr - mmap->joy1);

    if (unlikely(ctl->signal.strobe)) {
        joy->val = InputRead(ctl->input, joy1 ? INPUT_PORT_JOY1 : INPUT_PORT_JOY2) & 1;
        return joy;
    }

    if (unlikely(shift->cnt >> CTZ(BYTE_BITS))) {
        joy->val = 1;
        return joy;
    }
    joy->val = (shift->val >> shift->cnt++) & 1;

    return joy;
}

LuCNesController* ControllerInit(LuCNesCPU* cpu, __maybe_unused void* connector)
{
    static LuCNesController ctl;

    ctl = (LuCNesController) {
        .reg = (ControllerReg*)CpuMMap(cpu)->joy1,
        .input = InputInit(),
    };

    return &ctl;
}

void ControllerFree(LuCNesController* ctl)
{
    InputFree(ctl->input);
}
