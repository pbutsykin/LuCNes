/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#define CNES_CPU
#define CPU_EMULATE

#include <utils/utils.h>
#include <ppu/interface.h>

#include "cpu.h"

#define FLAG_SETV(_flag, _type) \
    ((_flag).val | _type##_MASK)

#define FLAG_CLEARV(_flag, _type) \
    ((_flag).val & ~_type##_MASK)

#define FLAG_SET(_flag, _type) \
    (_flag).val = FLAG_SETV(_flag, _type)

#define FLAG_CLEAR(_flag, _type) \
    (_flag).val = FLAG_CLEARV(_flag, _type)

#define NFLAG_CHECK(_reg) ((_reg) & NFLAG_MASK)

#define ZFLAG_CHECK(_reg) (!(_reg))

#define CFLAG_CHECK(_reg) (_reg)

#define VFLAG_CHECK(_reg) IS_NEG_INT8(_reg)

#define FLAG_UPDATE(_flag, _type, _cmp) \
    (_flag).val = _type##_CHECK(_cmp) ? FLAG_SETV(_flag, _type) : FLAG_CLEARV(_flag, _type)

#define IS_FLAG(_flag, _type) \
    ((_flag).val & _type##_MASK)

#define FLAG_UPDATE_OWN_CMP(_flag, _type, _cmp) \
    (_flag).val = (_cmp) ? FLAG_SETV(_flag, _type) : FLAG_CLEARV(_flag, _type)

#define CPU_LARGE_CACHE

#if defined(CPU_LITTLE_CACHE)
    #define VFLAG_CPU_CHECK(_A, _B, _R) \
        ((_A ^ _R) & (_B ^ _R))
#elif defined(CPU_LARGE_CACHE)
    #define VFLAG_CPU_CHECK(_A, _B, _R) \
        (IS_NEG_INT8(_A ^ _B) ? 0 : IS_NEG_INT8(_A) ? ~_R : _R)
#else
    #define VFLAG_CPU_CHECK(_A, _B, _R) \
        (IS_NEG_INT8(_R) ? ~(_A | _B) : _A & _B)
#endif

#define VFLAG_ADD(_A, _VAL, _SUB) \
    VFLAG_CPU_CHECK(_A, _VAL, _SUB)

#define VFLAG_SUB(_A, _VAL, _SUB) \
    VFLAG_CPU_CHECK(_SUB, _VAL, _A)

#define PAGE_CROSS_MASK 0xff00 /* Page size = 0xff */

static inline bool PageCrossing(const uint16_t addr1, const uint16_t addr2)
{
    return !!((addr1 ^ addr2) & PAGE_CROSS_MASK);
}

#define STACK_BASE_ADDR 0x100

static inline void push8(uint8_t val, CpuReg* reg, MMap* mmap)
{
    /* Note: 6502 stack can wrap around (S: 0x00 -> 0xFF). */
    CpuMemWrite8(mmap, STACK_BASE_ADDR + reg->S--, val);
}

static inline uint8_t pop8(CpuReg* reg, MMap* mmap)
{
    /* Note: 6502 stack can wrap around (S: 0xFF -> 0x00). */
    return CpuMemRead8(mmap, STACK_BASE_ADDR + ++reg->S);
}

static inline void push16(uint16_t val, CpuReg* reg, MMap* mmap)
{
    CpuMemWrite16(mmap, STACK_BASE_ADDR + reg->S - 1, val);
    reg->S -= 2;
}

static inline uint16_t pop16(CpuReg* reg, MMap* mmap)
{
    reg->S += 2;
    uint16_t val = CpuMemRead16(mmap, STACK_BASE_ADDR + reg->S - 1);
    return val;
}

static inline void CpuDummyCycle(MMap* mmap)
{
    CONTAINER_OF(mmap, LuCNesCPU, mmap)->ioInsnCycles++;
}

static inline uint16_t ZeroPageRead16(MMap* mmap, uint8_t addr)
{
    if (unlikely(addr == 0xff))
        return CpuMemRead8(mmap, 0xff) | (CpuMemRead8(mmap, 0) << 8);
    return CpuMemRead16(mmap, addr);
}

/* Base instruction implementation */
static inline void ora_base(CpuReg* reg, uint8_t val)
{
    reg->A |= val;
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline uint8_t asl_base(CpuReg* reg, uint8_t val)
{
    FLAG_UPDATE(reg->P, CFLAG, IS_NEG_INT8(val));
    val <<= 1;
    FLAG_UPDATE(reg->P, NFLAG, val);
    FLAG_UPDATE(reg->P, ZFLAG, val);

    return val;
}

static inline uint8_t branch_base(CpuReg* reg, MMap* mmap, bool take)
{
    if (take) {
        int8_t offs = (int8_t)CpuMemRead8(mmap, reg->PC++);
        uint16_t oldPC = reg->PC;
        reg->PC += offs;
        return PageCrossing(oldPC, reg->PC) ? 2 : 1;
    }
    reg->PC++;
    return 0;
}

static inline void bit_base(CpuReg* reg, uint8_t val)
{
    FLAG_UPDATE_OWN_CMP(reg->P, VFLAG, val & VFLAG_MASK);
    FLAG_UPDATE_OWN_CMP(reg->P, NFLAG, val & NFLAG_MASK);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A & val);
}

static inline uint8_t rol_base(CpuReg* reg, uint8_t val)
{
    uint8_t cflag = IS_FLAG(reg->P, CFLAG);
    FLAG_UPDATE(reg->P, CFLAG, IS_NEG_INT8(val));
    val  = (val << 1) | cflag;
    FLAG_UPDATE(reg->P, NFLAG, val);
    FLAG_UPDATE(reg->P, ZFLAG, val);

    return val;
}

static inline void and_base(CpuReg* reg, uint8_t val)
{
    reg->A &= val;
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline uint8_t lsr_base(CpuReg* reg, uint8_t val)
{
    FLAG_UPDATE(reg->P, CFLAG, val & 1);
    val >>= 1;
    FLAG_UPDATE(reg->P, NFLAG, val);
    FLAG_UPDATE(reg->P, ZFLAG, val);

    return val;
}

static inline void eor_base(CpuReg* reg, uint8_t val)
{
    reg->A ^= val;
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline void adc_base(CpuReg* reg, uint8_t val)
{
    uint8_t cflag = IS_FLAG(reg->P, CFLAG);
    uint8_t add = reg->A + val + cflag;

    FLAG_UPDATE(reg->P, VFLAG, VFLAG_ADD(reg->A, val, add));
    FLAG_UPDATE(reg->P, CFLAG, reg->A > add - cflag);
    FLAG_UPDATE(reg->P, NFLAG, add);
    FLAG_UPDATE(reg->P, ZFLAG, add);
    reg->A = add;
}

static inline uint8_t ror_base(CpuReg* reg, uint8_t val)
{
    uint8_t cflag = IS_FLAG(reg->P, CFLAG) << 7;
    FLAG_UPDATE(reg->P, CFLAG, val & 1);
    val  = (val >> 1) | cflag;
    FLAG_UPDATE(reg->P, NFLAG, val);
    FLAG_UPDATE(reg->P, ZFLAG, val);

    return val;
}

static inline uint8_t ld_base(CpuReg* reg, uint8_t val)
{
    FLAG_UPDATE(reg->P, NFLAG, val);
    FLAG_UPDATE(reg->P, ZFLAG, val);

    return val;
}

static inline void cmp_base(CpuReg* reg, uint8_t val)
{
    uint8_t sub = reg->A - val;

    FLAG_UPDATE(reg->P, CFLAG, reg->A >= val);
    FLAG_UPDATE(reg->P, NFLAG, sub);
    FLAG_UPDATE(reg->P, ZFLAG, sub);
}

static inline void dec_base(CpuReg* reg, MMap* mmap, uint16_t addr)
{
    uint8_t val = CpuMemRead8(mmap, addr) - 1;
    FLAG_UPDATE(reg->P, NFLAG, val);
    FLAG_UPDATE(reg->P, ZFLAG, val);
    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
}

static inline void cpn_base(CpuReg* reg, uint8_t val, uint8_t n)
{
    uint8_t sub = n - val;

    FLAG_UPDATE(reg->P, CFLAG, n >= val);
    FLAG_UPDATE(reg->P, NFLAG, sub);
    FLAG_UPDATE(reg->P, ZFLAG, sub);
}

static inline void sbc_base(CpuReg* reg, uint8_t val) /* XXX: optimize */
{
    uint8_t cflag = IS_FLAG(reg->P, CFLAG);
    uint8_t sub = reg->A - val - !cflag;

    FLAG_UPDATE(reg->P, VFLAG, VFLAG_SUB(reg->A, val, sub)); /* maybe need consider the CFLAG */
    FLAG_UPDATE(reg->P, CFLAG, reg->A >= val + !cflag); /* perhaps remove the cflag stack var? */
    FLAG_UPDATE(reg->P, NFLAG, sub);
    FLAG_UPDATE(reg->P, ZFLAG, sub);
    reg->A = sub;
}

static inline void inc_base(CpuReg* reg, MMap* mmap, uint16_t addr)
{
    uint8_t val = CpuMemRead8(mmap, addr) + 1;
    FLAG_UPDATE(reg->P, NFLAG, val);
    FLAG_UPDATE(reg->P, ZFLAG, val);
    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
}

static inline void dcp_base(CpuReg* reg, MMap* mmap, uint16_t addr)
{
    uint8_t val = CpuMemRead8(mmap, addr) - 1;
    uint8_t sub = reg->A - val;

    FLAG_UPDATE(reg->P, CFLAG, val <= reg->A);
    FLAG_UPDATE(reg->P, NFLAG, sub);
    FLAG_UPDATE(reg->P, ZFLAG, sub);

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
}

static inline void slo_base(CpuReg* reg, MMap* mmap, uint16_t addr)
{
    uint8_t val = CpuMemRead8(mmap, addr);

    FLAG_UPDATE(reg->P, CFLAG, IS_NEG_INT8(val));
    val <<= 1;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->A |= val;
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline void rla_base(CpuReg* reg, MMap* mmap, uint16_t addr)
{
    uint8_t val = CpuMemRead8(mmap, addr);

    uint8_t cflag = IS_FLAG(reg->P, CFLAG);
    FLAG_UPDATE(reg->P, CFLAG, IS_NEG_INT8(val));
    val  = (val << 1) | cflag;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->A &= val;
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline void sre_base(CpuReg* reg, MMap* mmap, uint16_t addr)
{
    uint8_t val = CpuMemRead8(mmap, addr);

    FLAG_UPDATE(reg->P, CFLAG, val & 1);
    val >>= 1;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    eor_base(reg, val);
}

static inline void rra_base(CpuReg* reg, MMap* mmap, uint16_t addr)
{
    uint8_t val = CpuMemRead8(mmap, addr);
    uint8_t cflag = IS_FLAG(reg->P, CFLAG) << 7;

    FLAG_UPDATE(reg->P, CFLAG, val & 1);
    val  = (val >> 1) | cflag;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    adc_base(reg, val);
}

/* Final instruction implementation */
static inline void ora_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    ora_base(reg, CpuMemRead8(mmap, addr));
}

static inline void slo_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;

    CpuDummyCycle(mmap);
    slo_base(reg, mmap, ZeroPageRead16(mmap, offs));
}

static inline void nop2(CpuReg* reg __maybe_unused, MMap* mmap __maybe_unused)
{
    reg->PC++;
}

static inline void ora_z(CpuReg* reg, MMap* mmap)
{
    ora_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void slo_z(CpuReg* reg, MMap* mmap)
{
    slo_base(reg, mmap, CpuMemRead8(mmap, reg->PC++));
}

static inline void php(CpuReg* reg, MMap* mmap)
{   /* https://wiki.nesdev.com/w/index.php?title=Status_flags */
    uint8_t flags = reg->P.val | BFLAG_MASK;
    push8(flags, reg, mmap);
}

static inline void ora_c(CpuReg* reg, MMap* mmap)
{
    ora_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void asl_z(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++);
    uint8_t val = asl_base(reg, CpuMemRead8(mmap, addr));
    CpuMemWrite8(mmap, addr, val);
}

static inline void asl(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->A = asl_base(reg, reg->A);
}

static inline void nop3(CpuReg* reg __maybe_unused, MMap* mmap __maybe_unused)
{
    reg->PC+=2;
}

static inline void ora_a(CpuReg* reg, MMap* mmap)
{
    ora_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void asl_a(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC);
    uint8_t val = asl_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->PC += 2;
}

static inline void slo_a(CpuReg* reg, MMap* mmap)
{
    slo_base(reg, mmap, CpuMemRead16(mmap, reg->PC));
    reg->PC += 2;
}

static inline uint8_t bpl(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, !IS_FLAG(reg->P, NFLAG));
}

static inline void ora_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);

    ora_base(reg, CpuMemRead8(mmap, addr));
}

static inline void slo_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    CpuDummyCycle(mmap);
    slo_base(reg, mmap, addr + reg->Y);
}

static inline void nop4(CpuReg* reg __maybe_unused, MMap* mmap __maybe_unused)
{
    reg->PC++;
}

static inline void ora_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    ora_base(reg, CpuMemRead8(mmap, addr));
}

static inline void asl_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint8_t val = asl_base(reg, CpuMemRead8(mmap, addr));
    CpuMemWrite8(mmap, addr, val);
}

