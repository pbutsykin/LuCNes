/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#ifndef __CNES_AUDIO_RATE_LIMITER_H_
#define __CNES_AUDIO_RATE_LIMITER_H_

/*
 * Audio Rate Limiter.
 * Provides backend-agnostic timing synchronization based on audio sample rate.
 * This effectively limits the emulator to real-time speed regardless of which
 * audio backend is used.
 */
#ifdef AUDIO_RL
void AudioRateLimiterTick(uint16_t samples);
#else
static inline void AudioRateLimiterTick(uint16_t) { }
#endif

#endif /* __CNES_AUDIO_RATE_LIMITER_H_ */
