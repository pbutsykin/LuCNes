/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_CPU
#define CPU

#include <utils/utils.h>
#include <rom/rom.h>
#include <cpu/interface.h>
#include <ppu/interface.h>
#include <apu/interface.h>
#include <mapper/interface.h>
#include <controller/interface.h>

#include "cpu.h"
#include "emulate.h"

typedef struct _CpuSpaceEmulate {
    uint8_t RAM[KB(2)];    /* $0000-$07FF */
    uint8_t MIRROR[KB(6)]; /* $0800-$1FFF */
    uint8_t PPUReg[8];     /* $2000-$2007 */
    uint8_t APUReg[20];    /* $4000-$4013 */
    uint8_t DMA;       /* $4014 */
    uint8_t SoundChan; /* $4015 */
    uint8_t Joy1;      /* $4016 */
    uint8_t Joy2;      /* $4017 */
    uint8_t DisabledAPUIO[1]; /* $4018-$401F disabled APU test functionallity. (reused for apu frameCounter) */
    uint8_t SRAM[];    /* 0x6000 ... 0x7FFF (Optional) */
} CpuSpaceEmulate;

static void CpuMMapInit(const RomDesc* rdesc, const MapperObj* mapper, MMap* mmap)
{
#define GET_VSPACE_ADDR(_P) ((CpuSpaceEmulate*)_P)

    uint8_t* virtMem = MemAlloc(sizeof(CpuSpaceEmulate) + (rdesc->sram ? KB(8) : 0));
    mmap->ram    = virtMem;
    mmap->ppuReg = GET_VSPACE_ADDR(virtMem)->PPUReg;
    mmap->apuReg = GET_VSPACE_ADDR(virtMem)->APUReg;
    mmap->dma = &GET_VSPACE_ADDR(virtMem)->DMA;
    mmap->soundChan = &GET_VSPACE_ADDR(virtMem)->SoundChan;
    mmap->joy1 = &GET_VSPACE_ADDR(virtMem)->Joy1;
    mmap->joy2 = &GET_VSPACE_ADDR(virtMem)->Joy2;
    mmap->mmio4018 = GET_VSPACE_ADDR(virtMem)->DisabledAPUIO;
    mmap->sRAM = rdesc->sram ? (uint8_t*)&GET_VSPACE_ADDR(virtMem)->SRAM : NULL;
    MapperPrgBankInitTable(mapper, mmap, &rdesc->prg);

#undef GET_VSPACE_ADDR
}

#define ZERO_PAGE  0x0000 ... 0xFF
#define CPU_RAM    0x0000 ... 0x7FF
#define CPU_STACK  0x0100 ... 0x1FF
#define RAM_MIRROR 0x0800 ... 0x1FFF /* Mirrors of 0000-07FF */
#define PPU_REG    0x2000 ... 0x2007
#define PPUCTRL    0x2000
#define PPU_STATUS 0x2002
#define PPU_ADDR   0x2006
#define PPU_DATA   0x2007
#define PPU_MIRROR 0x2008 ... 0x3FFF
#define SOUND_REG  0x4000 ... 0x4013
#define CPU_DMA    0x4014
#define SOUND_CHAN 0x4015
#define JOY_PAD1   0x4016
#define JOY_PAD2   0x4017 /* APU or Joypad register */
#define APU_IO     0x4018 ... 0x401F /* APU and I/O functionality that is normally disabled. */
#define PRG_RAM    0x6000 ... 0x7FFF
#define PRG_ROM    0x8000 ... 0xFFFF
#define PRG_ADDR   0x8000

enum {
    MASK_SREG = 0xFF,
    MASK_PPU  = 0x7,
    MASK_SRAM = 0x1FFF,
    MASK_PRG  = 0x7FFF
};

typedef struct _CpuMappedDevMemory {
    void* addr;
    void* ctx;
    void* (*cpuRead)(void* ctx, MMap* mmap, uint8_t* addr);
    void (*cpuWrite8)(void* ctx, MMap* mmap, uint8_t* addr, uint8_t val);
    void (*cpuWrite16)(void* ctx, MMap* mmap, uint16_t* addr, uint16_t val);
} CpuMappedDevMemory;