static inline void slo_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    slo_base(reg, mmap, addr);
}

static inline void clc(CpuReg* reg, MMap* mmap __maybe_unused)
{
    FLAG_CLEAR(reg->P, CFLAG);
}

static inline void ora_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    ora_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void slo_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->Y;
    CpuDummyCycle(mmap);
    slo_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void nop0(CpuReg* reg __maybe_unused, MMap* mmap __maybe_unused)
{
    /* Nothing to do */
}

static inline uint8_t nop6(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);

    reg->PC+=2;
    return (uint8_t)PageCrossing(base, base + reg->X);
}

static inline void ora_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    ora_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void asl_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    uint8_t val;

    CpuDummyCycle(mmap);
    val = asl_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->PC += 2;
}

static inline void slo_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    CpuDummyCycle(mmap);
    slo_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void jsr(CpuReg* reg, MMap* mmap)
{
    /* SR pushes the address-1 of the next  operation on to the stack before
     * transferring program control to the following address.
     */
    push16(reg->PC + 2 - 1, reg, mmap);
    reg->PC = CpuMemRead16(mmap, reg->PC);
}

static inline void and_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    and_base(reg, CpuMemRead8(mmap, addr));
}

static inline void rla_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    rla_base(reg, mmap, addr);
}

static inline void bit_z(CpuReg* reg, MMap* mmap)
{
    bit_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void rol_z(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++);
    uint8_t val = rol_base(reg, CpuMemRead8(mmap, addr));
    CpuMemWrite8(mmap, addr, val);
}

