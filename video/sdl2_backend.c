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
    uint16_t width, height;
    RGBColor vbuf[0];
} VideoBackend;

void VideoSetPixel(VideoBackend* video, const uint8_t y, const uint8_t x, RGBColor* const color)
{
    const uint32_t idx = (uint32_t)(y * video->width) + x; /* XXX: Get rid of mul */

    LogPrintAssert(idx < (video->width * video->height),
                   "Access to video buffer out of bounds. idx: %u, y: %u, x: %u, vbuf size: %u\n",
                   idx, y, x, (video->width * video->height));

    video->vbuf[idx] = *color;
}

void VideoFrameFlush(VideoBackend* video)
{
    SDL_Event event;
    SDL_WaitEventTimeout(&event, 0);
    if (event.type == SDL_QUIT) {
        LogPrintDbg("Exit due to SDL_QUIT event\n");
        exit(0);
    }

    SDL_UpdateTexture(video->texture, NULL, video->vbuf, sizeof(video->vbuf[0]) * video->width);
    SDL_RenderCopy(video->rend, video->texture, NULL, NULL);
    SDL_RenderPresent(video->rend);
}

VideoBackend* VideoInit(const uint16_t width, const uint16_t height)
{
    VideoBackend* video = MemAlloc(sizeof(VideoBackend) + (sizeof(RGBColor) * width * height));

    video->width = width;
    video->height = height;

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

    video->texture = SDL_CreateTexture(video->rend, SDL_PIXELFORMAT_RGB24,
                                       SDL_TEXTUREACCESS_STATIC, width, height);
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
