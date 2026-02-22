/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#ifndef __CNES_AUDIO_INTERFACE_
#define __CNES_AUDIO_INTERFACE_

typedef struct _AudioBackend AudioBackend;

#define AUDIO_SAMPLE_RATE 44100

AudioBackend* AudioInit(void);

void AudioFree(AudioBackend* audio);

void AudioPushSample(AudioBackend* audio, int16_t sample);

#endif /* __CNES_AUDIO_INTERFACE_ */
