/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_VIDEO_INTERFACE_
#define __CNES_VIDEO_INTERFACE_

typedef struct _VideoBackend VideoBackend;

#define VIDEO_FRAME_WIDTH_BITS 8
#define VIDEO_FRAME_WIDTH (1 << VIDEO_FRAME_WIDTH_BITS)
#define VIDEO_FRAME_HEIGHT 240

VideoBackend* VideoInit(void);

void VideoFree(VideoBackend* video);

void VideoFrameFlush(VideoBackend* video);

void VideoSetPixel(VideoBackend* video, const uint8_t y, const uint8_t x, const uint8_t color);

#endif /* __CNES_VIDEO_INTERFACE_ */
