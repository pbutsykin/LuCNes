/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#define CNES_VIDEO
#define VIDEO_SDL2

#include <utils/utils.h>
#include <SDL2/SDL.h>

#include "interface.h"

typedef struct _RGBColor {
    uint8_t r, g, b;
} __attribute__((packed)) RGBColor;

typedef struct _VideoBackend {
    SDL_Window* wind;
    SDL_Renderer* rend;
    SDL_Texture* texture;
    uint16_t width, height;
    RGBColor vbuf[0];
} VideoBackend;

void VideoSetPixel(VideoBackend* video, const uint8_t y, const uint8_t x, const uint8_t color)
{
    static const RGBColor RGBMap[64] = {
        {0x80, 0x80, 0x80}, {0x00, 0x3D, 0xA6}, {0x00, 0x12, 0xB0}, {0x44, 0x00, 0x96},
        {0xA1, 0x00, 0x5E}, {0xC7, 0x00, 0x28}, {0xBA, 0x06, 0x00}, {0x8C, 0x17, 0x00},
        {0x5C, 0x2F, 0x00}, {0x10, 0x45, 0x00}, {0x05, 0x4A, 0x00}, {0x00, 0x47, 0x2E},
        {0x00, 0x41, 0x66}, {0x00, 0x00, 0x00}, {0x05, 0x05, 0x05}, {0x05, 0x05, 0x05},
        {0xC7, 0xC7, 0xC7}, {0x00, 0x77, 0xFF}, {0x21, 0x55, 0xFF}, {0x82, 0x37, 0xFA},
        {0xEB, 0x2F, 0xB5}, {0xFF, 0x29, 0x50}, {0xFF, 0x22, 0x00}, {0xD6, 0x32, 0x00},
        {0xC4, 0x62, 0x00}, {0x35, 0x80, 0x00}, {0x05, 0x8F, 0x00}, {0x00, 0x8A, 0x55},
        {0x00, 0x99, 0xCC}, {0x21, 0x21, 0x21}, {0x09, 0x09, 0x09}, {0x09, 0x09, 0x09},
        {0xFF, 0xFF, 0xFF}, {0x0F, 0xD7, 0xFF}, {0x69, 0xA2, 0xFF}, {0xD4, 0x80, 0xFF},
        {0xFF, 0x45, 0xF3}, {0xFF, 0x61, 0x8B}, {0xFF, 0x88, 0x33}, {0xFF, 0x9C, 0x12},
        {0xFA, 0xBC, 0x20}, {0x9F, 0xE3, 0x0E}, {0x2B, 0xF0, 0x35}, {0x0C, 0xF0, 0xA4},
        {0x05, 0xFB, 0xFF}, {0x5E, 0x5E, 0x5E}, {0x0D, 0x0D, 0x0D}, {0x0D, 0x0D, 0x0D},
        {0xFF, 0xFF, 0xFF}, {0xA6, 0xFC, 0xFF}, {0xB3, 0xEC, 0xFF}, {0xDA, 0xAB, 0xEB},
        {0xFF, 0xA8, 0xF9}, {0xFF, 0xAB, 0xB3}, {0xFF, 0xD2, 0xB0}, {0xFF, 0xEF, 0xA6},
        {0xFF, 0xF7, 0x9C}, {0xD7, 0xE8, 0x95}, {0xA6, 0xED, 0xAF}, {0xA2, 0xF2, 0xDA},
        {0x99, 0xFF, 0xFC}, {0xDD, 0xDD, 0xDD}, {0x11, 0x11, 0x11}, {0x11, 0x11, 0x11},
    };
    const uint32_t idx = (uint32_t)(y * video->width) + x; /* XXX: Get rid of mul */

    LogPrintAssert(idx < (video->width * video->height),
                   "Access to video buffer out of bounds. idx: %u, y: %u, x: %u, vbuf size: %u\n",
                   idx, y, x, (video->width * video->height));

    LogPrintAssert(color < sizeof(RGBMap)/sizeof(RGBMap[0]), "Invalid color index for RGBMap\n");

    video->vbuf[idx] = RGBMap[color];
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
