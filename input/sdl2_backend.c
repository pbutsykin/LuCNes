/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_INPUT
#define INPUT_SDL2

#include <utils/utils.h>
#include <SDL2/SDL.h>

#include "interface.h"

typedef union _JoyConfig {
    struct {
        uint8_t a;
        uint8_t b;
        uint8_t select;
        uint8_t start;
        uint8_t up;
        uint8_t down;
        uint8_t left;
        uint8_t right;
    };
    uint8_t keys[8];
} JoyConfig;

typedef union _PadConfig {
    struct {
        uint8_t a;
        uint8_t b;
        uint8_t select;
        uint8_t start;
    };
    uint8_t buttons[4];
} PadConfig;

typedef struct _InputBackend {
    JoyConfig kbdConfig[INPUT_PORT_MAX];
    PadConfig padConfig;
    SDL_Joystick* js[INPUT_PORT_MAX];
} InputBackend;

static uint8_t KeyboardRead(const JoyConfig* cfg)
{
    const uint8_t* keys = SDL_GetKeyboardState(NULL);
    uint8_t state = 0;

    for (unsigned n = 0; n < sizeof(cfg->keys); n++)
        state |= keys[cfg->keys[n]] << n;

    return state;
}

static uint8_t GamepadRead(SDL_Joystick* js, const PadConfig* cfg)
{
    uint8_t state = 0;

    for (unsigned n = 0; n < sizeof(cfg->buttons); n++)
        state |= SDL_JoystickGetButton(js, cfg->buttons[n]) << n;

    if (SDL_JoystickNumHats(js) > 0) {
        const uint8_t dpad[4] = {SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT};
        const Uint8 hat = SDL_JoystickGetHat(js, 0);

        for (unsigned n = 0; n < sizeof(dpad) && hat; n++)
            state |= !!(hat & dpad[n]) << (sizeof(cfg->buttons) + n);
    }

    if (SDL_JoystickNumAxes(js) >= 2) {
        const int16_t deadZone = 8000;
        const int16_t lx = SDL_JoystickGetAxis(js, 0);
        const int16_t ly = SDL_JoystickGetAxis(js, 1);

        if (likely(ly)) {
            state |= (ly < -deadZone) << 4; /* Up */
            state |= (ly >  deadZone) << 5; /* Down */
        }
        if (likely(lx)) {
            state |= (lx < -deadZone) << 6; /* Left  */
            state |= (lx >  deadZone) << 7; /* Right */
        }
    }

    return state;
}

uint8_t InputRead(InputBackend* input, InputPort port)
{
    if (input->js[port])
        return GamepadRead(input->js[port], &input->padConfig);

    return KeyboardRead(&input->kbdConfig[port]);
}

InputBackend* InputInit(void)
{
    static InputBackend input = {
        .kbdConfig[INPUT_PORT_JOY1] = {
            .a = SDL_SCANCODE_Z,
            .b = SDL_SCANCODE_X,
            .select = SDL_SCANCODE_RSHIFT,
            .start = SDL_SCANCODE_RETURN,
            .up    = SDL_SCANCODE_UP,
            .down  = SDL_SCANCODE_DOWN,
            .left  = SDL_SCANCODE_LEFT,
            .right = SDL_SCANCODE_RIGHT,
        },
        .kbdConfig[INPUT_PORT_JOY2] = {
            .a = SDL_SCANCODE_G,
            .b = SDL_SCANCODE_H,
            .select = SDL_SCANCODE_T,
            .start = SDL_SCANCODE_Y,
            .up    = SDL_SCANCODE_W,
            .down  = SDL_SCANCODE_S,
            .left  = SDL_SCANCODE_A,
            .right = SDL_SCANCODE_D,
        },
        .padConfig = {
            .a = 2,
            .b = 1,
            .select = 8,
            .start = 9
        },
    };
    int jsIdx = 0;

    SDL_InitSubSystem(SDL_INIT_JOYSTICK);

    for (int i = 0; i < SDL_NumJoysticks() && jsIdx < INPUT_PORT_MAX; i++) {
        input.js[jsIdx] = SDL_JoystickOpen(i);
        if (input.js[jsIdx]) {
            LogPrintDbg("Joystick %d: %s -> Player %d\n",
                            i, SDL_JoystickName(input.js[jsIdx]), jsIdx + 1);
            jsIdx++;
        }
    }
    if (!jsIdx) {
        LogPrintDbg("Player 1: Arrows=D-pad, Z=A, X=B, RShift=Select, Enter=Start\n");
        LogPrintDbg("Player 2: WASD=D-pad, G=A, H=B, T=Select, Y=Start\n");
    }
    return &input;
}

void InputFree(InputBackend* input)
{
    for (int i = 0; i < INPUT_PORT_MAX; i++) {
        if (input->js[i])
            SDL_JoystickClose(input->js[i]);
    }
}
