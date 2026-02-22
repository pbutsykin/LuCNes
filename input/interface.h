/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2025 Pavel Butsykin
 */
#ifndef __CNES_INPUT_INTERFACE_
#define __CNES_INPUT_INTERFACE_

typedef struct _InputBackend InputBackend;

typedef enum {
    INPUT_PORT_JOY1 = 0,
    INPUT_PORT_JOY2 = 1,

    INPUT_PORT_MAX,
} InputPort;

InputBackend* InputInit(void);
void InputFree(InputBackend* input);

uint8_t InputRead(InputBackend* input, InputPort port);

#endif /* __CNES_INPUT_INTERFACE_ */
