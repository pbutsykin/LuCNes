/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Pavel Butsykin
 */
#define CNES_INPUT
#define INPUT_VT

#include <signal.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/time.h>
#include <utils/utils.h>

#include "interface.h"

enum {
    BTN_A      = 0,
    BTN_B      = 1,
    BTN_SELECT = 2,
    BTN_START  = 3,
    BTN_UP     = 4,
    BTN_DOWN   = 5,
    BTN_LEFT   = 6,
    BTN_RIGHT  = 7,
};

enum {
    KK_ENTER       = 13,
    KK_RIGHT_SHIFT = 57441,
};

typedef struct {
    uint32_t key;
    uint8_t port;
    uint8_t bit;
} KeyBind;

typedef struct _InputBackend {
    uint8_t state[INPUT_PORT_MAX];
    uint8_t ttl[INPUT_PORT_MAX][8];
    bool kitty;
    struct itimerval timer;
} InputBackend;

#define VT_SEQ(_s) write(STDOUT_FILENO, _s, sizeof(_s) - 1)

static bool ReadStdin(char* buf, size_t sz, int timeout)
{
    struct pollfd pfd = {
        .fd = STDIN_FILENO,
        .events = POLLIN,
    };

    if (poll(&pfd, 1, timeout) <= 0 || !(pfd.revents & POLLIN))
        return false;

    ssize_t n = read(STDIN_FILENO, buf, sz - 1);
    if (n <= 0)
        return false;

    buf[n] = '\0';
    return true;
}

static void TimerHandler(int signo __maybe_unused)
{
    char buf[64];

    if (ReadStdin(buf, sizeof(buf), 0) && strstr(buf, "\x1b[99;5"))
        raise(SIGINT);
}

static const KeyBind cfgKeys[] = {
    { 'z',            INPUT_PORT_JOY1, BTN_A      },
    { 'x',            INPUT_PORT_JOY1, BTN_B      },
    { '/',            INPUT_PORT_JOY1, BTN_SELECT },
    { KK_RIGHT_SHIFT, INPUT_PORT_JOY1, BTN_SELECT },
    { KK_ENTER,       INPUT_PORT_JOY1, BTN_START  },
    { '\n',           INPUT_PORT_JOY1, BTN_START  },

    { 'g',  INPUT_PORT_JOY2, BTN_A      },
    { 'h',  INPUT_PORT_JOY2, BTN_B      },
    { 't',  INPUT_PORT_JOY2, BTN_SELECT },
    { 'y',  INPUT_PORT_JOY2, BTN_START  },
    { 'w',  INPUT_PORT_JOY2, BTN_UP     },
    { 's',  INPUT_PORT_JOY2, BTN_DOWN   },
    { 'a',  INPUT_PORT_JOY2, BTN_LEFT   },
    { 'd',  INPUT_PORT_JOY2, BTN_RIGHT  },
    {}
};

static const KeyBind cfgArrows[] = {
    { 'A',  INPUT_PORT_JOY1, BTN_UP     },
    { 'B',  INPUT_PORT_JOY1, BTN_DOWN   },
    { 'C',  INPUT_PORT_JOY1, BTN_RIGHT  },
    { 'D',  INPUT_PORT_JOY1, BTN_LEFT   },
    {}
};

static const KeyBind* FindKey(const KeyBind* tab, uint32_t key)
{
    for (; tab->key; tab++) {
        if (tab->key == key)
            return tab;
    }
    return NULL;
}

static void ProcessSeq(InputBackend* in, const char* body, char final)
{
    const char ctrl_c[] = "99;5";
    if (unlikely(!strncmp(body, ctrl_c, sizeof(ctrl_c) - 1))) {
        raise(SIGINT);
        return;
    }

    const KeyBind* key = final == 'u' ? FindKey(cfgKeys, atoi(body)) :
                                        FindKey(cfgArrows, final);
    if (!key)
        return;

    if (strstr(body, ":3"))
        in->state[key->port] &= ~(1u << key->bit);
    else
        in->state[key->port] |= (1u << key->bit);
}