static inline void and_z(CpuReg* reg, MMap* mmap)
{
    and_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void rla_z(CpuReg* reg, MMap* mmap)
{
    rla_base(reg, mmap, CpuMemRead8(mmap, reg->PC++));
}

static inline void plp(CpuReg* reg, MMap* mmap)
{
    reg->P.val = pop8(reg, mmap);
    FLAG_CLEAR(reg->P, BFLAG);
    FLAG_SET(reg->P, UFLAG);
}

static inline void and_c(CpuReg* reg, MMap* mmap)
{
    and_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void rol(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->A = rol_base(reg, reg->A);
}

static inline void bit_a(CpuReg* reg, MMap* mmap)
{
    bit_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void and_a(CpuReg* reg, MMap* mmap)
{
    and_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void rol_a(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC);
    uint8_t val = rol_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->PC += 2;
}

static inline void rla_a(CpuReg* reg, MMap* mmap)
{
    rla_base(reg, mmap, CpuMemRead16(mmap, reg->PC));
    reg->PC += 2;
}

static inline uint8_t bmi(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, IS_FLAG(reg->P, NFLAG));
}

static inline void and_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;
    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    and_base(reg, CpuMemRead8(mmap, addr));
}

static inline void rla_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    CpuDummyCycle(mmap);
    rla_base(reg, mmap, base + reg->Y);
}

static inline void and_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    and_base(reg, CpuMemRead8(mmap, addr));
}

static inline void rol_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint8_t val = rol_base(reg, CpuMemRead8(mmap, addr));
    CpuMemWrite8(mmap, addr, val);
}

static inline void rla_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    rla_base(reg, mmap, addr);
}

static inline void sec(CpuReg* reg, MMap* mmap __maybe_unused)
{
    FLAG_SET(reg->P, CFLAG);
}

static inline void and_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    and_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void rla_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->Y;
    CpuDummyCycle(mmap);
    rla_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void and_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    and_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void rol_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    uint8_t val;

    CpuDummyCycle(mmap);
    val = rol_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->PC += 2;
}

static inline void rla_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    CpuDummyCycle(mmap);
    rla_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void rti(CpuReg* reg, MMap* mmap)
{
    reg->P.val = pop8(reg, mmap);
    reg->PC = pop16(reg, mmap);

    FLAG_CLEAR(reg->P, BFLAG);
    FLAG_SET(reg->P, UFLAG);
    CONTAINER_OF(reg, LuCNesCPU, reg)->irqDisabled = reg->P.I;
}

static inline void eor_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    eor_base(reg, CpuMemRead8(mmap, addr));
    /* Always constant cycles (6) */
}

static inline void sre_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    sre_base(reg, mmap, addr);
}

static inline void lsr_z(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++);
    uint8_t val = lsr_base(reg, CpuMemRead8(mmap, addr));
    CpuMemWrite8(mmap, addr, val);
}

static inline void sre_z(CpuReg* reg, MMap* mmap)
{
    sre_base(reg, mmap, CpuMemRead8(mmap, reg->PC++));
}

static inline void pha(CpuReg* reg, MMap* mmap)
{
    push8(reg->A, reg, mmap);
}

static inline void eor_z(CpuReg* reg, MMap* mmap)
{
    eor_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void eor_c(CpuReg* reg, MMap* mmap)
{
    eor_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void lsr(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->A = lsr_base(reg, reg->A);
}

static inline void jmp(CpuReg* reg, MMap* mmap)
{
    reg->PC = CpuMemRead16(mmap, reg->PC);
}

static inline void eor_a(CpuReg* reg, MMap* mmap)
{
    eor_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void lsr_a(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC);
    uint8_t val = lsr_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->PC += 2;
}

static inline void sre_a(CpuReg* reg, MMap* mmap)
{
    sre_base(reg, mmap, CpuMemRead16(mmap, reg->PC));
    reg->PC += 2;
}

static inline uint8_t bvc(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, !IS_FLAG(reg->P, VFLAG));
}

static inline void eor_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    eor_base(reg, CpuMemRead8(mmap, addr));
}

static inline void sre_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    CpuDummyCycle(mmap);
    sre_base(reg, mmap, addr + reg->Y);
}

static inline void eor_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    eor_base(reg, CpuMemRead8(mmap, addr));
    /* Constant cycles */
}

static inline void lsr_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint8_t val = lsr_base(reg, CpuMemRead8(mmap, addr));

    CpuMemWrite8(mmap, addr, val);
}

static inline void sre_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    sre_base(reg, mmap, addr);
}

static inline void eor_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    eor_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void sre_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->Y;
    CpuDummyCycle(mmap);
    sre_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void eor_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    eor_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void lsr_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    uint8_t val;

    CpuDummyCycle(mmap);
    val = lsr_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
    reg->PC += 2;
}

static inline void sre_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    CpuDummyCycle(mmap);
    sre_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void rts(CpuReg* reg, MMap* mmap)
{
    /* RTS pulls the top two bytes off the stack (low byte first) and transfers
     * program control to that address+1. It is used, as expected, to exit
     *  a subroutine invoked via JSR which pushed the address-1.
     */
    reg->PC = pop16(reg, mmap) + 1;
}

static inline void adc_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    adc_base(reg, CpuMemRead8(mmap, addr));
}

static inline void rra_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    rra_base(reg, mmap, addr);
}

static inline void adc_z(CpuReg* reg, MMap* mmap)
{
    adc_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void ror_z(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++);
    uint8_t val = ror_base(reg, CpuMemRead8(mmap, addr));
    CpuMemWrite8(mmap, addr, val);
}

static inline void rra_z(CpuReg* reg, MMap* mmap)
{
    rra_base(reg, mmap, CpuMemRead8(mmap, reg->PC++));
}

