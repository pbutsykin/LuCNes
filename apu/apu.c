/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 Pavel Butsykin
 */
#define CNES_APU
#define APU

#include <utils/utils.h>
#include <cpu/interface.h>
#include <audio/interface.h>

#include "apu.h"

/* NES CPU clock rate (NTSC) */
#define CPU_CLOCK_RATE 1789773

/* 0x8000 + 0x4000 = 0xC000 */
#define DMC_BASE_ADDR 0x4000
#define DMC_ADDR_MASK 0x7FFF

enum {
    APU_REG_PULSE1_R0 = 0x00,
    APU_REG_PULSE1_R1 = 0x01,
    APU_REG_PULSE1_R2 = 0x02,
    APU_REG_PULSE1_R3 = 0x03,
    APU_REG_PULSE2_R0 = 0x04,
    APU_REG_PULSE2_R1 = 0x05,
    APU_REG_PULSE2_R2 = 0x06,
    APU_REG_PULSE2_R3 = 0x07,
    APU_REG_TRI_R0    = 0x08,
    APU_REG_TRI_R1    = 0x09, /* Unused */
    APU_REG_TRI_R2    = 0x0A,
    APU_REG_TRI_R3    = 0x0B,
    APU_REG_NOISE_R0  = 0x0C,
    APU_REG_NOISE_R1  = 0x0D, /* Unused */
    APU_REG_NOISE_R2  = 0x0E,
    APU_REG_NOISE_R3  = 0x0F,
    APU_REG_DMC_R0    = 0x10,
    APU_REG_DMC_R1    = 0x11,
    APU_REG_DMC_R2    = 0x12,
    APU_REG_DMC_R3    = 0x13,

    APU_REG_STATUS    = 0x15,
    APU_REG_FRAME_CNT = 0x18, /* ̶0̶x̶1̶7̶ */

    APU_REG_MAX = 0x19,
} APU_REG_OFFS;

