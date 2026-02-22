/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_AUDIO
#define AUDIO_SDL2

#include <utils/utils.h>
#include <SDL2/SDL.h>

#include "interface.h"
#include "rate_limiter.h"

#define SAMPLE_BATCH_SIZE 128
#define AUDIO_BUFFER_SAMPLES 512

#define SDL2_QUEUE_SAMPLES 2822 /* ~64ms at 44100Hz */
#define SDL2_QUEUE_SAMPLES_BYTES (SDL2_QUEUE_SAMPLES * sizeof(int16_t))
#define FADEIN_SAMPLES_SHIFT 8
#define FADEIN_SAMPLES ((1 << FADEIN_SAMPLES_SHIFT) - 1)

typedef struct _AudioBackend {
    SDL_AudioDeviceID devId;
    SDL_AudioSpec spec;
    int16_t batchBuf[SAMPLE_BATCH_SIZE];
    uint8_t batchPos;
    uint8_t fadeinPos;
} AudioBackend;

static void AudioQueueDebugCheck(AudioBackend* audio __maybe_unused)
{
#ifdef AUDIO_RL
/* Critical: less than 1 SDL2 buffer pull remaining */
#define QUEUE_UNDERRUN_THRESHOLD (AUDIO_BUFFER_SAMPLES * sizeof(int16_t))
#define QUEUE_OVERRUN_THRESHOLD  (SDL2_QUEUE_SAMPLES_BYTES * 2)
    static bool firstBuffer;
    uint32_t qBytes = SDL_GetQueuedAudioSize(audio->devId);

    if (unlikely(!firstBuffer) && qBytes > QUEUE_UNDERRUN_THRESHOLD) {
        firstBuffer = true;
        return;
    }
    if (qBytes < QUEUE_UNDERRUN_THRESHOLD || qBytes > QUEUE_OVERRUN_THRESHOLD)
        LogPrintWrn("Audio queue underrun/overrun detected. qBytes=%u, min=%zu, max=%zu.\n",
                    qBytes, QUEUE_UNDERRUN_THRESHOLD, QUEUE_OVERRUN_THRESHOLD);
#endif
}

void AudioPushSample(AudioBackend* audio, int16_t sample)
{
    if (unlikely(audio->fadeinPos != FADEIN_SAMPLES))
        sample = (sample * audio->fadeinPos++) >> FADEIN_SAMPLES_SHIFT;

    audio->batchBuf[audio->batchPos++] = sample;

    if (likely(audio->batchPos != SAMPLE_BATCH_SIZE))
        return;

    SDL_QueueAudio(audio->devId, audio->batchBuf, sizeof(audio->batchBuf));
    audio->batchPos = 0;

#ifdef AUDIO_RL
    AudioRateLimiterTick(SAMPLE_BATCH_SIZE);
#else
    while (SDL_GetQueuedAudioSize(audio->devId) > SDL2_QUEUE_SAMPLES_BYTES)
        SDL_Delay(1);
#endif

    AudioQueueDebugCheck(audio);
}

AudioBackend* AudioInit(void)
{
    AudioBackend* audio = MemAlloc(sizeof(AudioBackend));

    *audio = (AudioBackend) {
        .batchPos = 0,
    };

    SDL_AudioSpec want = {
        .freq = AUDIO_SAMPLE_RATE,
        .format = AUDIO_S16SYS,
        .channels = 1,
        .samples = AUDIO_BUFFER_SAMPLES,
    };

    audio->devId = SDL_OpenAudioDevice(NULL, 0, &want, &audio->spec, 0);
    if (!audio->devId) {
        LogPrintErr("Failed SDL_OpenAudioDevice: %s\n", SDL_GetError());
        MemFree(audio);
        return NULL;
    }
    SDL_PauseAudioDevice(audio->devId, 0);

    return audio;
}

void AudioFree(AudioBackend* audio)
{
    if (!audio)
        return;

    SDL_PauseAudioDevice(audio->devId, 1);
    SDL_CloseAudioDevice(audio->devId);
    MemFree(audio);
}