static inline void* CpuDevMemReadN(void* _ __maybe_unused, MMap* __ __maybe_unused, uint8_t* addr)
{
    return addr;
}

static inline void CpuDevMemWrite8(void* _ __maybe_unused, MMap* __ __maybe_unused, uint8_t* addr, uint8_t val)
{
    *addr = val;
}

static inline void CpuDevMemWrite16(void* _ __maybe_unused, MMap* __ __maybe_unused, uint16_t* addr, uint16_t val)
{
    *addr = val;
}

static inline void CpuDevMapperReload8(void* ctx, MMap* mmap __maybe_unused, uint8_t* addr, uint8_t val)
{
    CNesConnector* con = ctx;

    LogPrintDbg("Update bank: %d\n",  val);
    MapperPrgBankSwitch(con, &con->rdesc->prg, addr, val);
}

static inline void CpuDevMapperReload16(void* ctx, MMap* mmap, uint16_t* addr, uint16_t val)
{
    LogPrintAssert(0, "Is write16 used to switch banks?");
    CpuDevMapperReload8(ctx, mmap, (uint8_t*)addr, (uint8_t)val);
}

static inline void CpuSyncDevices(LuCNesCPU* cpu, uint8_t cycles)
{
    if (unlikely(!cycles))
        return;

    cpu->cycles += cycles;
    PpuTicksExecute(cpu->con->ppu, cycles);
    ApuTicksExecute(cpu->con->apu, cycles);
    cpu->ioInsnCycles = 0;
}

static CpuMappedDevMemory CpuSlowResolveAddr(LuCNesCPU* cpu, MMap* mmap, uint16_t addr, bool write)
{
    CpuMappedDevMemory mdev = {
        .cpuRead = CpuDevMemReadN,
        .cpuWrite8 = CpuDevMemWrite8,
        .cpuWrite16 = CpuDevMemWrite16,
    };

    switch(addr) {
        case CPU_RAM:
            mdev.addr = mmap->ram + addr;
            break;
        case PPU_REG:
        case PPU_MIRROR:
            mdev = (CpuMappedDevMemory) {
                .addr = mmap->ppuReg + (addr & MASK_PPU),
                .ctx = cpu->con->ppu,
                .cpuRead = PpuRegRead,
                .cpuWrite8 = PpuRegWrite,
            };
            CpuSyncDevices(cpu, cpu->ioInsnCycles);
            break;
        case SOUND_REG:
            mdev = (CpuMappedDevMemory) {
                .addr = mmap->apuReg + (addr & MASK_SREG),
                .ctx = cpu->con->apu,
                .cpuWrite8 = ApuRegWrite,
            };
            CpuSyncDevices(cpu, cpu->ioInsnCycles);
            break;
        case CPU_DMA:
            mdev = (CpuMappedDevMemory) {
                .addr = mmap->dma,
                .ctx = cpu->con->ppu,
                .cpuRead = PpuDMARead,
                .cpuWrite8 = PpuDMAWrite,
            };
            CpuSyncDevices(cpu, cpu->ioInsnCycles);
            break;
        case SOUND_CHAN:
            mdev = (CpuMappedDevMemory) {
                .addr = mmap->soundChan,
                .ctx = cpu->con->apu,
                .cpuRead = ApuRegRead,
                .cpuWrite8 = ApuRegWrite,
            };
            CpuSyncDevices(cpu, cpu->ioInsnCycles);
            break;
        case JOY_PAD1:
            mdev = (CpuMappedDevMemory) {
                .addr = mmap->joy1,
                .ctx = cpu->con->ctl,
                .cpuRead = ControllerRegRead,
                .cpuWrite8 = ControllerRegWrite,
            };
            break;
        case JOY_PAD2:
            /* $4017: Read = Joypad 2, Write = APU Frame Counter */
            if (write) {
                mdev = (CpuMappedDevMemory) {
                    .addr = mmap->mmio4018, /* abuse $4018 */
                    .ctx = cpu->con->apu,
                    .cpuWrite8 = ApuRegWrite,
                };
                CpuSyncDevices(cpu, cpu->ioInsnCycles);
            } else {
                mdev = (CpuMappedDevMemory) {
                    .addr = mmap->joy2,
                    .ctx = cpu->con->ctl,
                    .cpuRead = ControllerRegRead,
                };
            }
            break;
        case PRG_RAM:
            if (unlikely(!mmap->sRAM))
                mmap->sRAM = MemAlloc(KB(8));

            mdev.addr = mmap->sRAM + (addr & MASK_SRAM); /* TODO: mirror for sRAM size < 8kb */
            break;
        case PRG_ROM:
            if (unlikely(write)) {
                mdev.ctx = cpu->con;
                mdev.cpuWrite8 = CpuDevMapperReload8;
                mdev.cpuWrite16 = CpuDevMapperReload16;
            }
            mdev.addr = MMapPrgResolve(mmap, addr);
            break;
        default: 
            mdev = (CpuMappedDevMemory){0};
            LogPrintAssert(0, "Address 0x%x not resolved\n", addr);
            break;
    }
    return mdev;
}

