/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#define CNES_INPUT
#define INPUT_EMPTY

#include <utils/utils.h>

#include "interface.h"

uint8_t InputRead(InputBackend* input __maybe_unused, InputPort port __maybe_unused)
{
    return 0;
}

InputBackend* InputInit(void)
{
    return (InputBackend*)-1;
}

void InputFree(InputBackend* input __maybe_unused) {}
