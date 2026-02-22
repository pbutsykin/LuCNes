/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_VIDEO_INTERFACE_
#define __CNES_VIDEO_INTERFACE_

typedef struct _VideoBackend VideoBackend;

typedef struct _RGBColor {
    uint8_t r, g, b;
} __attribute__((packed)) RGBColor;

VideoBackend* VideoInit(const uint16_t width, const uint16_t height);

void VideoFree(VideoBackend* video);

void VideoFrameFlush(VideoBackend* video);

void VideoSetPixel(VideoBackend* video, const uint8_t y, const uint8_t x, RGBColor* const color);

#endif /* __CNES_VIDEO_INTERFACE_ */