inline uint16_t CpuMemRead16(MMap* mmap, uint16_t addr)
{
    LuCNesCPU* cpu = CONTAINER_OF(mmap, LuCNesCPU, mmap);
    CpuMappedDevMemory mdev;
        /* Slow path for non-contiguous 8kb prg bank boundaries.*/
    if (unlikely((addr & PRG_BANK_MASK) == PRG_BANK_MASK && addr > PRG_ADDR)) {
        uint8_t lo = CpuMemRead8(mmap, addr), hi = CpuMemRead8(mmap, addr + 1);
        return (uint16_t)lo | ((uint16_t)hi << 8);
    }
    cpu->ioInsnCycles += 2;
    mdev = CpuSlowResolveAddr(cpu, mmap, addr, false);
    return *(uint16_t*)mdev.cpuRead(mdev.ctx, mmap, mdev.addr);
}

inline uint8_t CpuMemRead8(MMap* mmap, uint16_t addr)
{
    LuCNesCPU* cpu = CONTAINER_OF(mmap, LuCNesCPU, mmap);
    CpuMappedDevMemory mdev;

    cpu->ioInsnCycles++;
    mdev = CpuSlowResolveAddr(cpu, mmap, addr, false);
    return *(uint8_t*)mdev.cpuRead(mdev.ctx, mmap, mdev.addr);
}

inline void CpuMemWrite16(MMap* mmap, uint16_t addr, uint16_t val)
{
    LuCNesCPU* cpu = CONTAINER_OF(mmap, LuCNesCPU, mmap);
    CpuMappedDevMemory mdev;

    cpu->ioInsnCycles += 2;
    mdev = CpuSlowResolveAddr(cpu, mmap, addr, true);
    mdev.cpuWrite16(mdev.ctx, mmap, mdev.addr, val);
}

inline void CpuMemWrite8(MMap* mmap, uint16_t addr, uint8_t val)
{
    LuCNesCPU* cpu = CONTAINER_OF(mmap, LuCNesCPU, mmap);
    CpuMappedDevMemory mdev;

    cpu->ioInsnCycles++;
    mdev = CpuSlowResolveAddr(cpu, mmap, addr, true);
    mdev.cpuWrite8(mdev.ctx, mmap, mdev.addr, val);
}

inline static void CpuRegistersReset(MMap* mmap, CpuReg* reg)
{
#define END_OF_STACK 0xFD

    reg->A = reg->X = reg->Y = 0;
    reg->S = END_OF_STACK;
    reg->PC = CpuMemRead16(mmap, INTERRUPT_RESET);
    reg->P.val = 1 << 5; /* P5 flag is never used and is always 1 */
    reg->P.val |= IFLAG_MASK;

#undef END_OF_STACK
}