static inline void pla(CpuReg* reg, MMap* mmap)
{
    reg->A = pop8(reg, mmap);
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline void adc_c(CpuReg* reg, MMap* mmap)
{
    adc_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void ror(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->A = ror_base(reg, reg->A);
}

static inline void jmp_i(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC);
    reg->PC = (addr & 0xff) == 0xff ? (CpuMemRead8(mmap, addr & 0xff00) << 8) | CpuMemRead8(mmap, addr)
                                    : CpuMemRead16(mmap, addr);
}

static inline void adc_a(CpuReg* reg, MMap* mmap)
{
    adc_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void ror_a(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC);
    uint8_t val = ror_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    reg->PC += 2;
}

static inline void rra_a(CpuReg* reg, MMap* mmap)
{
    rra_base(reg, mmap, CpuMemRead16(mmap, reg->PC));
    reg->PC += 2;
}

static inline uint8_t bvs(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, IS_FLAG(reg->P, VFLAG));
}

static inline void adc_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    adc_base(reg, CpuMemRead8(mmap, addr));
}

static inline void rra_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    CpuDummyCycle(mmap);
    rra_base(reg, mmap, base + reg->Y);
}

static inline void adc_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    adc_base(reg, CpuMemRead8(mmap, addr));
}

static inline void ror_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint8_t val = ror_base(reg, CpuMemRead8(mmap, addr));
    CpuMemWrite8(mmap, addr, val);
}

static inline void rra_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    rra_base(reg, mmap, addr);
}

static inline void cli(CpuReg* reg, MMap* mmap __maybe_unused)
{
    FLAG_CLEAR(reg->P, IFLAG);
}

static inline void sei(CpuReg* reg, MMap* mmap __maybe_unused)
{
    FLAG_SET(reg->P, IFLAG);
}

static inline void adc_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    adc_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void rra_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->Y;
    CpuDummyCycle(mmap);
    rra_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void adc_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    adc_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void ror_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    uint8_t val;

    CpuDummyCycle(mmap);
    val = ror_base(reg, CpuMemRead8(mmap, addr));

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
    reg->PC += 2;
}

static inline void rra_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    CpuDummyCycle(mmap);
    rra_base(reg, mmap, addr);
    reg->PC += 2;
}

static inline void nop5(CpuReg* reg __maybe_unused, MMap* mmap __maybe_unused)
{
    reg->PC++;
}

static inline void sta_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);

    CpuMemWrite8(mmap, addr, reg->A);
}

static inline void sax_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;
    uint8_t val = reg->A & reg->X;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);

    CpuMemWrite8(mmap, addr, val);
}

static inline void sty_z(CpuReg* reg, MMap* mmap)
{
    CpuMemWrite8(mmap, CpuMemRead8(mmap, reg->PC++), reg->Y);
}

static inline void sta_z(CpuReg* reg, MMap* mmap)
{
    CpuMemWrite8(mmap, CpuMemRead8(mmap, reg->PC++), reg->A);
}

static inline void stx_z(CpuReg* reg, MMap* mmap)
{
    CpuMemWrite8(mmap, CpuMemRead8(mmap, reg->PC++), reg->X);
}

static inline void sax_z(CpuReg* reg, MMap* mmap)
{
    uint8_t val = reg->A & reg->X;
    CpuMemWrite8(mmap, CpuMemRead8(mmap, reg->PC++), val);
}

static inline void dey(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->Y--;
    FLAG_UPDATE(reg->P, NFLAG, reg->Y);
    FLAG_UPDATE(reg->P, ZFLAG, reg->Y);
}

static inline void txa(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->A = reg->X;
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline void sty_a(CpuReg* reg, MMap* mmap)
{
    CpuMemWrite8(mmap, CpuMemRead16(mmap, reg->PC), reg->Y);
    reg->PC += 2;
}

static inline void sta_a(CpuReg* reg, MMap* mmap)
{
    CpuMemWrite8(mmap, CpuMemRead16(mmap, reg->PC), reg->A);
    reg->PC += 2;
}

static inline void stx_a(CpuReg* reg, MMap* mmap)
{
    CpuMemWrite8(mmap, CpuMemRead16(mmap, reg->PC), reg->X);
    reg->PC += 2;
}

static inline void sax_a(CpuReg* reg, MMap* mmap)
{
    uint8_t val = reg->A & reg->X;
    CpuMemWrite8(mmap, CpuMemRead16(mmap, reg->PC), val);
    reg->PC += 2;
}

static inline uint8_t bcc(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, !IS_FLAG(reg->P, CFLAG));
}

static inline void sty_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    CpuMemWrite8(mmap, addr, reg->Y);
}

static inline void sta_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, base + reg->Y, reg->A);
}

static inline void sta_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    CpuMemWrite8(mmap, addr, reg->A);
}

static inline void stx_zy(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->Y;
    CpuMemWrite8(mmap, addr, reg->X);
}

static inline void sax_zy(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->Y;
    uint8_t val = reg->A & reg->X;
    CpuMemWrite8(mmap, addr, val);
}

static inline void tya(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->A = reg->Y;
    FLAG_UPDATE(reg->P, NFLAG, reg->A);
    FLAG_UPDATE(reg->P, ZFLAG, reg->A);
}

static inline void sta_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->Y;
    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, reg->A);
    reg->PC += 2;
}

static inline void txs(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->S = reg->X;
}

static inline void sta_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, reg->A);
    reg->PC += 2;
}

static inline void ldy_c(CpuReg* reg, MMap* mmap)
{
    reg->Y = ld_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void lda_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);

    reg->A = ld_base(reg, CpuMemRead8(mmap, addr));
}

