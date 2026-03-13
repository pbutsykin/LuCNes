/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Pavel Butsykin
 */
#define CNES_VIDEO
#define VIDEO_VT

#include <signal.h>
#include <utils/utils.h>

#include "interface.h"
#include "vtrenderlib/vtrenderlib.h"

typedef struct _VideoBackend {
    struct vtr_canvas* vt;
    uint16_t width, height;
    uint8_t vbuf[];
} VideoBackend;

#define BRAILLE_CELL_HEIGHT 4
#define BRAILLE_CELL_WIDTH  2
#define BRAILLE_CELL_SIZE   (BRAILLE_CELL_HEIGHT * BRAILLE_CELL_WIDTH)

void VideoSetPixel(VideoBackend* video, const uint8_t y, const uint8_t x, const uint8_t color)
{
    video->vbuf[(uint16_t)(y * video->width) + x] = color;
}

static vtr_fgcolor_t dominant_color(const uint8_t* cell, uint8_t ndots)
{
    uint8_t dominant = 0, max_votes = 0;

    for (uint8_t i = 0; i < ndots; i++) {
        const uint8_t color = cell[i];

        if (color == dominant || VTR_COLOR256(color) == VTR_COLOR_BLACK)
            continue;
        for (uint8_t j = i, count = 0; j < ndots; j++) {
            if (color == cell[j] && ++count > max_votes) {
                max_votes = count;
                dominant = color;
            }
        }
    }
    return VTR_COLOR256(dominant);
}

void VideoFrameFlush(VideoBackend* video)
{
    static const uint8_t xterm256ColorMap[64] = {
        244,  25,  19,  54, 125, 160, 124,  88,
         52,  22,  22,  22,  23,   0,   0,   0,
        251,  33,  27,  99, 199, 197, 196, 166,
        166,  64,  28,  29,  32, 234,   0,   0,
        231,  45,  75, 177, 207, 210, 209, 214,
        214, 148,  47,  49,  51, 240,   0,   0,
        231, 159, 159, 183, 219, 217, 223, 229,
        229, 186, 157, 158, 159, 253,   0,   0,
    };
    for (uint16_t cy = 0; cy < video->height; cy += BRAILLE_CELL_HEIGHT) {
        for (uint16_t cx = 0; cx < video->width; cx += BRAILLE_CELL_WIDTH) {
            uint8_t cell[BRAILLE_CELL_SIZE];
            uint8_t ndots = 0;

            for (uint8_t dy = 0; dy < BRAILLE_CELL_HEIGHT && cy + dy < video->height; dy++) {
                for (uint8_t dx = 0; dx < BRAILLE_CELL_WIDTH && cx + dx < video->width; dx++) {
                    uint8_t colorIdx = video->vbuf[(cy + dy) * video->width + cx + dx];

                    LogPrintAssert(colorIdx < sizeof(xterm256ColorMap)/sizeof(xterm256ColorMap[0]),
                                   "Invalid color index for xterm256ColorMap\n");
                    cell[ndots++] = xterm256ColorMap[colorIdx];
                }
            }

            vtr_fgcolor_t dominant = dominant_color(cell, ndots);
            if (dominant == VTR_COLOR_BLACK)
                continue;

            ndots = 0;
            for (uint8_t dy = 0; dy < BRAILLE_CELL_HEIGHT; dy++) {
                for (uint8_t dx = 0; dx < BRAILLE_CELL_WIDTH; dx++) {
                    uint8_t color = cell[ndots++];
                    if (VTR_COLOR256(color) != VTR_COLOR_BLACK)
                        vtr_render_dotc(video->vt, cx + dx, cy + dy, dominant);
                }
            }
        }
    }

    vtr_swap_buffers(video->vt);
}

static VideoBackend* g_video;

static void handle_signal(int signo)
{
    if (g_video->vt) {
        vtr_close(g_video->vt);
        g_video->vt = NULL;
    }
    signal(signo, SIG_DFL);
    raise(signo);
}

VideoBackend* VideoInit(const uint16_t width, const uint16_t height)
{
    VideoBackend* video = MemAlloc(sizeof(VideoBackend) + sizeof(video->vbuf[0]) * width * height);

    memset(video->vbuf, 63, width * height);  /* fill video->vbuf with black color idx */

    video->vt = vtr_canvas_create(STDOUT_FILENO);
    if (!video->vt) {
        LogPrintErr("vtr_canvas_create failed\n");
        goto fail0;
    }
    g_video = video;
    video->width = MIN(width,  vtr_xdots(video->vt));
    video->height = MIN(height, vtr_ydots(video->vt));

    if (vtr_reset(video->vt)) {
        LogPrintErr("vtr_reset failed\n");
        goto fail1;
    }
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    return video;

fail1:
    vtr_close(video->vt);
fail0:
    MemFree(video);
    return NULL;
}

void VideoFree(VideoBackend* video)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    if (video->vt) {
        vtr_close(video->vt);
        video->vt = NULL;
    }
    MemFree(video);
}