#if APU_REG_TRACE
#define RegTraceFmt(_rw, _reg, _cycles2x, _fmt, ...) \
    do {                                             \
        printf("%c: %-13s: "_fmt"\t(cyc:%lu)\n", (_rw), (_reg), \
               ##__VA_ARGS__, (_cycles2x >> 1));     \
        fflush(stdout); \
    } while(false)
#else
    #define RegTraceFmt(...) do {} while(0)
#endif

#define RegTraceW(_reg, _cycles2x, _val, _rval) \
    RegTraceFmt('w', _reg, _cycles2x, "%02x => %02x",  _val, _rval)

#define RegTraceR(_reg, _cycles2x, _rval, ...) \
    RegTraceFmt('r', _reg, _cycles2x, "%02x", _rval)

static inline uint16_t ApuPulseTimerRead(APUChannelPulse* p)
{
    return (p->timerHigh << BYTE_BITS) | p->timerLow;
}

static inline uint16_t ApuTriangleTimerRead(APUChannelTriangle* t)
{
    return (t->timerHigh << BYTE_BITS) | t->timerLow;
}

static void ApuSweepCalcTarget(APUStatePulse* pulse, APUChannelPulse* reg, bool isPulse1)
{
#define SWEEP_OVERFLOW 0x7FF
#define MIN_TIMER_PERIOD 8

    int16_t target, change = pulse->timer.period >> reg->sweep.shift;

    LogPrintAssert(pulse->timer.period <= SWEEP_OVERFLOW,
                   "Timer period is 11-bit value. Period overflow: %x)\n", pulse->timer.period);

    if (reg->sweep.negate_flag) {
        change = -change;
        if (isPulse1)
            change--; /* Pulse1 uses one's complement */
    }
    target = (int16_t)pulse->timer.period + change;

    pulse->sweep.targetPeriod = target < 0 ? 0 : (uint16_t)target;
    pulse->sweep.mute = (pulse->timer.period < MIN_TIMER_PERIOD ||
                         pulse->sweep.targetPeriod > SWEEP_OVERFLOW);
}

static uint8_t ApuPulseOutput(APUStatePulse* pulse, APUChannelPulse* reg)
{
    /* Pulse duty sequences */
    static const uint8_t dutyTable[4][8] = {
        {0, 0, 0, 0, 0, 0, 0, 1}, /* 12.5% */
        {0, 0, 0, 0, 0, 0, 1, 1}, /* 25% */
        {0, 0, 0, 0, 1, 1, 1, 1}, /* 50% */
        {1, 1, 1, 1, 1, 1, 0, 0}  /* 75% (inverted 25%) */
    };

    if (pulse->sweep.mute)
        return 0;

    if (!pulse->lengthCounter)
        return 0;

    if (!dutyTable[reg->duty][pulse->dutyIdx])
        return 0;

    /* Output envelope or constant volume */
    return reg->cvFlag ? reg->volume : pulse->envelope.decay;
}


/* Non-linear mixing lookup tables */
static uint16_t pulseTable[31];
static uint16_t tndTable[203];

#define TND_MIX_IDX_TRIANGLE 3
#define TND_MIX_IDX_NOISE    2

/* The accurate model uses different per-channel conductance terms (Triangle/8227,
 * Noise/12241, DMC/22638 in the common approximation). For the lookup-table variant,
 * those fractional weights are approximated by small integers to form a compact index:
 *   tndIndex = 3*triangle + 2*noise + dmc
 */

static inline uint16_t ApuGetMixedSample(const LuCNesAPU* apu)
{
    const APUState* state = &apu->state;
    const uint16_t pulseMix = pulseTable[state->pulse1.output + state->pulse2.output];

    return tndTable[apu->tndIndexBase + state->triangle.output] + pulseMix;
}

static uint8_t ApuTriangleOutput(APUStateTriangle* tri)
{
#define TRI_ULTRASONIC_PERIOD_THRESHOLD 2
#define TRI_OUTPUT_MIDPOINT_LEVEL 7

    /* TODO: Add high pass and low pass filter instead of this hack. */
    if (tri->timer.period < TRI_ULTRASONIC_PERIOD_THRESHOLD)
        return TRI_OUTPUT_MIDPOINT_LEVEL * TND_MIX_IDX_TRIANGLE; /* Output mid-point to avoid pops */

    /* 32-step triangle sequence function */
    uint8_t output = (tri->sequenceIdx & 0x10 ? tri->sequenceIdx : ~tri->sequenceIdx) & 0xF;
    return output * TND_MIX_IDX_TRIANGLE;
}

static uint8_t ApuNoiseOutput(APUStateNoise* noise, APUChannelNoise* reg)
{
    if (!noise->lengthCounter)
        return 0;

    /* If Bit 0 of the shift register is set, output is 0 */
    if (noise->shiftReg & 1)
        return 0;

    return (reg->cvFlag ? reg->volume : noise->envelope.decay) * TND_MIX_IDX_NOISE;
}

static inline void ApuUpdateOutput(LuCNesAPU* apu)
{
    APUReg* reg = apu->reg;

    apu->state.pulse1.output = ApuPulseOutput(&apu->state.pulse1, &reg->pulse1);
    apu->state.pulse2.output = ApuPulseOutput(&apu->state.pulse2, &reg->pulse2);
    apu->state.triangle.output = ApuTriangleOutput(&apu->state.triangle);
    apu->state.noise.output = ApuNoiseOutput(&apu->state.noise, &reg->noise);
    apu->tndIndexBase = apu->state.noise.output + apu->state.dmc.outputLevel;
    apu->outputMix = ApuGetMixedSample(apu);
}

/* Noise period lookup table (NTSC) */
static const uint16_t noisePeriodTable[16] = {
    2, 4, 8,  16, 32, 48, 64,  80,  101, 127, 190, 254, 381, 508,  1017, 2034
};

/* DMC rate lookup table (NTSC) */
static const uint16_t dmcRateTable[16] = {
    214, 190, 170, 160, 143, 127, 113, 107, 95, 80, 71, 64, 53, 42, 36, 27
};

static inline void ApuFlushFrameSignals(LuCNesAPU* apu);

void ApuRegWrite(void* ctx, MMap* mmap, uint8_t* addr, uint8_t val)
{
    /* Length counter lookup table */
    static const uint8_t lengthTable[32] = {
        10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
        12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
    };

    LuCNesAPU* apu = ctx;
    APUReg* reg = (APUReg*)mmap->apuReg;
    APUState* state = &apu->state;
    uint16_t regOffs = addr - mmap->apuReg;

    switch (regOffs) {
        case APU_REG_PULSE1_R0:
            RegTraceW("pulse1.r0", apu->cycles2x, val, reg->pulse1.r0);
            ApuFlushFrameSignals(apu);
            reg->pulse1.r0 = val;
            state->pulse1.envelope.startFlag = true;
            break;
        case APU_REG_PULSE1_R1:
            RegTraceW("pulse1.sweep", apu->cycles2x, val, reg->pulse1.r1);
            reg->pulse1.r1 = val;
            state->pulse1.sweep.reload = true;
            break;
        case APU_REG_PULSE1_R2:
            RegTraceW("pulse1.r2", apu->cycles2x, val, reg->pulse1.r2);
            reg->pulse1.r2 = val;
            state->pulse1.timer.period = ApuPulseTimerRead(&reg->pulse1);
            ApuSweepCalcTarget(&state->pulse1, &reg->pulse1, true);
            break;
        case APU_REG_PULSE1_R3:
            RegTraceW("pulse1.r3", apu->cycles2x, val, reg->pulse1.r3);
            reg->pulse1.r3 = val;
            state->pulse1.timer.period = ApuPulseTimerRead(&reg->pulse1);
            state->pulse1.dutyIdx = 0;
            if (reg->status.pulse1) {
                const uint8_t prev = state->pulse1.lengthCounter;

                ApuFlushFrameSignals(apu);
                if (prev == state->pulse1.lengthCounter)
                    state->pulse1.lengthCounter = lengthTable[reg->pulse1.lenghCount];
            }
            state->pulse1.envelope.startFlag = true;
            ApuSweepCalcTarget(&state->pulse1, &reg->pulse1, true);
            LogPrintDbg("pulse1: period=%x, length=%x\n", state->pulse1.timer.period,
                        state->pulse1.lengthCounter);
            break;

        case APU_REG_PULSE2_R0:
            RegTraceW("pulse2.r0", apu->cycles2x, val, reg->pulse2.r0);
            ApuFlushFrameSignals(apu);
            reg->pulse2.r0 = val;
            state->pulse2.envelope.startFlag = true;
            break;
        case APU_REG_PULSE2_R1:
            RegTraceW("pulse2.sweep", apu->cycles2x, val, reg->pulse2.r1);
            reg->pulse2.r1 = val;
            state->pulse2.sweep.reload = true;
            break;
        case APU_REG_PULSE2_R2:
            RegTraceW("pulse2.r2", apu->cycles2x, val, reg->pulse2.r2);
            reg->pulse2.r2 = val;
            state->pulse2.timer.period = ApuPulseTimerRead(&reg->pulse2);
            ApuSweepCalcTarget(&state->pulse2, &reg->pulse2, false);
            break;
        case APU_REG_PULSE2_R3:
            RegTraceW("pulse2.r3", apu->cycles2x, val, reg->pulse2.r3);
            reg->pulse2.r3 = val;
            state->pulse2.timer.period = ApuPulseTimerRead(&reg->pulse2);
            state->pulse2.dutyIdx = 0;
            if (reg->status.pulse2) {
                const uint8_t prev = state->pulse2.lengthCounter;

                ApuFlushFrameSignals(apu);
                if (prev == state->pulse2.lengthCounter)
                    state->pulse2.lengthCounter = lengthTable[reg->pulse2.lenghCount];
            }
            state->pulse2.envelope.startFlag = true;
            ApuSweepCalcTarget(&state->pulse2, &reg->pulse2, false);
            LogPrintDbg("pulse2: period=%x, length=%x\n", state->pulse2.timer.period,
                        state->pulse2.lengthCounter);
            break;

        case APU_REG_TRI_R0:
            RegTraceW("tri.r0", apu->cycles2x, val, reg->triangle.r0);
            ApuFlushFrameSignals(apu);
            reg->triangle.r0 = val;
            break;
        case APU_REG_TRI_R1:
            RegTraceW("tri.unused", apu->cycles2x, val, reg->triangle.r1);
            /* Unused */
            break;
        case APU_REG_TRI_R2:
            RegTraceW("tri.r2", apu->cycles2x, val, reg->triangle.r2);
            reg->triangle.r2 = val;
            state->triangle.timer.period = ApuTriangleTimerRead(&reg->triangle);
            break;
        case APU_REG_TRI_R3:
            RegTraceW("tri.r3", apu->cycles2x, val, reg->triangle.r3);
            reg->triangle.r3 = val;
            state->triangle.timer.period = ApuTriangleTimerRead(&reg->triangle);
            if (reg->status.triangle) {
                const uint8_t prev = state->triangle.lengthCounter;

                ApuFlushFrameSignals(apu);
                if (prev == state->triangle.lengthCounter)
                    state->triangle.lengthCounter = lengthTable[reg->triangle.lenghCount];
            }
            state->triangle.linearReload = true;
            LogPrintDbg("triangle: period=%x, length=%x\n", state->triangle.timer.period,
                        state->triangle.lengthCounter);
            break;

        case APU_REG_NOISE_R0:
            RegTraceW("noise.r0", apu->cycles2x, val, reg->noise.r0);
            ApuFlushFrameSignals(apu);
            reg->noise.r0 = val;
            state->noise.envelope.startFlag = true;
            break;
        case APU_REG_NOISE_R1:
            RegTraceW("noise.unused", apu->cycles2x, val, reg->noise.r1);
            /* Unused */
            break;
        case APU_REG_NOISE_R2:
            RegTraceW("noise.period", apu->cycles2x, val, reg->noise.r2);
            reg->noise.r2 = val;
            state->noise.timer.period = noisePeriodTable[reg->noise.period];
            break;
        case APU_REG_NOISE_R3:
            RegTraceW("noise.length", apu->cycles2x, val, reg->noise.r3);
            reg->noise.r3 = val;
            if (reg->status.noise) {
                const uint8_t prev = state->noise.lengthCounter;

                ApuFlushFrameSignals(apu);
                if (prev == state->noise.lengthCounter)
                    state->noise.lengthCounter = lengthTable[reg->noise.lenghCount];
            }
            state->noise.envelope.startFlag = true;
            LogPrintDbg("noise: period=%x, length=%x\n", state->noise.timer.period,
                        state->noise.lengthCounter);
            break;

        case APU_REG_DMC_R0:
            RegTraceW("dmc.r0", apu->cycles2x, val, reg->dmc.r0);
            reg->dmc.r0 = val;
            state->dmc.timer.period = dmcRateTable[reg->dmc.rate];
            if (!reg->dmc.irqEnabled)
                reg->status.dmcIrq = 0; /* Clear DMC IRQ flag */
            break;
        case APU_REG_DMC_R1:
            RegTraceW("dmc.load", apu->cycles2x, val, reg->dmc.r1);
            reg->dmc.r1 = val;
            state->dmc.outputLevel = reg->dmc.directLoad;
            break;
        case APU_REG_DMC_R2:
            RegTraceW("dmc.addr", apu->cycles2x, val, reg->dmc.sampleAddr);
            reg->dmc.sampleAddr = val;
            break;
        case APU_REG_DMC_R3:
            RegTraceW("dmc.length", apu->cycles2x, val, reg->dmc.sampleLen);
            reg->dmc.sampleLen = val;
            break;

        case APU_REG_STATUS: {
            RegTraceW("status", apu->cycles2x, val, reg->status.v);
            APUStatusReg status = (APUStatusReg)val;

            status.frameIrq = reg->status.frameIrq;
            // TODO: Open bus behavior
            reg->status = status;

            if (!status.pulse1)
                state->pulse1.lengthCounter = 0;
            if (!status.pulse2)
                state->pulse2.lengthCounter = 0;
            if (!status.triangle)
                state->triangle.lengthCounter = 0;
            if (!status.noise)
                state->noise.lengthCounter = 0;

            if (!status.dmc)
                state->dmc.bytesRemaining = 0;
            else if (!state->dmc.bytesRemaining) {
                /* Restart DMC sample */
                state->dmc.currAddr = (DMC_BASE_ADDR | ((uint16_t)reg->dmc.sampleAddr << 6)) &
                                      DMC_ADDR_MASK;
                state->dmc.bytesRemaining = ((uint16_t)reg->dmc.sampleLen << 4) + 1;
            }
            /* Clear DMC IRQ flag */
            reg->status.dmcIrq = 0;
            break;
        }
        case APU_REG_FRAME_CNT:
            RegTraceW("frame", apu->cycles2x, val, reg->frameCounter.v);
            reg->frameCounter.v = val;

            if (reg->frameCounter.irqDisable)
                reg->status.frameIrq = false;

            /* Frame counter reset is delayed by 3-4 CPU cycles:
             * - 3 cycles if write occurs during an APU cycle (odd cycles2x)
             * - 4 cycles if write occurs between APU cycles (even cycles2x)
             * https://www.nesdev.org/wiki/APU_Frame_Counter
             */
            state->frame.resetDelay = (apu->cycles2x & 1) ? 3 : 4;
            break;
        default:
            LogPrintAssert(0, "Invalid APU register: %x\n", regOffs);
            break;
    }
    ApuUpdateOutput(apu);
}

bool ApuCheckIRQ(LuCNesAPU* apu)
{
    return apu->irq;
}

void* ApuRegRead(void* ctx, MMap* mmap, uint8_t* addr)
{
    /* Status register ($4015) is readable */
    if (likely(addr == mmap->soundChan)) {
        static uint8_t statusResult;
        LuCNesAPU* apu = ctx;
        APUReg* reg = (APUReg*)mmap->apuReg;
        APUState* state = &apu->state;
        APUStatusReg status = reg->status;

        status.pulse1 = !!state->pulse1.lengthCounter;
        status.pulse2 = !!state->pulse2.lengthCounter;
        status.triangle = !!state->triangle.lengthCounter;
        status.noise = !!state->noise.lengthCounter;
        status.dmc = !!state->dmc.bytesRemaining;
        // TODO: Open bus behavior
        //status.unused = ((APUStatusReg)cpuOpenBus).unused;

        /* Reading clears frame IRQ flag */
        if (reg->status.frameIrq)
            reg->status.frameIrq = false;

        //TODO: If an interrupt flag was set at the same moment of the read,
        //      it will read back as 1 but it will not be cleared.
        statusResult = status.v;
        RegTraceR("status", apu->cycles2x, statusResult);
        return &statusResult;
    }

    /* https://www.nesdev.org/wiki/Open_bus_behavior */
    /* TODO: Other registers are write-only, return address (open bus behavior) */
    return addr;
}

static void ApuPulseTick(APUStatePulse* pulse, APUChannelPulse* reg)
{
    /* https://www.nesdev.org/wiki/APU_Pulse
     *
     * The reason for the odd output from the sequencer is that the counter is initialized to zero
     * but counts downward rather than upward. Thus it reads the sequence lookup table in the order
     * 0, 7, 6, 5, 4, 3, 2, 1.
     */

    if (likely(pulse->timer.countdown))
        pulse->timer.countdown--;
    else {
        pulse->timer.countdown = pulse->timer.period;
        pulse->dutyIdx = (pulse->dutyIdx - 1) & 7;
        pulse->output = ApuPulseOutput(pulse, reg);
    }
}

static void ApuTriangleTick(APUStateTriangle* tri)
{
    if (likely(tri->timer.countdown))
        tri->timer.countdown--;
    else {
        tri->timer.countdown = tri->timer.period;
        /* Only step if length counter and linear counter are non-zero */
        if (tri->lengthCounter && tri->linearCounter) {
            LuCNesAPU* apu = CONTAINER_OF(tri, LuCNesAPU, state.triangle);
            tri->sequenceIdx = (tri->sequenceIdx + 1) & 31;
            tri->output = ApuTriangleOutput(tri);
            apu->outputMix = ApuGetMixedSample(apu);
        }
    }
}

static void ApuNoiseTick(APUStateNoise* noise, APUChannelNoise* reg)
{
#define NOISE_LFSR_TAP_MODE0 1
#define NOISE_LFSR_TAP_MODE1 6
#define NOISE_LFSR_LEFTMOST_BIT 14

    /* The period determines how many APU cycles happen between shift register clocks. */
    if (unlikely(!--noise->timer.countdown)) {
        /* Calculate feedback bit. https://www.nesdev.org/wiki/APU_Noise */
        uint8_t bit = reg->mode ? NOISE_LFSR_TAP_MODE1 : NOISE_LFSR_TAP_MODE0;
        uint8_t feedback = (noise->shiftReg ^ (noise->shiftReg >> bit)) & 1;

        noise->timer.countdown = noise->timer.period;
        noise->shiftReg >>= 1;
        noise->shiftReg |= feedback << NOISE_LFSR_LEFTMOST_BIT;
        if (noise->lengthCounter) {
            LuCNesAPU* apu = CONTAINER_OF(noise, LuCNesAPU, state.noise);
            noise->output = ApuNoiseOutput(noise, reg);
            apu->tndIndexBase = noise->output + apu->state.dmc.outputLevel;
        }
    }
}

static void DmcOutputTick(APUStateDMC* dmc)
{
#define DMC_OUTPUT_MAX 127
#define DMC_OUTPUT_MIN 0

    /* Output unit:
     * If silence flag is clear, adjust output level based on bit 0 of shift register */
    if (!dmc->silence) {
        if (dmc->shiftReg & 1) {
            if (dmc->outputLevel < DMC_OUTPUT_MAX - 1) {
                LuCNesAPU* apu = CONTAINER_OF(dmc, LuCNesAPU, state.dmc);
                dmc->outputLevel += 2;
                apu->tndIndexBase = apu->state.noise.output + dmc->outputLevel;
            }
        } else {
            if (dmc->outputLevel > DMC_OUTPUT_MIN + 1) {
                LuCNesAPU* apu = CONTAINER_OF(dmc, LuCNesAPU, state.dmc);
                dmc->outputLevel -= 2;
                apu->tndIndexBase = apu->state.noise.output + dmc->outputLevel;
            }
        }
    }
    dmc->shiftReg >>= 1;
}

static void DmcMemoryReaderTick(LuCNesAPU* apu, APUStateDMC* dmc)
{
    if (likely(!dmc->bufferEmpty || !dmc->bytesRemaining))
        return;

    /* TODO: DMC DMA steals 1-4 CPU cycles per sample byte read.
     * This affects CPU/PPU timing but is omitted for simplicity.
     * See: https://www.nesdev.org/wiki/APU_DMC#Memory_reader
     */
    dmc->sampleBuf = *MMapPrgResolve(CpuMMap(apu->con->cpu), dmc->currAddr);
    dmc->bufferEmpty = false;

    /* Address wraps at $FFFF to $8000. */
    dmc->currAddr = (dmc->currAddr + 1) & DMC_ADDR_MASK;

    /* When bytes remaining becomes 0: */
    if (unlikely(!--dmc->bytesRemaining)) {
        APUChannelDMC* reg = &apu->reg->dmc;

        if (reg->loop) {
            /* If loop flag is set, restart sample */
            dmc->currAddr = (DMC_BASE_ADDR | ((uint16_t)reg->sampleAddr << 6)) & DMC_ADDR_MASK;
            dmc->bytesRemaining = ((uint16_t)reg->sampleLen << 4) + 1;
        } else if (reg->irqEnabled)
            apu->reg->status.dmcIrq = 1; /* If IRQ enabled flag is set, set DMC IRQ flag */
    }
}

/*
 * https://www.nesdev.org/wiki/APU_DMC
 */
static void ApuDMCTick(LuCNesAPU* apu)
{
    APUStateDMC* dmc = &apu->state.dmc;

    if (unlikely(!--dmc->timer.countdown)) {
        dmc->timer.countdown = dmc->timer.period;

        DmcOutputTick(dmc);

        /* When bits remaining becomes 0, a new output cycle starts */
        if (unlikely(!--dmc->bitsRemaining)) {
            dmc->bitsRemaining = BYTE_BITS;

            if (dmc->bufferEmpty)
                dmc->silence = true; /* If sample buffer is empty, silence flag is set */
            else {
                dmc->silence = false;
                dmc->bufferEmpty = true;
                dmc->shiftReg = dmc->sampleBuf;
            }
        }
    }
    DmcMemoryReaderTick(apu, dmc);
}

static void ApuEnvelopeTick(APUEnvelope* env, uint8_t volume, uint8_t loopFlag)
{
#define MAX_DECAY_COUNTER 15 //(4-bit)

    if (env->startFlag) {
        env->startFlag = false;
        env->decay = MAX_DECAY_COUNTER;
        env->divider = volume;
        return;
    }

    if (env->divider) {
        env->divider--;
        return;
    }

    env->divider = volume;
    if (env->decay)
        env->decay--;
    else if (loopFlag)
        env->decay = MAX_DECAY_COUNTER;
}

static void ApuSweepTick(APUStatePulse* pulse, APUChannelPulse* reg, bool isPulse1)
{
    ApuSweepCalcTarget(pulse, reg, isPulse1);

    if (!pulse->sweep.divider && reg->sweep.enabled && reg->sweep.shift && !pulse->sweep.mute)
        pulse->timer.period = pulse->sweep.targetPeriod;

    if (!pulse->sweep.divider || pulse->sweep.reload) {
        pulse->sweep.divider = reg->sweep.period;
        pulse->sweep.reload = false;
    } else
        pulse->sweep.divider--;
}

static void ApuLengthCounterTick(uint8_t* counter, uint8_t halt)
{
    if (*counter && !halt)
        (*counter)--;
}

static void ApuLinearCounterTick(APUStateTriangle* tri, APUChannelTriangle* reg)
{
    if (tri->linearReload) {
        tri->linearCounter = reg->linearLoad;
    } else if (tri->linearCounter) {
        tri->linearCounter--;
    }

    if (!reg->ctrlFlag)
        tri->linearReload = false;
}

static void ApuQuarterFrameTick(APUReg* reg, APUState* state)
{
    ApuEnvelopeTick(&state->pulse1.envelope, reg->pulse1.volume, reg->pulse1.lcFlag);
    ApuEnvelopeTick(&state->pulse2.envelope, reg->pulse2.volume, reg->pulse2.lcFlag);
    ApuEnvelopeTick(&state->noise.envelope, reg->noise.volume, reg->noise.lcFlag);
    ApuLinearCounterTick(&state->triangle, &reg->triangle);
}

static void ApuHalfFrameTick(APUReg* reg, APUState* state)
{
    ApuLengthCounterTick(&state->pulse1.lengthCounter, reg->pulse1.lcFlag);
    ApuLengthCounterTick(&state->pulse2.lengthCounter, reg->pulse2.lcFlag);
    ApuLengthCounterTick(&state->triangle.lengthCounter, reg->triangle.ctrlFlag);
    ApuLengthCounterTick(&state->noise.lengthCounter, reg->noise.lcFlag);

    ApuSweepTick(&state->pulse1, &reg->pulse1, true);
    ApuSweepTick(&state->pulse2, &reg->pulse2, false);
}

static inline void ApuFlushFrameSignals(LuCNesAPU* apu)
{
    APUFrameCounter* frame = &apu->state.frame;

    if (unlikely(frame->pendingQuarter)) {
        frame->pendingQuarter = false;
        ApuQuarterFrameTick(apu->reg, &apu->state);
    }
    if (unlikely(frame->pendingHalf)) {
        frame->pendingHalf = false;
        ApuHalfFrameTick(apu->reg, &apu->state);
    }
}

static void ApuFrameCounterTick(LuCNesAPU* apu)
{
    static const uint16_t frameSteps[] = {3728, 7456, 11185, 14914, 18640};
    APUReg* reg = apu->reg;
    APUState* state = &apu->state;
    APUFrameCounter* frame = &state->frame;
    const uint8_t maxSteps = sizeof(frameSteps) / sizeof(frameSteps[0]) - !reg->frameCounter.mode;

    if (frame->countdown++ < frameSteps[frame->step])
        return;

    /* Schedule frame signals with 1 cpu cycle delay.
     * https://www.nesdev.org/wiki/APU_Frame_Counter
     */
    switch (frame->step) {
        case 0:
            frame->pendingQuarter = true;
            break;
        case 1:
            frame->pendingQuarter = true;
            frame->pendingHalf = true;
            break;
        case 2:
            frame->pendingQuarter = true;
            break;
        case 3: /* Mode 0: quarter + half + IRQ. Mode 1: Nothing */
            if (!reg->frameCounter.mode) {
                frame->pendingQuarter = true;
                frame->pendingHalf = true;

                /* IRQ flag is set 3 times in mode 0 (14914, 14914.5, 0(14915)).
                 * This literally means we should set IRQ flag for 3 cpu cycles in a row.
                 */
                if (!reg->frameCounter.irqDisable) {
                    reg->status.frameIrq = true;
                    frame->pendingIrq = true;
                }
            }
            break;
        case 4: /* Mode 1 only */
            frame->pendingQuarter = true;
            frame->pendingHalf = true;
            break;
    }

    if (++frame->step >= maxSteps) {
        frame->step = 0;
        frame->countdown = 0;
    }
}

/* Fixed-point scale factor (2^15) */
#define FP_SCALE 32768LL

/* The 2A03/2A07 doesn't mix channels by simple linear addition. Each channel has its
 * own DAC and the outputs are combined through an analog resistive network, so the
 * effective contribution is non-linear and channels "load" each other.
 * These constants are fitted parameters of the approximation used to emulate the analog mixer
 * curve while keeping runtime cost low (tables instead of divisions per sample).
 */
#define PULSE_MIX_NUM_X100  9552
#define PULSE_MIX_DEN_BASE  8128
#define TND_MIX_NUM_X100    16367
#define TND_MIX_DEN_BASE    24329
#define MIX_DEN_OFFSET      100

static void ApuMixerInit(void)
{
    /* https://www.nesdev.org/wiki/APU_Mixer */
    size_t i;

    /* pulse_out = 95.52 / (8128/i + 100), scaled by FP_SCALE */
    for (i = 1; i < sizeof(pulseTable) / sizeof(pulseTable[0]); i++)
        pulseTable[i] = (uint16_t)((FP_SCALE * PULSE_MIX_NUM_X100 * i) /
                                   (100 * (PULSE_MIX_DEN_BASE + MIX_DEN_OFFSET * i)));

    /* tnd_out = 163.67 / (24329/i + 100), scaled by FP_SCALE */
    for (i = 1; i < sizeof(tndTable) / sizeof(tndTable[0]); i++)
        tndTable[i] = (uint16_t)((FP_SCALE * TND_MIX_NUM_X100 * i) /
                                 (100 * (TND_MIX_DEN_BASE + MIX_DEN_OFFSET * i)));
}

static void ApuProcessPendingFrameSignals(LuCNesAPU* apu)
{
    APUReg* reg = apu->reg;
    APUFrameCounter* frame = &apu->state.frame;

    if (unlikely(frame->pendingIrq)) {
        /* Set IRQ on consecutive cpu cycles (14914.5 and 0(14915)).
         * Clear pendingIrq on the next apu tick (odd cycles2x).
         */
        if (apu->cycles2x & 1)
            frame->pendingIrq = false;
        if (!reg->frameCounter.irqDisable)
            reg->status.frameIrq = true;
    }
    if (unlikely(frame->pendingQuarter || frame->pendingHalf)) {
        ApuFlushFrameSignals(apu);
        ApuUpdateOutput(apu);
    }
    if (unlikely(frame->resetDelay && !--frame->resetDelay)) {
        frame->countdown = 0;
        frame->step = 0;
        /* If mode 1, generate quarter + half frame signals immediately. */
        if (reg->frameCounter.mode) {
            ApuQuarterFrameTick(reg, &apu->state);
            ApuHalfFrameTick(reg, &apu->state);
            ApuUpdateOutput(apu);
        }
    }
}

void ApuTicksExecute(LuCNesAPU* apu, const uint8_t cpuCycles)
{
    APUReg* reg = apu->reg;
    APUState* state = &apu->state;

    for (int i = 0; i < cpuCycles; i++) {
        apu->irq = reg->status.frameIrq || reg->status.dmcIrq;

        ApuProcessPendingFrameSignals(apu);

        /* APU runs at half CPU rate for sequencer/pulse/noise/DMC */
        if (apu->cycles2x & 1) {
            ApuFrameCounterTick(apu);
            ApuPulseTick(&state->pulse1, &reg->pulse1);
            ApuPulseTick(&state->pulse2, &reg->pulse2);
            ApuNoiseTick(&state->noise, &reg->noise);
            ApuDMCTick(apu);
            apu->outputMix = ApuGetMixedSample(apu);
        }
        /* Triangle runs at CPU rate */
        ApuTriangleTick(&state->triangle);

        /* Mix and accumulate channel outputs */
        apu->sampleAccum += apu->outputMix;
        apu->sampleCount++;

        /* Output sample at target rate using precise fractional timing
         * Add AUDIO_SAMPLE_RATE per cycle, output when >= CPU_CLOCK_RATE
         */
        apu->sampleCycleAccum += AUDIO_SAMPLE_RATE;
        if (unlikely(apu->sampleCycleAccum >= CPU_CLOCK_RATE)) {
            int32_t avgSample = apu->sampleAccum / apu->sampleCount;

            /* Convert from unsigned [0, FP_SCALE] to signed [-32768, 32767] */
            avgSample = (avgSample * 2) - FP_SCALE;

            AudioPushSample(apu->audio, (int16_t)avgSample);

            /* TODO: This approach is not very accurate, because we lose the part of sampleAccum
             *       that corresponds to the remaining apu->sampleCycleAccum. It would probably
             *       be better to carry the remaining sampleAccum over to the next sample cycles.
             */
            apu->sampleAccum = 0;
            apu->sampleCount = 0;
            apu->sampleCycleAccum -= CPU_CLOCK_RATE;
        }
        apu->cycles2x++;
    }
}

LuCNesAPU* ApuInit(LuCNesCPU* cpu, void* connector)
{
    LuCNesAPU* apu = MemAlloc(sizeof(*apu));
    *apu = (LuCNesAPU) {
        .reg = (APUReg*)CpuMMap(cpu)->apuReg,
        .sampleCycleAccum = 0,
    };

    /* APU power up state (https://www.nesdev.org/wiki/CPU_power_up_state#APU)
     *
     * $4000-$400F = $00
     * $4010-$4013 = $00
     * $4017 = $00 (frame irq enabled)
     * $4015 = $00 (all channels disabled)
     */
    memset(apu->reg, 0, sizeof(*apu->reg));

    /* On power-up, the shift register is loaded with the value 1.
     * Timer starts at rate 0 period (noisePeriodTable[0]).
     */
    apu->state.triangle.output = ApuTriangleOutput(&apu->state.triangle);

    apu->state.noise.shiftReg = 1;
    apu->state.noise.timer.period = noisePeriodTable[0];
    apu->state.noise.timer.countdown = noisePeriodTable[0];
    apu->state.noise.output = ApuNoiseOutput(&apu->state.noise, &apu->reg->noise);

    /* Skip 5 apu ticks (10 cpu cycles) already elapsed during cpu reset sequence. */
    apu->state.frame.countdown = 5;

    /* DMC power-up state: buffer empty, ready for new byte, silence
     * Timer starts at rate 0 period (dmcRateTable[0]).
     */
    apu->state.dmc.bufferEmpty = true;
    apu->state.dmc.bitsRemaining = BYTE_BITS;
    apu->state.dmc.silence = true;
    apu->state.dmc.timer.period = dmcRateTable[0];
    apu->state.dmc.timer.countdown = dmcRateTable[0];

    ApuMixerInit();

    apu->audio = AudioInit();
    if (!apu->audio) {
        LogPrintErr("Failed to initialize audio backend\n");
        MemFree(apu);
        return NULL;
    }
    apu->con = connector;

    LogPrintDbg("APU initialized: sample rate=%d, cpu clock=%d\n",
                AUDIO_SAMPLE_RATE, CPU_CLOCK_RATE);
    return apu;
}

void ApuFree(LuCNesAPU* apu)
{
    if (apu->audio)
        AudioFree(apu->audio);
    MemFree(apu);
}