static inline void ldx_c(CpuReg* reg, MMap* mmap)
{
    reg->X = ld_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void lax_ix(CpuReg* reg, MMap* mmap)
{
    lda_ix(reg, mmap);

    reg->X = reg->A;
}

static inline void ldy_z(CpuReg* reg, MMap* mmap)
{
    reg->Y = ld_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void lda_z(CpuReg* reg, MMap* mmap)
{
    reg->A = ld_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void ldx_z(CpuReg* reg, MMap* mmap)
{
    reg->X = ld_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void lax_z(CpuReg* reg, MMap* mmap)
{
    lda_z(reg, mmap);
    reg->X = reg->A;
}

static inline void tay(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->Y = reg->A;
    FLAG_UPDATE(reg->P, NFLAG, reg->Y);
    FLAG_UPDATE(reg->P, ZFLAG, reg->Y);
}

static inline void lda_c(CpuReg* reg, MMap* mmap)
{
    reg->A = ld_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void tax(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->X = reg->A;
    FLAG_UPDATE(reg->P, NFLAG, reg->X);
    FLAG_UPDATE(reg->P, ZFLAG, reg->X);
}

static inline void ldy_a(CpuReg* reg, MMap* mmap)
{
    reg->Y = ld_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void lda_a(CpuReg* reg, MMap* mmap)
{
    reg->A = ld_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void ldx_a(CpuReg* reg, MMap* mmap)
{
    reg->X = ld_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void lax_a(CpuReg* reg, MMap* mmap)
{
    lda_a(reg, mmap);
    reg->X = reg->A;
}

static inline void cpy_c(CpuReg* reg, MMap* mmap)
{
    cpn_base(reg, CpuMemRead8(mmap, reg->PC++), reg->Y);
}

static inline void cmp_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    cmp_base(reg, CpuMemRead8(mmap, addr));
}

static inline void dcp_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    dcp_base(reg, mmap, addr);
}

static inline void cpy_z(CpuReg* reg, MMap* mmap)
{
    cpn_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)), reg->Y);
}

static inline void cmp_z(CpuReg* reg, MMap* mmap)
{
    cmp_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void dec_z(CpuReg* reg, MMap* mmap)
{
    dec_base(reg, mmap, CpuMemRead8(mmap, reg->PC++));
}

static inline void dcp_z(CpuReg* reg, MMap* mmap)
{
    dcp_base(reg, mmap, CpuMemRead8(mmap, reg->PC++));
}

static inline void iny(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->Y++;
    FLAG_UPDATE(reg->P, NFLAG, reg->Y);
    FLAG_UPDATE(reg->P, ZFLAG, reg->Y);
}

static inline void cmp_c(CpuReg* reg, MMap* mmap)
{
    cmp_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void cmp_a(CpuReg* reg, MMap* mmap)
{
    cmp_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void dex(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->X--;
    FLAG_UPDATE(reg->P, NFLAG, reg->X);
    FLAG_UPDATE(reg->P, ZFLAG, reg->X);
}

static inline void cpy_a(CpuReg* reg, MMap* mmap)
{
    cpn_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)), reg->Y);
    reg->PC += 2;
}

static inline void dec_a(CpuReg* reg, MMap* mmap)
{
    dec_base(reg, mmap, CpuMemRead16(mmap, reg->PC));
    reg->PC += 2;
}

static inline void dcp_a(CpuReg* reg, MMap* mmap)
{
    dcp_base(reg, mmap, CpuMemRead16(mmap, reg->PC));
    reg->PC += 2;
}

static inline uint8_t bcs(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, IS_FLAG(reg->P, CFLAG));
}

static inline void lda_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    reg->A = ld_base(reg, CpuMemRead8(mmap, addr));
}

static inline void lax_iy(CpuReg* reg, MMap* mmap)
{
    lda_iy(reg, mmap);
    reg->X = reg->A;
}

static inline void ldy_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    reg->Y = ld_base(reg, CpuMemRead8(mmap, addr));
}

static inline void lda_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    reg->A = ld_base(reg, CpuMemRead8(mmap, addr));
}

static inline void ldx_zy(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->Y;
    reg->X = ld_base(reg, CpuMemRead8(mmap, addr));
}

static inline void lax_zy(CpuReg* reg, MMap* mmap)
{
    ldx_zy(reg, mmap);
    reg->A = reg->X;
}

static inline void clv(CpuReg* reg, MMap* mmap __maybe_unused)
{
    FLAG_CLEAR(reg->P, VFLAG);
}

static inline void lda_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    reg->A = ld_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void tsx(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->X = reg->S;
    FLAG_UPDATE(reg->P, NFLAG, reg->X);
    FLAG_UPDATE(reg->P, ZFLAG, reg->X);
}

static inline void ldy_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    reg->Y = ld_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void lda_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    reg->A = ld_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void ldx_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    reg->X = ld_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void lax_ay(CpuReg* reg, MMap* mmap)
{
    lda_ay(reg, mmap);
    reg->X = reg->A;
}

static inline uint8_t bne(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, !IS_FLAG(reg->P, ZFLAG));
}

static inline void cmp_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    cmp_base(reg, CpuMemRead8(mmap, addr));
}

static inline void dcp_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    CpuDummyCycle(mmap);
    dcp_base(reg, mmap, base + reg->Y);
}

static inline void cmp_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    cmp_base(reg, CpuMemRead8(mmap, addr));
}

static inline void dec_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    dec_base(reg, mmap, addr);
}

static inline void dcp_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    dcp_base(reg, mmap, addr);
}

static inline void cld(CpuReg* reg, MMap* mmap __maybe_unused)
{
    FLAG_CLEAR(reg->P, DFLAG);
}

static inline void cmp_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    cmp_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void dcp_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);

    CpuDummyCycle(mmap);
    dcp_base(reg, mmap, base + reg->Y);
    reg->PC += 2;
}

static inline void dec_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);

    CpuDummyCycle(mmap);
    dec_base(reg, mmap, base + reg->X);
    reg->PC += 2;
}

static inline void cmp_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    cmp_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void dcp_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);

    CpuDummyCycle(mmap);
    dcp_base(reg, mmap, base + reg->X);
    reg->PC += 2;
}

static inline void cpx_c(CpuReg* reg, MMap* mmap)
{
    cpn_base(reg, CpuMemRead8(mmap, reg->PC++), reg->X);
}

static inline void sbc_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    sbc_base(reg, CpuMemRead8(mmap, addr));
}

static inline void isb_ix(CpuReg* reg, MMap* mmap)
{
    uint8_t offs = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint16_t addr;
    uint8_t val;

    CpuDummyCycle(mmap);
    addr = ZeroPageRead16(mmap, offs);
    val = (int8_t)CpuMemRead8(mmap, addr) + 1;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    sbc_base(reg, val);
}

static inline void cpx_z(CpuReg* reg, MMap* mmap)
{
    cpn_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)), reg->X);
}

static inline void sbc_z(CpuReg* reg, MMap* mmap)
{
    sbc_base(reg, CpuMemRead8(mmap, CpuMemRead8(mmap, reg->PC++)));
}

static inline void inc_z(CpuReg* reg, MMap* mmap)
{
    inc_base(reg, mmap, CpuMemRead8(mmap, reg->PC++));
}

static inline void isb_z(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead8(mmap, reg->PC++);
    uint8_t val = (int8_t)CpuMemRead8(mmap, addr) + 1;

    CpuMemWrite8(mmap, addr, val);
    sbc_base(reg, val);
}

static inline void inx(CpuReg* reg, MMap* mmap __maybe_unused)
{
    reg->X++;
    FLAG_UPDATE(reg->P, NFLAG, reg->X);
    FLAG_UPDATE(reg->P, ZFLAG, reg->X);
}

static inline void sbc_c(CpuReg* reg, MMap* mmap)
{
    sbc_base(reg, CpuMemRead8(mmap, reg->PC++));
}

static inline void nop(CpuReg* reg __maybe_unused, MMap* mmap __maybe_unused)
{
    /* Nothing to do */
}

static inline void cpx_a(CpuReg* reg, MMap* mmap)
{
    cpn_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)), reg->X);
    reg->PC += 2;
}

static inline void inc_a(CpuReg* reg, MMap* mmap)
{
    inc_base(reg, mmap, CpuMemRead16(mmap, reg->PC));
    reg->PC += 2;
}

static inline void sbc_a(CpuReg* reg, MMap* mmap)
{
    sbc_base(reg, CpuMemRead8(mmap, CpuMemRead16(mmap, reg->PC)));
    reg->PC += 2;
}

