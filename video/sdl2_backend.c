/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#define CNES_VIDEO
#define VIDEO_SDL2

#include <utils/utils.h>
#include <SDL2/SDL.h>

#include "interface.h"

typedef struct _VideoBackend {
    SDL_Window* wind;
    SDL_Renderer* rend;
    SDL_Texture* texture;
    uint16_t vbuf[];
} VideoBackend;

void VideoSetPixel(VideoBackend* video, const uint8_t y, const uint8_t x, const uint8_t color)
{
    static const uint16_t RGB565Map[64] = {
        0x8410, 0x01F4, 0x0096, 0x4012, 0xA00B, 0xC005, 0xB820, 0x88A0,
        0x5960, 0x1220, 0x0240, 0x0225, 0x020C, 0x0000, 0x0020, 0x0020,
        0xC638, 0x03BF, 0x22BF, 0x81BF, 0xE976, 0xF94A, 0xF900, 0xD180,
        0xC300, 0x3400, 0x0460, 0x044A, 0x04D9, 0x2104, 0x0841, 0x0841,
        0xFFFF, 0x0EBF, 0x6D1F, 0xD41F, 0xFA3E, 0xFB11, 0xFC46, 0xFCE2,
        0xFDE4, 0x9F01, 0x2F86, 0x0F94, 0x07DF, 0x5AEB, 0x0861, 0x0861,
        0xFFFF, 0xA7FF, 0xB77F, 0xDD5D, 0xFD5F, 0xFD56, 0xFE96, 0xFF74,
        0xFFB3, 0xD752, 0xA775, 0xA79B, 0x9FFF, 0xDEFB, 0x1082, 0x1082,
    };
    const uint32_t idx = (uint32_t)(y << VIDEO_FRAME_WIDTH_BITS) + x;

    LogPrintAssert(idx < (VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT),
                   "Access to video buffer out of bounds. idx: %u, y: %u, x: %u, vbuf size: %u\n",
                   idx, y, x, (VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT));

    LogPrintAssert(color < sizeof(RGB565Map)/sizeof(RGB565Map[0]), "Invalid color index for RGB565Map\n");

    video->vbuf[idx] = RGB565Map[color];
}

void VideoFrameFlush(VideoBackend* video)
{
    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, 0) && event.type == SDL_QUIT) {
        LogPrintDbg("Exit due to SDL_QUIT event\n");
        exit(0);
    }

    SDL_UpdateTexture(video->texture, NULL, video->vbuf, sizeof(video->vbuf[0]) * VIDEO_FRAME_WIDTH);
    SDL_RenderCopy(video->rend, video->texture, NULL, NULL);
    SDL_RenderPresent(video->rend);
}

VideoBackend* VideoInit(void)
{
    VideoBackend* video = MemAlloc(sizeof(VideoBackend) +
                                   (sizeof(video->vbuf[0]) * VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT));
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11");
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {

        SDL_SetHint(SDL_HINT_VIDEODRIVER, "");
        if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
            LogPrintErr("SDL_Init failed: %s\n", SDL_GetError());
            goto fail1;
        }
        LogPrintWrn("X11 isn't available, using %s . Sync rendering may cause audio glitches.\n",
                     SDL_GetCurrentVideoDriver());
    }
    atexit(SDL_Quit);

    video->wind = SDL_CreateWindow("LuCNes", SDL_WINDOWPOS_CENTERED, /* XXX: Configure windows size */
                                   SDL_WINDOWPOS_CENTERED, 640 + 240, 480 + 240, SDL_WINDOW_RESIZABLE);
    if (!video->wind) {
        LogPrintErr("Failed SDL_CreateWindow: %s\n", SDL_GetError());
        goto fail1;
    }

    video->rend = SDL_CreateRenderer(video->wind, -1, SDL_RENDERER_ACCELERATED);
    if (!video->rend) {
        LogPrintErr("Failed SDL_CreateRenderer: %s\n", SDL_GetError());
        goto fail2;
    }

    video->texture = SDL_CreateTexture(video->rend, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC,
                                       VIDEO_FRAME_WIDTH, VIDEO_FRAME_HEIGHT);
    if (!video->texture) {
        LogPrintErr("Failed SDL_CreateTexture: %s\n", SDL_GetError());
        goto fail3;
    }

    return video;

fail3:
    SDL_DestroyRenderer(video->rend);
fail2:
    SDL_DestroyWindow(video->wind);
fail1:
    MemFree(video);
    return NULL;
}

void VideoFree(VideoBackend* video)
{
    SDL_DestroyTexture(video->texture);
    SDL_DestroyRenderer(video->rend);
    SDL_DestroyWindow(video->wind);
    MemFree(video);
}