static void CpuDebugDumpReg(__maybe_unused CpuReg* reg)
{
    PRINT_FIELD_HEX8(reg->A);
    PRINT_FIELD_HEX8(reg->X);
    PRINT_FIELD_HEX8(reg->Y);
    PRINT_FIELD_HEX8(reg->S);
    PRINT_FIELD_HEX16(reg->PC);
    PRINT_FIELD_HEX8(reg->P.val);
    LogPrintDbg("FLAGS: N:%u V:%u P5:%u B:%u D:%u I:%u Z:%u C:%u\n",
                reg->P.N, reg->P.V, reg->P.P5, reg->P.B, reg->P.D,
                reg->P.I, reg->P.Z, reg->P.C);
}

void CpuDebugDumpState(LuCNesCPU* cpu)
{
    LogPrintDbg("cpu dump:\n");
    CpuDebugDumpReg(&cpu->reg);
}

static void CpuInitTestState(__maybe_unused LuCNesCPU* cpu, __maybe_unused CNesCPUTestState* test)
{
#ifdef CNES_TEST
    if (test) {
        cpu->reg.PC = test->offs ?: cpu->reg.PC;
        cpu->cycles = test->cycles ?: cpu->cycles;
        cpu->maxCycles = test->maxCycles ?: 0;
    }
#define StopTest(_cpu) ((_cpu)->maxCycles && (_cpu)->cycles > (_cpu)->maxCycles)
#else
#define StopTest(_) false
#endif
}

LuCNesCPU* CpuInit(RomDesc* rdesc, MapperObj* mapper, void* connector, CNesCPUTestState* test)
{
    LuCNesCPU* cpu = MemAlloc(sizeof(*cpu));
    *cpu = (LuCNesCPU) {
        .con = connector,
    };
    CpuMMapInit(rdesc, mapper, &cpu->mmap);
    CpuRegistersReset(&cpu->mmap, &cpu->reg);
    CpuInitTestState(cpu, test);
    CpuDebugDumpState(cpu);
    cpu->ioInsnCycles = 0;
    cpu->irqDisabled = cpu->reg.P.I;

    return cpu;
}

void CpuFree(LuCNesCPU* cpu)
{
    assert(cpu != NULL);

    if (cpu->mmap.sRAM && !cpu->con->rdesc->sram)
        MemFree(cpu->mmap.sRAM); /* release lazy allocated sRAM */
    MemFree(cpu->mmap.ram);
    MemFree(cpu);
}

MMap* CpuMMap(LuCNesCPU* cpu)
{
    return &cpu->mmap;
}

uint64_t CpuReadCycles(LuCNesCPU* cpu)
{
    return cpu->cycles;
}

void CpuWriteCycles(LuCNesCPU* cpu, uint64_t cycles)
{
    cpu->cycles = cycles;
}

int32_t CpuMainLoop(LuCNesCPU* cpu)
{
    CpuReg* reg = &cpu->reg;
    MMap* mmap = &cpu->mmap;
    LuCNesPPU* ppu = cpu->con->ppu;

    do {
        uint8_t currOpcode;
        int32_t opCycles;

        if (StopTest(cpu))
            break;

        if (unlikely(PpuCheckNMI(ppu))) {
            CpuExecuteNMI(reg, mmap);
            CpuSyncDevices(cpu, 7);
        }

        if (unlikely(!cpu->irqDisabled && ApuCheckIRQ(cpu->con->apu))) {
            CpuExecuteIRQ(reg, mmap);
            CpuSyncDevices(cpu, 7);
        }

        LogPrintAssert(!cpu->ioInsnCycles, "cpu->cycles: %ld, opCycles: %d\n", cpu->cycles, cpu->ioInsnCycles);

        currOpcode = CpuMemRead8(mmap, reg->PC++);
        opCycles = CpuOpcodeExecute(currOpcode, reg, mmap);
        if (unlikely(opCycles < 0))
            break;

        CpuSyncDevices(cpu, opCycles);
    } while(true);

    return 0;
}