static inline void isb_a(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC);
    uint8_t val = CpuMemRead8(mmap, addr) + 1;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
    sbc_base(reg, val);

    reg->PC += 2;
}

static inline uint8_t beq(CpuReg* reg, MMap* mmap)
{
    return branch_base(reg, mmap, IS_FLAG(reg->P, ZFLAG));
}

static inline void sbc_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    sbc_base(reg, CpuMemRead8(mmap, addr));
}

static inline void isb_iy(CpuReg* reg, MMap* mmap)
{
    uint16_t base = ZeroPageRead16(mmap, CpuMemRead8(mmap, reg->PC++));
    uint16_t addr = base + reg->Y;
    uint8_t val;

    CpuDummyCycle(mmap);
    val = (int8_t)CpuMemRead8(mmap, addr) + 1;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);

    sbc_base(reg, val);
}

static inline void sbc_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    sbc_base(reg, CpuMemRead8(mmap, addr));
}

static inline void inc_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    inc_base(reg, mmap, addr);
}

static inline void isb_zx(CpuReg* reg, MMap* mmap)
{
    uint8_t addr = CpuMemRead8(mmap, reg->PC++) + reg->X;
    uint8_t val = (int8_t)CpuMemRead8(mmap, addr) + 1;

    CpuMemWrite8(mmap, addr, val);
    sbc_base(reg, val);
}

static inline void sed(CpuReg* reg, MMap* mmap __maybe_unused)
{
    FLAG_SET(reg->P, DFLAG);
}

static inline void sbc_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->Y;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    sbc_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void isb_ay(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->Y;
    uint8_t val;

    CpuDummyCycle(mmap);
    val = CpuMemRead8(mmap, addr) + 1;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
    sbc_base(reg, val);

    reg->PC += 2;
}

static inline void sbc_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    uint16_t addr = base + reg->X;

    if (unlikely(PageCrossing(base, addr)))
        CpuDummyCycle(mmap);
    sbc_base(reg, CpuMemRead8(mmap, addr));
    reg->PC += 2;
}

static inline void inc_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t base = CpuMemRead16(mmap, reg->PC);
    CpuDummyCycle(mmap);
    inc_base(reg, mmap, base + reg->X);
    reg->PC += 2;
}

static inline void isb_ax(CpuReg* reg, MMap* mmap)
{
    uint16_t addr = CpuMemRead16(mmap, reg->PC) + reg->X;
    uint8_t val;

    CpuDummyCycle(mmap);
    val = CpuMemRead8(mmap, addr) + 1;

    CpuDummyCycle(mmap);
    CpuMemWrite8(mmap, addr, val);
    sbc_base(reg, val);

    reg->PC += 2;
}

#if OPCODE_TRACE
static uint32_t opcodeCnt;
static inline void OpcodeTrace(char* opcode, uint8_t size, CpuReg* reg, MMap* mmap)
{
    LuCNesCPU* _cpu = CONTAINER_OF(mmap, LuCNesCPU, mmap);
    uint8_t savedOpCycles = _cpu->ioInsnCycles;

    if(opcodeCnt++ == 0xffffffff) {
        LogPrintAssert(0, "opcodeCnt overflow: %x", opcodeCnt);
    }
    printf("%04x: %-6s [%02x] ", reg->PC - 1, opcode, CpuMemRead8(mmap, reg->PC - 1));
    if (size == 2) {
        printf("%02x", CpuMemRead8(mmap, reg->PC));
    } else if (size == 3) {
        printf("%04x", CpuMemRead16(mmap, reg->PC));
    }

    printf("\tA:%02x X:%02x Y:%02x S:%x P:%c%c%c%c%c%c%c%c  ", reg->A, reg->X, reg->Y, reg->S,
           reg->P.N ? 'N' : 'n', reg->P.V ? 'V' : 'v', reg->P.P5 ? 'U' : 'u',
           reg->P.B ? 'B' : 'b', reg->P.D ? 'D' : 'd', reg->P.I ? 'I' : 'i',
           reg->P.Z ? 'Z' : 'z', reg->P.C ? 'C' : 'c');


    printf("cycles: %lu\n", CONTAINER_OF(reg, LuCNesCPU, reg)->cycles);
    _cpu->ioInsnCycles = savedOpCycles;
}
static inline void OpcodeCntPrint(void)
{
    LogPrintDbg("Opcode count: %u\n", opcodeCnt);
}
#else
#define OpcodeTrace(_op, _sz, _reg, _m)
#define OpcodeCntPrint()
#endif

void CpuExecuteNMI(CpuReg* reg, MMap* mmap)
{
    push16(reg->PC, reg, mmap);

    /* B flag isn't real cpu flag. It's only pushed onto the stack to to distinguish IRQ from BRK. */
    LogPrintAssert(!IS_FLAG(reg->P, BFLAG), "Someone is using B flag! See brk or php instructions\n");
    push8(reg->P.val, reg, mmap);
    reg->PC = CpuMemRead16(mmap, INTERRUPT_NMI);

    FLAG_SET(reg->P, IFLAG);
    CONTAINER_OF(reg, LuCNesCPU, reg)->irqDisabled = true;
}

void CpuExecuteIRQ(CpuReg* reg, MMap* mmap)
{
    push16(reg->PC, reg, mmap);

    /* B flag is 0 for IRQ (distinguishes from BRK) */
    LogPrintAssert(!IS_FLAG(reg->P, BFLAG), "Someone is using B flag! See brk or php instructions\n");
    push8(reg->P.val, reg, mmap);
    reg->PC = CpuMemRead16(mmap, INTERRUPT_IRQ);

    FLAG_SET(reg->P, IFLAG);
}

