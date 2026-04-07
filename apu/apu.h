/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 Pavel Butsykin
 */
#ifndef __CNES_APU_
#define __CNES_APU_

typedef struct _CNesConnector CNesConnector;
typedef struct _AudioBackend AudioBackend;

typedef struct _APUChannelPulse {
    union {
        uint8_t r0;
        struct {
            uint8_t volume:4;
            uint8_t cvFlag:1; /* Constant volume/envelope flag */
            uint8_t lcFlag:1; /* Length counter halt */
            uint8_t duty:2;
        };
    };

    union {
        uint8_t r1;
        struct {
            uint8_t shift:3;
            uint8_t negate_flag:1;
            uint8_t period:3;
            uint8_t enabled:1;
        } sweep;
    };

    union {
        uint8_t r2;
        uint8_t timerLow;
    };

    union {
        uint8_t r3;
        struct {
            uint8_t timerHigh:3;
            uint8_t lenghCount:5;
        };
    };
} __attribute__((packed)) APUChannelPulse;

typedef struct _APUChannelTriangle {
    union {
        uint8_t r0;
        struct {
            uint8_t linearLoad:7;
            uint8_t ctrlFlag:1; /* Control flag / length counter halt */
        };
    };

    uint8_t r1; /* Unused */

    union {
        uint8_t r2;
        uint8_t timerLow;
    };

    union {
        uint8_t r3;
        struct {
            uint8_t timerHigh:3;
            uint8_t lenghCount:5;
        };
    };
} __attribute__((packed)) APUChannelTriangle;

typedef struct _APUChannelNoise {
    union {
        uint8_t r0;
        struct {
            uint8_t volume:4;
            uint8_t cvFlag:1;
            uint8_t lcFlag:1; /* Length counter halt */
            uint8_t unused0:2;
        };
    };

    uint8_t r1; /* Unused */

    union {
        uint8_t r2;
        struct {
            uint8_t period:4;
            uint8_t unused1:3;
            uint8_t mode:1;
        };
    };

    union {
        uint8_t r3;
        struct {
            uint8_t unused2:3;
            uint8_t lenghCount:5;
        };
    };
} __attribute__((packed)) APUChannelNoise;

typedef struct _APUChannelDMC {
    union {
        uint8_t r0;
        struct {
            uint8_t rate:4;
            uint8_t unused0:2;
            uint8_t loop:1;
            uint8_t irqEnabled:1;
        };
    };

    union {
        uint8_t r1;
        struct {
            uint8_t directLoad:7;
            uint8_t unused1:1;
        };
    };

    uint8_t sampleAddr;
    uint8_t sampleLen;
} __attribute__((packed)) APUChannelDMC;

typedef union _APUStatusReg {
    uint8_t v;
    struct {
        uint8_t pulse1:1;
        uint8_t pulse2:1;
        uint8_t triangle:1;
        uint8_t noise:1;
        uint8_t dmc:1;
        uint8_t unused:1;
        uint8_t frameIrq:1;
        uint8_t dmcIrq:1;
    };
} __attribute__((packed)) APUStatusReg;

typedef union _APUFrameCounterReg {
    uint8_t v;
    struct {
        uint8_t unused:6;
        uint8_t irqDisable:1;
        uint8_t mode:1; /* 0 = 4-step, 1 = 5-step */
    };
} __attribute__((packed)) APUFrameCounterReg;

typedef struct _APUReg {
    APUChannelPulse pulse1;
    APUChannelPulse pulse2;
    APUChannelTriangle triangle;
    APUChannelNoise noise;
    APUChannelDMC dmc;
    uint8_t unused1;
    APUStatusReg status;
    uint8_t unused2;
    uint8_t _joy2;  /* Frame counter control Or Joypad2 register. The same address $4017 is used on
                     * cpu bus, but on 2A03/2A07 the read and write paths are routed to different
                     * functional blocks.
                     */
    APUFrameCounterReg frameCounter; /* $4018 (APU, I/O) is unsed. Let's reuse this memory to avoid
                                      * to avoid a data conflict with Joypad2 register.
                                      */
} __attribute__((packed)) APUReg;

typedef struct _APUTimer {
    uint16_t period;
    uint16_t countdown;
} APUTimer;

typedef struct _APUEnvelope {
    bool startFlag;
    uint8_t divider;
    uint8_t decay;
} APUEnvelope;

typedef struct _APUSweep {
    bool reload;
    bool mute;
    uint8_t divider;
    uint16_t targetPeriod;
} APUSweep;

typedef struct _APUStatePulse {
    APUTimer timer;
    APUEnvelope envelope;
    APUSweep sweep;
    uint8_t dutyIdx; /* sequencer */
    uint8_t lengthCounter;
} APUStatePulse;

typedef struct _APUStateTriangle {
    APUTimer timer;
    uint8_t sequenceIdx;
    uint8_t lengthCounter;
    uint8_t linearCounter;
    bool linearReload;
} APUStateTriangle;

typedef struct _APUStateNoise {
    APUTimer timer;
    APUEnvelope envelope;
    uint16_t shiftReg;
    uint8_t lengthCounter;
} APUStateNoise;

typedef struct _APUStateDMC {
    APUTimer timer;
    uint16_t currAddr;
    uint16_t bytesRemaining;
    uint8_t sampleBuf;
    uint8_t shiftReg;
    uint8_t bitsRemaining;
    uint8_t outputLevel;
    bool silence;
    bool bufferEmpty;
} APUStateDMC;

typedef struct _APUFrameCounter {
    uint16_t countdown;
    uint8_t step:3;
    uint8_t resetDelay:3;
    uint8_t pendingQuarter:1;
    uint8_t pendingHalf:1;
    bool pendingIrq;
} APUFrameCounter;

typedef struct _APUInternalState APUState;

typedef struct _CNesAPU {
    APUReg* reg;

    struct _APUInternalState {
        APUStatePulse pulse1;
        APUStatePulse pulse2;
        APUStateTriangle triangle;
        APUStateNoise noise;
        APUStateDMC dmc;
        APUFrameCounter frame;
    } state;

    uint64_t cycles2x;

    uint32_t sampleAccum;
    uint32_t sampleCount;
    uint32_t sampleCycleAccum;  /* Accumulated cycles for sample timing */
    uint16_t pulseMix;          /* Pulse mix (changes on half-cycles) */
    uint8_t  tndIndexBase;      /* 2 noise + dmc index (changes on half-cycles) */

    AudioBackend* audio;
    CNesConnector* con;
} LuCNesAPU;

#endif /* __CNES_APU_ */
