/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#define CNES_VIDEO
#define VIDEO_EMPTY

#include <utils/utils.h>

#include "interface.h"

void VideoSetPixel(VideoBackend* video __maybe_unused, const uint8_t y __maybe_unused,
                   const uint8_t x __maybe_unused, uint8_t color __maybe_unused) {}
void VideoFrameFlush(VideoBackend* video __maybe_unused) {}
VideoBackend* VideoInit(void)
{
    return (VideoBackend*)-1;
}
void VideoFree(VideoBackend* video __maybe_unused) {}