/* XXX: Slow processing */
int32_t CpuOpcodeExecute(uint8_t currOpcode, CpuReg* reg, MMap* mmap)
{
#define OPOCDE_RUN_PGC(_opcode, _handler, _size, _cycles) \
        cycles = _handler(reg, mmap) + _cycles

#define OPOCDE_RUN_IORW(_opcode, _handler, _size, _cycles) \
        _handler(reg, mmap);                               \
        cycles = cpu->ioInsnCycles;                        \
        LogPrintAssert(cycles <= _cycles, "invalid iorw insn cycles.\n")

#define OPOCDE_RUN_(_opcode, _handler, _size, _cycles) \
            _handler(reg, mmap);                       \
            cycles = _cycles

#define OP_RUN_CHOSE_MACROS(_arg1, _arg2, _def, ...) _def##_arg2

#define OPOCDE_RUN_CASE(_opcode, _handler, _size, _cycles, ...) \
        case _opcode:                                           \
            OpcodeTrace(#_handler, _size, reg, mmap);           \
            OP_RUN_CHOSE_MACROS(0, ##__VA_ARGS__,               \
                                OPOCDE_RUN_,                    \
                                 )(_opcode, _handler, _size, _cycles); \
            break

    int32_t cycles;
    LuCNesCPU* cpu = CONTAINER_OF(reg, LuCNesCPU, reg);

    cpu->irqDisabled = reg->P.I;

    switch (currOpcode) {
                   /* |opcode |name|size|cycles| */
        OPOCDE_RUN_CASE(0x01, ora_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x03, slo_ix, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x04, nop2,   2, 3);
        OPOCDE_RUN_CASE(0x05, ora_z,  2, 3);
        OPOCDE_RUN_CASE(0x07, slo_z,  2, 5);
        OPOCDE_RUN_CASE(0x08, php,    1, 3);
        OPOCDE_RUN_CASE(0x09, ora_c,  2, 2);
        OPOCDE_RUN_CASE(0x06, asl_z,  2, 5);
        OPOCDE_RUN_CASE(0x0A, asl,    1, 2);
        OPOCDE_RUN_CASE(0x0C, nop3,   3, 4);
        OPOCDE_RUN_CASE(0x0D, ora_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x0E, asl_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x0F, slo_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x10, bpl,    2, 2, PGC);
        OPOCDE_RUN_CASE(0x11, ora_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x13, slo_iy, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x14, nop4,   2, 4);
        OPOCDE_RUN_CASE(0x15, ora_zx, 2, 4);
        OPOCDE_RUN_CASE(0x16, asl_zx, 2, 6);
        OPOCDE_RUN_CASE(0x17, slo_zx, 2, 6);
        OPOCDE_RUN_CASE(0x18, clc,    1, 2);
        OPOCDE_RUN_CASE(0x19, ora_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x1A, nop0,   1, 2);
        OPOCDE_RUN_CASE(0x1B, slo_ay, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x1C, nop6,   3, 4, PGC);
        OPOCDE_RUN_CASE(0x1D, ora_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x1E, asl_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x1F, slo_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x20, jsr,    3, 6);
        OPOCDE_RUN_CASE(0x21, and_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x23, rla_ix, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x24, bit_z,  2, 3);
        OPOCDE_RUN_CASE(0x26, rol_z,  2, 5);
        OPOCDE_RUN_CASE(0x25, and_z,  2, 3);
        OPOCDE_RUN_CASE(0x27, rla_z,  2, 5);
        OPOCDE_RUN_CASE(0x28, plp,    1, 4);
        OPOCDE_RUN_CASE(0x29, and_c,  2, 2);
        OPOCDE_RUN_CASE(0x2A, rol,    1, 2);
        OPOCDE_RUN_CASE(0x2C, bit_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x2D, and_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x2E, rol_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x2F, rla_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x30, bmi,    2, 2, PGC);
        OPOCDE_RUN_CASE(0x31, and_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x33, rla_iy, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x34, nop4,   2, 4);
        OPOCDE_RUN_CASE(0x35, and_zx, 2, 4);
        OPOCDE_RUN_CASE(0x36, rol_zx, 2, 6);
        OPOCDE_RUN_CASE(0x37, rla_zx, 2, 6);
        OPOCDE_RUN_CASE(0x38, sec,    1, 2);
        OPOCDE_RUN_CASE(0x39, and_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x3A, nop0,   1, 2);
        OPOCDE_RUN_CASE(0x3B, rla_ay, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x3C, nop6,   3, 4, PGC);
        OPOCDE_RUN_CASE(0x3D, and_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x3E, rol_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x3F, rla_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x40, rti, 1, 6);
        OPOCDE_RUN_CASE(0x41, eor_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x43, sre_ix, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x44, nop2,   2, 3);
        OPOCDE_RUN_CASE(0x45, eor_z,  2, 3);
        OPOCDE_RUN_CASE(0x46, lsr_z,  2, 5);
        OPOCDE_RUN_CASE(0x47, sre_z,  2, 5);
        OPOCDE_RUN_CASE(0x48, pha,    1, 3);
        OPOCDE_RUN_CASE(0x49, eor_c,  2, 2);
        OPOCDE_RUN_CASE(0x4A, lsr,    1, 2);
        OPOCDE_RUN_CASE(0x4C, jmp,    3, 3);
        OPOCDE_RUN_CASE(0x4D, eor_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x4E, lsr_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x4F, sre_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x50, bvc,    2, 2, PGC);
        OPOCDE_RUN_CASE(0x54, nop4,   2, 4);
        OPOCDE_RUN_CASE(0x51, eor_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x53, sre_iy, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x55, eor_zx, 2, 4);
        OPOCDE_RUN_CASE(0x56, lsr_zx, 2, 6);
        OPOCDE_RUN_CASE(0x57, sre_zx, 2, 6);
        OPOCDE_RUN_CASE(0x58, cli,    1, 2);
        OPOCDE_RUN_CASE(0x59, eor_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x5A, nop0,   1, 2);
        OPOCDE_RUN_CASE(0x5B, sre_ay, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x5C, nop6,   3, 4, PGC);
        OPOCDE_RUN_CASE(0x5D, eor_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x5E, lsr_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x5F, sre_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x60, rts,    1, 6);
        OPOCDE_RUN_CASE(0x61, adc_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x63, rra_ix, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x64, nop2,   2, 3);
        OPOCDE_RUN_CASE(0x65, adc_z,  2, 3);
        OPOCDE_RUN_CASE(0x66, ror_z,  2, 5);
        OPOCDE_RUN_CASE(0x67, rra_z,  2, 5);
        OPOCDE_RUN_CASE(0x68, pla,    1, 4);
        OPOCDE_RUN_CASE(0x69, adc_c,  2, 2);
        OPOCDE_RUN_CASE(0x6A, ror,    1, 2);
        OPOCDE_RUN_CASE(0x6C, jmp_i,  3, 5);
        OPOCDE_RUN_CASE(0x6D, adc_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x6E, ror_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x6F, rra_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0x70, bvs,    2, 2, PGC);
        OPOCDE_RUN_CASE(0x71, adc_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x73, rra_iy, 2, 8, IORW);
        OPOCDE_RUN_CASE(0x74, nop4,   2, 4);
        OPOCDE_RUN_CASE(0x75, adc_zx, 2, 4);
        OPOCDE_RUN_CASE(0x76, ror_zx, 2, 6);
        OPOCDE_RUN_CASE(0x77, rra_zx, 2, 6);
        OPOCDE_RUN_CASE(0x78, sei,    1, 2);
        OPOCDE_RUN_CASE(0x79, adc_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x7A, nop0,   1, 2);
        OPOCDE_RUN_CASE(0x7B, rra_ay, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x7C, nop6,   3, 4, PGC);
        OPOCDE_RUN_CASE(0x7D, adc_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x7E, ror_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x7F, rra_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0x80, nop5,   2, 2);
        OPOCDE_RUN_CASE(0x81, sta_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x83, sax_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x84, sty_z,  2, 3);
        OPOCDE_RUN_CASE(0x85, sta_z,  2, 3);
        OPOCDE_RUN_CASE(0x86, stx_z,  2, 3);
        OPOCDE_RUN_CASE(0x87, sax_z,  2, 3);
        OPOCDE_RUN_CASE(0x88, dey,    1, 2);
        OPOCDE_RUN_CASE(0x8A, txa,    1, 2);
        OPOCDE_RUN_CASE(0x8C, sty_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x8D, sta_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x8E, stx_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x8F, sax_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0x90, bcc,    2, 2, PGC);
        OPOCDE_RUN_CASE(0x94, sty_zx, 2, 4);
        OPOCDE_RUN_CASE(0x91, sta_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0x95, sta_zx, 2, 4);
        OPOCDE_RUN_CASE(0x96, stx_zy, 2, 4);
        OPOCDE_RUN_CASE(0x97, sax_zy, 2, 4);
        OPOCDE_RUN_CASE(0x98, tya,    1, 2);
        OPOCDE_RUN_CASE(0x99, sta_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0x9A, txs,    1, 2);
        OPOCDE_RUN_CASE(0x9D, sta_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xA0, ldy_c,  2, 2);
        OPOCDE_RUN_CASE(0xA1, lda_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xA2, ldx_c,  2, 2);
        OPOCDE_RUN_CASE(0xA3, lax_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xA4, ldy_z,  2, 3);
        OPOCDE_RUN_CASE(0xA5, lda_z,  2, 3);
        OPOCDE_RUN_CASE(0xA6, ldx_z,  2, 3);
        OPOCDE_RUN_CASE(0xA7, lax_z,  2, 3);
        OPOCDE_RUN_CASE(0xA8, tay,    1, 2);
        OPOCDE_RUN_CASE(0xA9, lda_c,  2, 2);
        OPOCDE_RUN_CASE(0xAA, tax,    1, 2);
        OPOCDE_RUN_CASE(0xAC, ldy_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xAD, lda_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xAE, ldx_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xAF, lax_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xC0, cpy_c,  2, 2);
        OPOCDE_RUN_CASE(0xC1, cmp_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xC3, dcp_ix, 2, 8, IORW);
        OPOCDE_RUN_CASE(0xC4, cpy_z,  2, 3);
        OPOCDE_RUN_CASE(0xC5, cmp_z,  2, 3);
        OPOCDE_RUN_CASE(0xC6, dec_z,  2, 5);
        OPOCDE_RUN_CASE(0xC7, dcp_z,  2, 5);
        OPOCDE_RUN_CASE(0xC8, iny,    1, 2);
        OPOCDE_RUN_CASE(0xC9, cmp_c,  2, 2);
        OPOCDE_RUN_CASE(0xCD, cmp_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xCA, dex,    1, 2);
        OPOCDE_RUN_CASE(0xCC, cpy_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xCE, dec_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0xCF, dcp_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0xB0, bcs,    2, 2, PGC);
        OPOCDE_RUN_CASE(0xB1, lda_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xB3, lax_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xB4, ldy_zx, 2, 4);
        OPOCDE_RUN_CASE(0xB5, lda_zx, 2, 4);
        OPOCDE_RUN_CASE(0xB6, ldx_zy, 2, 4);
        OPOCDE_RUN_CASE(0xB7, lax_zy, 2, 4);
        OPOCDE_RUN_CASE(0xB8, clv,    1, 2);
        OPOCDE_RUN_CASE(0xB9, lda_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xBA, tsx,    1, 2);
        OPOCDE_RUN_CASE(0xBC, ldy_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xBD, lda_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xBE, ldx_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xBF, lax_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xD0, bne,    2, 2, PGC);
        OPOCDE_RUN_CASE(0xD1, cmp_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xD3, dcp_iy, 2, 8, IORW);
        OPOCDE_RUN_CASE(0xD4, nop4,   2, 4);
        OPOCDE_RUN_CASE(0xD5, cmp_zx, 2, 4);
        OPOCDE_RUN_CASE(0xD6, dec_zx, 2, 6);
        OPOCDE_RUN_CASE(0xD7, dcp_zx, 2, 6);
        OPOCDE_RUN_CASE(0xD8, cld,    1, 2);
        OPOCDE_RUN_CASE(0xD9, cmp_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xDA, nop0,   1, 2);
        OPOCDE_RUN_CASE(0xDB, dcp_ay, 3, 7, IORW);
        OPOCDE_RUN_CASE(0xDC, nop6,   3, 4, PGC);
        OPOCDE_RUN_CASE(0xDD, cmp_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xDE, dec_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0xDF, dcp_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0xE0, cpx_c,  2, 2);
        OPOCDE_RUN_CASE(0xE1, sbc_ix, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xE3, isb_ix, 2, 8, IORW);
        OPOCDE_RUN_CASE(0xE4, cpx_z,  2, 3);
        OPOCDE_RUN_CASE(0xE5, sbc_z,  2, 3);
        OPOCDE_RUN_CASE(0xE6, inc_z,  2, 5);
        OPOCDE_RUN_CASE(0xE7, isb_z,  2, 5);
        OPOCDE_RUN_CASE(0xE8, inx,    1, 2);
        OPOCDE_RUN_CASE(0xE9, sbc_c,  2, 2);
        OPOCDE_RUN_CASE(0xEA, nop,    1, 2);
        OPOCDE_RUN_CASE(0xEB, sbc_c,  2, 2);
        OPOCDE_RUN_CASE(0xEC, cpx_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xEE, inc_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0xED, sbc_a,  3, 4, IORW);
        OPOCDE_RUN_CASE(0xEF, isb_a,  3, 6, IORW);
        OPOCDE_RUN_CASE(0xF0, beq,    2, 2, PGC);
        OPOCDE_RUN_CASE(0xF1, sbc_iy, 2, 6, IORW);
        OPOCDE_RUN_CASE(0xF3, isb_iy, 2, 8, IORW);
        OPOCDE_RUN_CASE(0xF4, nop4,   2, 4);
        OPOCDE_RUN_CASE(0xF5, sbc_zx, 2, 4);
        OPOCDE_RUN_CASE(0xF6, inc_zx, 2, 6);
        OPOCDE_RUN_CASE(0xF7, isb_zx, 2, 6);
        OPOCDE_RUN_CASE(0xF8, sed,    1, 2);
        OPOCDE_RUN_CASE(0xF9, sbc_ay, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xFA, nop0,   1, 2);
        OPOCDE_RUN_CASE(0xFB, isb_ay, 3, 7, IORW);
        OPOCDE_RUN_CASE(0xFC, nop6,   3, 4, PGC);
        OPOCDE_RUN_CASE(0xFD, sbc_ax, 3, 5, IORW);
        OPOCDE_RUN_CASE(0xFE, inc_ax, 3, 7, IORW);
        OPOCDE_RUN_CASE(0xFF, isb_ax, 3, 7, IORW);
        default:
            CpuDebugDumpState(cpu);
            PpuDebugDumpState(mmap);
            OpcodeCntPrint();
            LogPrintErr("opcode: 0x%x exceute invalid\n", currOpcode);
            return -1;
    }
#undef OPOCDE_RUN_CASE

    return cycles;
}
