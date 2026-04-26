/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_AUDIO
#define AUDIO_PIPE
#define _GNU_SOURCE

#include <utils/utils.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <spawn.h>

#include "interface.h"

/* Pipe Audio Backend for Linux
 * Works with PipeWire, PulseAudio, or ALSA.
 */
#define SAMPLE_BATCH_SIZE 128
#define FADEIN_SAMPLES_SHIFT 8
#define FADEIN_SAMPLES ((1 << FADEIN_SAMPLES_SHIFT) - 1)
#define PIPE_BUF_SAMPLES 2048 /* ~46ms at 44100Hz */
#define PIPE_BUF_BYTES (PIPE_BUF_SAMPLES * sizeof(int16_t))

typedef struct _AudioBackend {
    pid_t pid;
    int fd;
    int16_t batchBuf[SAMPLE_BATCH_SIZE];
    uint8_t batchPos;
    uint8_t fadeinPos;
} AudioBackend;

static inline void AudioPipeDebugCheck(AudioBackend* audio __maybe_unused, ssize_t written __maybe_unused)
{
    LogPrintAssert(!waitpid(audio->pid, NULL, WNOHANG), "Audio player process died unexpectedly.\n");

    LogPrintAssert(written == sizeof(audio->batchBuf),"Write error. Expected %zu, wrote %zd.\n",
                   sizeof(audio->batchBuf), written);
}

static pid_t SpawnPlayer(int* outFd)
{
    extern char** environ;
    static const char* const PAPLAY[] = { /* PipeWire or PulseAudio */
        "paplay", "--raw", "--format=s16le", "--rate=44100", "--channels=1",  "--latency-msec=64", NULL
    };
    static const char* const APLAY[] = { /* ALSA */
        "aplay", "-t", "raw", "-f", "S16_LE", "-r", "44100", "-c", "1", "--buffer-time=64000", NULL
    };
    static const char* const* PLAYERS[] = { PAPLAY, APLAY, NULL };

    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; PLAYERS[i]; i++) {
        int pipefd[2];
        pid_t pid;
        posix_spawn_file_actions_t actions;
        int ret;

        if (pipe(pipefd) < 0) {
            LogPrintErr("Failed to create audio pipe: %s\n", strerror(errno));
            continue;
        }

        ret = posix_spawn_file_actions_init(&actions);
        if (ret) {
            LogPrintErr("posix_spawn_file_actions_init failed: %s\n", strerror(ret));
            goto close_pipe;
        }

        ret = posix_spawn_file_actions_adddup2(&actions, pipefd[0], STDIN_FILENO);
        if (ret) {
            LogPrintErr("posix_spawn_file_actions_adddup2 failed: %s\n", strerror(ret));
            goto destroy_actions;
        }

        if (pipefd[0] != STDIN_FILENO) {
            ret = posix_spawn_file_actions_addclose(&actions, pipefd[0]);
            if (ret) {
                LogPrintErr("posix_spawn_file_actions_addclose failed: %s\n", strerror(ret));
                goto destroy_actions;
            }
        }

        ret = posix_spawn_file_actions_addclose(&actions, pipefd[1]);
        if (ret) {
            LogPrintErr("posix_spawn_file_actions_addclose failed: %s\n", strerror(ret));
            goto destroy_actions;
        }

        ret = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        if (ret) {
            LogPrintErr("posix_spawn_file_actions_addopen failed: %s\n", strerror(ret));
            goto destroy_actions;
        }

        ret = posix_spawnp(&pid, PLAYERS[i][0], &actions, NULL, (char* const*)PLAYERS[i], environ);
        if (ret)
            LogPrintWrn("Failed to start %s: %s\n", PLAYERS[i][0], strerror(ret));

destroy_actions:
        posix_spawn_file_actions_destroy(&actions);

        if (!ret) {
            close(pipefd[0]);
            *outFd = pipefd[1];
            return pid;
        }

close_pipe:
        close(pipefd[0]);
        close(pipefd[1]);
    }

    return -1;
}

void AudioPushSample(AudioBackend* audio, int16_t sample)
{
    ssize_t written;

    if (unlikely(audio->fadeinPos != FADEIN_SAMPLES))
        sample = (sample * audio->fadeinPos++) >> FADEIN_SAMPLES_SHIFT;

    audio->batchBuf[audio->batchPos++] = sample;

    if (likely(audio->batchPos != SAMPLE_BATCH_SIZE))
        return;

    written = write(audio->fd, audio->batchBuf, sizeof(audio->batchBuf));
    audio->batchPos = 0;

    AudioPipeDebugCheck(audio, written);
}

AudioBackend* AudioInit(void)
{
    int fd;

    pid_t pid = SpawnPlayer(&fd);
    if (pid < 0) {
        LogPrintErr("No audio player found (install pulseaudio-utils or alsa-utils)\n");
        return NULL;
    }

    if (fcntl(fd, F_SETPIPE_SZ, PIPE_BUF_BYTES) < 0) {
        LogPrintErr("Can't change pipe buffer size: %s\n", strerror(errno));
        close(fd);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    AudioBackend* audio = MemAlloc(sizeof(AudioBackend));
    *audio = (AudioBackend) {
        .pid = pid,
        .fd = fd,
    };
    return audio;
}

void AudioFree(AudioBackend* audio)
{
    if (!audio)
        return;

    close(audio->fd);
    waitpid(audio->pid, NULL, 0);
    MemFree(audio);
}
