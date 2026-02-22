/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_AUDIO
#define AUDIO_RATE_LIMITER

#include <time.h>
#include <utils/utils.h>
#include "interface.h"

#define NS_PER_SEC    1000000000ULL
#define NS_PER_MS     1000000ULL
#define MAX_DRIFT_NS  (65 * NS_PER_MS)
#define REBUFFER_NS   (40 * NS_PER_MS)

static uint64_t GetTimeNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

static void SleepNs(uint64_t ns)
{
    struct timespec ts = {
        .tv_sec = ns / NS_PER_SEC,
        .tv_nsec = ns % NS_PER_SEC
    };
    nanosleep(&ts, NULL);
}

void AudioRateLimiterTick(uint16_t samples)
{
    static uint64_t startTimeNs, samplesPushed;
    uint64_t expectedNs, elapsedNs;

    if (unlikely(!startTimeNs))
        startTimeNs = GetTimeNs();

    samplesPushed += samples;

    expectedNs = (samplesPushed * NS_PER_SEC) / AUDIO_SAMPLE_RATE;
    elapsedNs = GetTimeNs() - startTimeNs;

    if (expectedNs > elapsedNs) {
        SleepNs(expectedNs - elapsedNs);
    } else if ((elapsedNs - expectedNs) > MAX_DRIFT_NS) {
        LogPrintWrn("Stall detected, drift=%lums. Resetting baseline.\n",
                        (unsigned long)((elapsedNs - expectedNs) / NS_PER_MS));
        startTimeNs = GetTimeNs() - REBUFFER_NS;
        samplesPushed = 0;
    }
}