static void ScanKitty(InputBackend* in)
{
    char buf[256];
    char* p = buf;
    const char csi[] = "\x1b[";
    const char csiTerms[] = "uABCD";

    if (!ReadStdin(buf, sizeof(buf), 0))
        return;

    while ((p = strstr(p, csi)) && (p += sizeof(csi) - 1)) {
        char body[16];
        char* end = strpbrk(p, csiTerms);
        size_t len;

        if (!end)
            break;

        len = end - p;
        if (len < sizeof(body)) {
            memcpy(body, p, len);
            body[len] = '\0';
            ProcessSeq(in, body, *end);
        }
        p = end + 1;
    }
}

static void LegacyPress(InputBackend* in, const KeyBind* tab, uint32_t key)
{
#define KEY_TTL 6

    const KeyBind* k = FindKey(tab, key);
    if (!k)
        return;
    in->state[k->port] |= (1u << k->bit);
    in->ttl[k->port][k->bit] = KEY_TTL;
}

static void ScanLegacy(InputBackend* in)
{
    char buf[256];
    const char csi[] = "\x1b[";

    if (!ReadStdin(buf, sizeof(buf), 0))
        return;

    for (char* p = buf; *p; p++) {
        if (!strncmp(p, csi, sizeof(csi) - 1)) {
            p += sizeof(csi) - 1;
            if (!*p)
                break;
            LegacyPress(in, cfgArrows, (uint32_t)*p);
            continue;
        }
        uint8_t c = (uint8_t)*p;
        if (c == 0x03) {
            raise(SIGINT);
            return;
        }
        if (c >= 'A' && c <= 'Z')
            c += 32;
        LegacyPress(in, cfgKeys, c);
    }
}

static void TickTTL(InputBackend* in, unsigned port)
{
    for (unsigned i = 0; i < 8; i++)
        if (in->ttl[port][i] && !--in->ttl[port][i])
            in->state[port] &= ~(1u << i);
}

uint8_t InputRead(InputBackend* input, InputPort port)
{
    if (input->kitty) {
        setitimer(ITIMER_REAL, &input->timer, NULL);
        ScanKitty(input);
    } else {
        TickTTL(input, port);
        ScanLegacy(input);
    }
    return input->state[port];
}

static void (*origInt)(int);
static void (*origTerm)(int);

static void InputSignalHandler(int signo)
{
    VT_SEQ("\x1b[<u"); /* pop Kitty flags */
    signal(signo, signo == SIGINT ? origInt : origTerm);
    raise(signo);
}

static bool IsKittySupported(void)
{
    char buf[64];

    VT_SEQ("\x1b[?u");

    return ReadStdin(buf, sizeof(buf), 100) && strstr(buf, "\x1b[?");
}

InputBackend* InputInit(void)
{
    static InputBackend input = {
        .timer = {
            .it_interval = { .tv_usec = 500000 },
            .it_value    = { .tv_usec = 500000 },
        },
    };

    input.kitty = IsKittySupported();
    if (input.kitty) {
        /* push Kitty keyboard mode: disambiguate | report_events | report_all_keys */
        VT_SEQ("\x1b[>11u");
        origInt  = signal(SIGINT,  InputSignalHandler);
        origTerm = signal(SIGTERM, InputSignalHandler);
        signal(SIGALRM, TimerHandler);
        setitimer(ITIMER_REAL, &input.timer, NULL);
    } else
        LogPrintWrn("Terminal doesn't support Kitty keyboard protocol, using legacy input\n");

    LogPrintDbg("Player 1: Arrows=D-pad, Z=A, X=B, /=Select, Enter=Start\n");
    LogPrintDbg("Player 2: WASD=D-pad, G=A, H=B, T=Select, Y=Start\n");

    return &input;
}

void InputFree(InputBackend* input)
{
    if (input->kitty) {
        const struct itimerval itv = {0};

        setitimer(ITIMER_REAL, &itv, NULL);
        signal(SIGALRM, SIG_DFL);

        VT_SEQ("\x1b[<u");

        signal(SIGINT,  origInt);
        signal(SIGTERM, origTerm);
    }
}
