/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Pavel Butsykin
 */
#define CNES_AUDIO
#define AUDIO_COREAUDIO

#include <AudioToolbox/AudioToolbox.h>
#include <dispatch/dispatch.h>
#include <utils/utils.h>

#include "interface.h"

#define FADEIN_SAMPLES_SHIFT 8
#define FADEIN_SAMPLES ((1 << FADEIN_SAMPLES_SHIFT) - 1)

#define AQ_LATENCY_MS 64
#define NUM_AQ_BUFS 2
#define AQ_BUF_SAMPLES DIV_ROUND_UP(AUDIO_SAMPLE_RATE * AQ_LATENCY_MS / 1000, NUM_AQ_BUFS)
#define AQ_BUF_BYTES (AQ_BUF_SAMPLES * sizeof(int16_t))

typedef struct _AudioBackend {
    AudioQueueRef        queue;
    dispatch_semaphore_t sem;
    AudioQueueBufferRef  bufs[NUM_AQ_BUFS];
    uint16_t writePos;
    uint8_t writeIdx;
    uint8_t fadeinPos;
} AudioBackend;

static void AQOutputCallback(void* ctx, AudioQueueRef queue __maybe_unused,
                             AudioQueueBufferRef buf __maybe_unused)
{
    AudioBackend* audio = ctx;

    if (!dispatch_semaphore_signal(audio->sem))
        LogPrintWrn("Audio underrun: writeIdx=%d writePos=%d/%d\n",
                    audio->writeIdx, audio->writePos, AQ_BUF_SAMPLES);
}

void AudioPushSample(AudioBackend* audio, int16_t sample)
{
    int idx = audio->writeIdx, pos = audio->writePos;

    if (unlikely(audio->fadeinPos != FADEIN_SAMPLES))
        sample = (sample * audio->fadeinPos++) >> FADEIN_SAMPLES_SHIFT;

    if (unlikely(!pos))
        dispatch_semaphore_wait(audio->sem, DISPATCH_TIME_FOREVER);

    ((int16_t*)audio->bufs[idx]->mAudioData)[pos++] = sample;

    if (unlikely(pos == AQ_BUF_SAMPLES)) {
        OSStatus err = AudioQueueEnqueueBuffer(audio->queue, audio->bufs[idx], 0, NULL);
        if (err)
            LogPrintWrn("AudioQueueEnqueueBuffer failed: %d\n", (int)err);

        pos = 0;
        audio->writeIdx = idx ^ 1;
    }
    audio->writePos = pos;
}

AudioBackend* AudioInit(void)
{
    AudioStreamBasicDescription fmt = {
        .mSampleRate = AUDIO_SAMPLE_RATE,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                        kLinearPCMFormatFlagIsPacked,
        .mBytesPerPacket = sizeof(int16_t),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = sizeof(int16_t),
        .mChannelsPerFrame = 1,
        .mBitsPerChannel = 16,
    };
    OSStatus err;

    AudioBackend* audio = MemAlloc(sizeof(AudioBackend));
    *audio = (AudioBackend) {
        .writeIdx = 0,
    };

    err = AudioQueueNewOutput(&fmt, AQOutputCallback, audio, NULL, NULL, 0, &audio->queue);
    if (err) {
        LogPrintErr("AudioQueueNewOutput failed: %d\n", (int)err);
        MemFree(audio);
        return NULL;
    }

    audio->sem = dispatch_semaphore_create(NUM_AQ_BUFS);

    for (int i = 0; i < NUM_AQ_BUFS; i++) {
        err = AudioQueueAllocateBuffer(audio->queue, AQ_BUF_BYTES, &audio->bufs[i]);
        if (err) {
            LogPrintErr("AudioQueueAllocateBuffer failed: %d\n", (int)err);
            goto fail_queue;
        }
        audio->bufs[i]->mAudioDataByteSize = AQ_BUF_BYTES;
    }

    err = AudioQueueStart(audio->queue, NULL);
    if (err) {
        LogPrintErr("AudioQueueStart failed: %d\n", (int)err);
        goto fail_queue;
    }
    return audio;

fail_queue:
    AudioQueueDispose(audio->queue, true);
    dispatch_release(audio->sem);
    MemFree(audio);
    return NULL;
}

void AudioFree(AudioBackend* audio)
{
    OSStatus err;

    if (!audio)
        return;

    err = AudioQueueStop(audio->queue, false);
    if (err)
        LogPrintWrn("AudioQueueStop failed: %d\n", (int)err);

    err = AudioQueueDispose(audio->queue, true);
    if (err)
        LogPrintWrn("AudioQueueDispose failed: %d\n", (int)err);

    dispatch_release(audio->sem);
    MemFree(audio);
}
