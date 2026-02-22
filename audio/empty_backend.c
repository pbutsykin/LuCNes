/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_AUDIO
#define AUDIO_EMPTY

#include <utils/utils.h>
#include "interface.h"
#include "rate_limiter.h"

AudioBackend* AudioInit(void)
{
    return (AudioBackend*)-1;
}

void AudioFree(AudioBackend* audio __maybe_unused) { }

void AudioPushSample(AudioBackend* audio __maybe_unused, int16_t sample __maybe_unused)
{
    AudioRateLimiterTick(1);
}
