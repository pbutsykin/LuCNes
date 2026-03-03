//
// TTY devices
// ------------
//
// - Kernel implementes PTYs (pseudoterminals) as a pair of chardevs: slave (e.g. /dev/pts/3) and master (e.g. /dev/ptmx).
//   Slave behaves like a classic serial-line tty.
//
// - Terminal emulator opens the PTY master which allocates a slave for that terminal. The terminal then forks a shell
//   and attaches STDIN/OUT/ERR to the allocated slave. The terminal emulator sits on the other end of the master/slave pair.
//
// - Shell/app can use ttyname to get its slave PTY node (or just STDOUT_FILENO).
//   Or "tty" from a shell directly.
//   Or "ll `readlink /dev/stdout`"
//
// - Bytes written by the terminal emulator into the master PTY appear on the slave FD as if typed by the user and vice versa.
//   User keystroke -> terminal emulator -> write to PTY master -> read from PTY slave -> deliver input to shell/app via STDIN.
//
// - VT/xterm escape sequences work in the same manner:
//   1. Shell/app: 0x1B 0x5B 0x33 0x31 0x6D   # ESC [ 3 1 m  → “set foreground color to red” -> written to PTY slave via STDOUT_FILENO
//   2. Kernel PTY layer: handles line discipline, raw mode (bytes pass through unchanged) vs canonical mode (control chars handled),
//      forwards data to PTY master.
//   3. Terminal emulator: reads data from PTY master, recognizes `ESC [ 31 m`, changes rendering color, paints to the screen.
//

//
// termios
// -------
//
// Below are some flag bits that make sense for a software emulated TTY
//
// c_iflag - Controls how input bytes are processed by PTY before being send to the slave
//
//   ICRNL - turns incoming \r into \n unless IGNCR is set. Disable it to see raw \r bytes.
//   INLCR - turns incoming \n into \r. 
//   IGNCR - ignores incoming \r.
//   IXON/IXOFF - Enable XON/XOFF flow control.
//                When enabled, ctrl+s (XOFF) will pause output from kernel, and ctrl+q (XON) will resume it.
//   IXANY - Any character will act as XON.
//   ISTRIP - Strip the MSB in each incoming byte. Turns input into 7-bit bytes.
//
// c_oflags - Controls how output bytes are processed by PTY after recieving them from the slave
//
//   OPOST - enable output processing.
//           when cleared all bytes are passed through as-is and all following oflags have no effect.
//   ONCLR - turns each outgoing \n into \r\n
//   OCRNL - turns each outgoing \r into \n
//   ONOCR - suppress \r at column 0, instead do nothing and drop the \r.
//           avoids unncecessary processing in PTY or something.
//   ONLRET - perform a carriage return on each \r.
//            almost the same as turning \r into \r\n but doesn't add a \n explicitly.
//   TABDLY (TAB0/TAB1/TAB2/TAB3/XTABS) - Linux specific, expands tabs to 0/1/2/8 spaces.
//                                        Historically was inserting line delays with tabs, not relevant anymore.
//   NL0 - drops every \n
//   NL1 - turns every \n into \r\n, similar to ONLCR.
//
// c_cflags - Line control flags. Most are noops for software TTYs.
//
//   CSIZE (CS5/CS6/CS7/CS8) - char bitsize mask.
//   CREAD - enable char ingress. when disabled the app will not see any input.
//   CLOCAL - enable local mode. when enabled will ignore modem control signals (like carrier-detect).
//   PARENB/PARODD - even/odd parity bits. just disable.
//   CSTOPB - two stop bits if set, 1 if cleared. just disable.
//   CRTSCTS - hardware RTS/CTS flow control. just disable.
//
// c_lflags - More input editing flags.
//
//   ICANON - canonical mode.
//            when set input is line-buffered, special-char editing is enabled.
//            when cleared input is raw and unbuffered.
//   ECHO - echo typed characters back to the terminal automatically.
//   ECHOE - If ICANON is set, typing backspace will erase the last char.
//   ECHOK - If ICANON is set, kill-line char (ctrl+u) erases the current line.
//   ECHONL - If ECHO is set, \n is still echoed.
//   ISIG - enable VINTR/VQUIT/VSUSP signals.
//   NOFLSH - don't flush the buffer in canonical mode on signals.
//   TOSTOP - background processes that attempt to write to the terminal will generate SIGSTOU
//   PENDIN - when a SIGUSP is received, TTY reprints any pending input after SIGCONT.
//   IEXTEN - Linux specific, enables special input processing.
//

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>

#include <unistd.h>
#include <termios.h>
#include <sys/unistd.h>
#include <sys/ioctl.h>

#include "vtrenderlib.h"

#ifndef NDEBUG
#define DBG
#endif

#ifdef DBG
#define DBG_LOG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DBG_LOG(fmt, ...)
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef CLAMP
#define CLAMP(v, l, u) (MIN(MAX((v), (l)), (u)))
#endif

// Character cell dims in dots
#define VT_CELL_YDOTS ((uint16_t)4)
#define VT_CELL_XDOTS ((uint16_t)2)

// We preallocate enough space to hold the biggest number of draw calls (3 + 11 colored bytes per escape seq)
// plus some slack to hold a cursor reset commands.
#define VT_MIN_SEQLIST_SLACK                ((size_t)64)
#define VT_COLOR_SEQ_MAXLEN                 ((size_t)11)
#define VT_DRAW_SEQ_LEN                     ((size_t)3)
#define VT_CELL_SEQS_MAX                    (VT_COLOR_SEQ_MAXLEN + VT_DRAW_SEQ_LEN)
#define VT_SEQLIST_BUFFER_SIZE(rows, cols)  MAX((size_t)((rows + 1) * cols + 1) * VT_CELL_SEQS_MAX, VT_MIN_SEQLIST_SLACK)

struct vtr_stencil_buf
{
    uint16_t ydots;
    uint16_t xdots;
    uint8_t* buffer;
    uint8_t* fgcolors;
    uint8_t* textoverlay;
};

struct vtr_canvas
{
    int fd;
    struct termios origattrs;

    // Canvas dimentions in char cells
    uint16_t nrows;
    uint16_t ncols;

    // Canvas dimensions in dots
    uint16_t ydots;
    uint16_t xdots;

    // Double-buffered stencil
    struct vtr_stencil_buf sb[2];
    struct vtr_stencil_buf* cur_sb;

    // Escape sequence list buffer
    char* seqlist;
    size_t seqcap;
};

static int create_stencil_buf(struct vtr_stencil_buf* sb, uint16_t rows, uint16_t cols)
{
    memset(sb, 0, sizeof(*sb));

    sb->buffer = calloc(rows, cols);
    if (!sb->buffer) {
        goto error_out;
    }

    sb->fgcolors = calloc(rows, cols);
    if (!sb->fgcolors) {
        goto error_out;
    }

    sb->textoverlay = calloc(rows, cols);
    if (!sb->textoverlay) {
        goto error_out;
    }

    sb->xdots = cols * VT_CELL_XDOTS;
    sb->ydots = rows * VT_CELL_YDOTS;

    return 0;

error_out:
    free(sb->fgcolors);
    free(sb->buffer);

    return -ENOMEM;
}

static void free_stencil_buf(struct vtr_stencil_buf* sb)
{
    if (sb) {
        free(sb->buffer);
        free(sb->fgcolors);
        free(sb->textoverlay);
        sb->ydots = sb->xdots = 0;
        sb->buffer = sb->fgcolors = NULL;
    }
}

struct vtr_canvas* vtr_canvas_create(int ttyfd)
{
    int error = 0;

    struct termios attrs;
    error = tcgetattr(ttyfd, &attrs);
    if (error) {
        return NULL;
    }

    struct winsize ws;
    error = ioctl(ttyfd, TIOCGWINSZ, &ws);
    if (error) {
        return NULL;
    }

    struct vtr_canvas* vt = malloc(sizeof(*vt));
    if (!vt) {
        return NULL;
    }

    struct vtr_stencil_buf sb1 = {0};
    struct vtr_stencil_buf sb2 = {0};
    char* seqlist = NULL;

    if (0 != create_stencil_buf(&sb1, ws.ws_row, ws.ws_col) || 0 != create_stencil_buf(&sb2, ws.ws_row, ws.ws_col)) {
        goto error_out;
    }

    size_t seqcap = VT_SEQLIST_BUFFER_SIZE(ws.ws_row, ws.ws_col);
    seqlist = malloc(seqcap);
    if (!seqlist) {
        goto error_out;
    }

    vt->fd = ttyfd;
    vt->nrows = ws.ws_row;
    vt->ncols = ws.ws_col;
    vt->ydots = ws.ws_row * VT_CELL_YDOTS;
    vt->xdots = ws.ws_col * VT_CELL_XDOTS;
    vt->sb[0] = sb1;
    vt->sb[1] = sb2;
    vt->cur_sb = &vt->sb[0];
    vt->seqlist = seqlist;
    vt->seqcap = seqcap;
    memcpy(&vt->origattrs, &attrs, sizeof(attrs));

    return vt;

error_out:

    free_stencil_buf(&sb1);
    free_stencil_buf(&sb2);
    free(seqlist);
    free(vt);

    return NULL;
}

static int sendseq(int ttyfd, const char* seq, size_t nbytes)
{
    ssize_t res = write(ttyfd, seq, nbytes);
    if (res == -1 || (size_t)res != nbytes) {
        return -1;
    }

    return 0;
}

int vtr_reset(struct vtr_canvas* vt)
{
    int error;

    struct termios attrs;
    error = tcgetattr(vt->fd, &attrs);
    if (error) {
        return error;
    }
    
    attrs.c_oflag &= ~OPOST;
    attrs.c_cflag &= ~CREAD;
    attrs.c_lflag &= ~(ICANON | ECHO | IEXTEN);

    error = tcsetattr(vt->fd, TCSANOW, &attrs);
    if (error) {
        return error;
    }

    // switch to alternate buffer, hide cursor and reset attributes
    error |= sendseq(vt->fd, "\x1B[?1049h", 8);
    error |= sendseq(vt->fd, "\x1B[?25l", 6);
    error |= sendseq(vt->fd, "\x1B[2J", 4);
    error |= sendseq(vt->fd, "\x1B[0m", 4);

    return error;
}

int vtr_resize(struct vtr_canvas* vt)
{
    int error;
    struct winsize ws;
    error = ioctl(vt->fd, TIOCGWINSZ, &ws);
    if (error) {
        return error;
    }

    if (ws.ws_row == vt->nrows && ws.ws_col == vt->ncols) {
        return 0;
    }

    struct vtr_stencil_buf sb1 = {0};
    struct vtr_stencil_buf sb2 = {0};
    char* seqlist = NULL;

    if (0 != create_stencil_buf(&sb1, ws.ws_row, ws.ws_col) || 0 != create_stencil_buf(&sb2, ws.ws_row, ws.ws_col)) {
        goto error_out;
    }

    size_t seqcap = VT_SEQLIST_BUFFER_SIZE(ws.ws_row, ws.ws_col);
    seqlist = malloc(seqcap);
    if (!seqlist) {
        goto error_out;
    }

    // No use keeping the previous buffer contents since those
    // are invalid in the new dimentions anyway.
    free_stencil_buf(&vt->sb[0]);
    free_stencil_buf(&vt->sb[1]);
    free(vt->seqlist);

    vt->nrows = ws.ws_row;
    vt->ncols = ws.ws_col;
    vt->ydots = ws.ws_row * VT_CELL_YDOTS;
    vt->xdots = ws.ws_col * VT_CELL_XDOTS;
    vt->sb[0] = sb1;
    vt->sb[1] = sb2;
    vt->cur_sb = &vt->sb[0];
    vt->seqlist = seqlist;
    vt->seqcap = seqcap;

    vtr_clear_screen(vt);

    return 0;

error_out:

    free_stencil_buf(&sb1);
    free_stencil_buf(&sb2);
    free(seqlist);

    return -1;
}

uint16_t vtr_xdots(struct vtr_canvas* vt)
{
    return vt->xdots;
}

uint16_t vtr_ydots(struct vtr_canvas* vt)
{
    return vt->ydots;
}
    
void vtr_close(struct vtr_canvas* vt)
{
    tcsetattr(vt->fd, TCSANOW, &vt->origattrs);
    free_stencil_buf(&vt->sb[0]);
    free_stencil_buf(&vt->sb[1]);
    free(vt->seqlist);

    // switch back to main buffer and restore cursor
    (void) sendseq(vt->fd, "\x1B[?1049l", 8);
    (void) sendseq(vt->fd, "\x1B[?25h", 6);

    free(vt);
}

int vtr_clear_screen(struct vtr_canvas* vt)
{
    return sendseq(vt->fd, "\x1B[2J", 4);
}

static char* extend_seq_buf(struct vtr_canvas* vt)
{
    DBG_LOG("Ran out of sequence list capacity %zu\n", vt->seqcap);

    vt->seqcap <<= 1;
    assert(vt->seqcap > 0);

    char* seqlist = realloc(vt->seqlist, vt->seqcap);
    if (!seqlist) {
        vt->seqcap >>= 1;
        return NULL;
    }

    vt->seqlist = seqlist;
    return seqlist;
}

static size_t set_pos_s(char* seq, size_t seqcap, uint16_t row, uint16_t col)
{
    // Not having enough space for \0 is fine (which is the nwritten == seqcap case).
    size_t nwritten = snprintf(seq, seqcap, "\x1B[%d;%dH", row, col);
    assert(nwritten <= seqcap);
    return nwritten;
}

static size_t draw_cell_s(char* seq, __attribute__((unused)) size_t seqcap, uint8_t mask)
{
    assert(seqcap >= 3);

    seq[0] = 0xE2;
    seq[1] = 0xA0 | (mask >> 6);
    seq[2] = 0x80 | (mask & 0x3f);

    return 3;
}

static size_t u8_to_dec(char* dst, uint8_t v)
{
    if (v >= 100) {
        dst[0] = '0' + v / 100;
        dst[1] = '0' + (v / 10) % 10;
        dst[2] = '0' + v % 10;
        return 3;
    }
    if (v >= 10) {
        dst[0] = '0' + v / 10;
        dst[1] = '0' + v % 10;
        return 2;
    }
    dst[0] = '0' + v;
    return 1;
}

static size_t set_foreground_color_s(char* seq, __attribute__((unused)) size_t seqcap, uint8_t fgc)
{
    assert(seqcap >= VT_COLOR_SEQ_MAXLEN);

    if (fgc == VTR_COLOR_DEFAULT) {
        memcpy(seq, "\x1b[39m", 5);
        return 5;
    }
    const char fgc256[] = "\x1b[38;5;";
    size_t fgc256_len = sizeof(fgc256) - 1;

    memcpy(seq, fgc256, fgc256_len);
    size_t n = u8_to_dec(&seq[fgc256_len], fgc - 1);
    seq[fgc256_len + n] = 'm';

    assert(fgc256_len + n + 1 <= VT_COLOR_SEQ_MAXLEN);
    return fgc256_len + n + 1;
}

static size_t put_char_s(char* seq, __attribute__((unused)) size_t seqcap, char chr)
{
    assert(seqcap >= 1);
    seq[0] = chr;
    return 1;
}

int vtr_swap_buffers(struct vtr_canvas* vt)
{
    struct vtr_stencil_buf* cur_sb = vt->cur_sb;
    struct vtr_stencil_buf* prev_sb = (vt->cur_sb == &vt->sb[0] ? &vt->sb[1] : &vt->sb[0]);
    size_t seqlen = 0;
    size_t cell_idx = 0;
    bool cell_skipped = true;

    uint8_t cur_fgc = VTR_COLOR_DEFAULT;
    seqlen += set_foreground_color_s(vt->seqlist, vt->seqcap, VTR_COLOR_DEFAULT);

    for (uint16_t row = 1; row <= vt->nrows; row++) {
        for (uint16_t col = 1; col <= vt->ncols; col++, cell_idx++) {
            bool is_overlaid = cur_sb->textoverlay[cell_idx] != 0;
            bool is_text_diff = cur_sb->textoverlay[cell_idx] != prev_sb->textoverlay[cell_idx];
            bool is_cell_diff = (cur_sb->buffer[cell_idx] != prev_sb->buffer[cell_idx]) ||
                                (cur_sb->fgcolors[cell_idx] != prev_sb->fgcolors[cell_idx]);

            if (!is_text_diff && (is_overlaid || !is_cell_diff)) {
                cell_skipped = true;
                continue;
            }

            // We're going to draw something, check if we're nearing the end of our seqlist buffer and extend it.
            // The most chars we can generate per iteration is ~12 (set_pos) + 6 (fgcolor) + 3 (draw).
            // The check is made with a lot of slack just to be sure.
            if (vt->seqcap - seqlen <= VT_MIN_SEQLIST_SLACK && !extend_seq_buf(vt)) {
                return -ENOMEM;
            }

            if (cell_skipped) {
                seqlen += set_pos_s(vt->seqlist + seqlen, vt->seqcap - seqlen, row, col);
                cell_skipped = false;
            }

            // Underlying buffer cell might just got un-overlaid so we need to draw it uncoditionally
            if (!is_overlaid && (is_text_diff || is_cell_diff)) {
                // Actual braille cell has a different mask layout, bit numbers displayed below.
                // So we need to convert our stencil first.
                //
                // +---+---+
                // | 1 | 4 |
                // +---+---+
                // | 2 | 5 |
                // +---+---+
                // | 3 | 6 |
                // +---+---+
                // | 7 | 8 |
                // +---+---+

                uint8_t stencil = cur_sb->buffer[cell_idx];
                uint8_t bcell = (stencil & 0x7) | (stencil & 0x8) << 3 | (stencil & 0x70) >> 1 | (stencil & 0x80);
                uint8_t fgc = cur_sb->fgcolors[cell_idx];

                if (fgc != cur_fgc) {
                    seqlen += set_foreground_color_s(vt->seqlist + seqlen, vt->seqcap - seqlen, fgc);
                    cur_fgc = fgc;
                }

                seqlen += draw_cell_s(vt->seqlist + seqlen, vt->seqcap - seqlen, bcell);
            } else if (is_overlaid && is_text_diff) {
                if (cur_fgc != VTR_COLOR_DEFAULT) {
                    seqlen += set_foreground_color_s(vt->seqlist + seqlen, vt->seqcap - seqlen, VTR_COLOR_DEFAULT);
                    cur_fgc = VTR_COLOR_DEFAULT;
                }

                seqlen += put_char_s(vt->seqlist + seqlen, vt->seqcap - seqlen, cur_sb->textoverlay[cell_idx]);
            }
        }
    }

    memset(prev_sb->buffer, 0, vt->nrows * vt->ncols);
    memset(prev_sb->fgcolors, 0, vt->nrows * vt->ncols);
    memset(prev_sb->textoverlay, 0, vt->nrows * vt->ncols);
    vt->cur_sb = prev_sb;

    return sendseq(vt->fd, vt->seqlist, seqlen);
}

static void render_dot(struct vtr_stencil_buf* sb, uint16_t x, uint16_t y, vtr_fgcolor_t fgc)
{
    assert(x < sb->xdots && y < sb->ydots);

    uint16_t row = y / VT_CELL_YDOTS;
    uint16_t col = x / VT_CELL_XDOTS;
    uint16_t ncols = sb->xdots / VT_CELL_XDOTS;

    uint8_t stencil = (1u << (y & (VT_CELL_YDOTS - 1))) << ((x & (VT_CELL_XDOTS - 1)) * 4);
    sb->buffer[row * ncols + col] |= stencil;
    sb->fgcolors[row * ncols + col] = fgc;
}

static void print_char(struct vtr_stencil_buf* sb, uint16_t row, uint16_t col, char c)
{
    uint16_t ncols = sb->xdots / VT_CELL_XDOTS;
    sb->textoverlay[row * ncols + col] = c;
}

static inline bool point_test(struct vtr_canvas* vt, int x, int y)
{
    return (x >= 0 && x < vt->xdots) && (y >= 0 && y < vt->ydots);
}

void vtr_render_dot(struct vtr_canvas* vt, int x, int y)
{
    vtr_render_dotc(vt, x, y, VTR_COLOR_DEFAULT);
}

void vtr_render_dotc(struct vtr_canvas* vt, int x, int y, vtr_fgcolor_t fgc)
{
    if (point_test(vt, x, y)) {
        render_dot(vt->cur_sb, x, y, fgc);
    }
}

/**
 * Calculate a line slope.
 * Returns INFINITY if slope is undefined.
 */
static float calc_slope(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    return (x1 == x0 ? INFINITY : ((float)y1 - y0) / ((float)x1 - x0));
}

static void scan_line(struct vtr_stencil_buf* sb, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, vtr_fgcolor_t fgc)
{
    assert(x0 < sb->xdots);
    assert(x1 < sb->xdots);
    assert(y0 < sb->ydots);
    assert(y1 < sb->ydots);

    float m = calc_slope(x0, y0, x1, y1);
    int hdir = (x0 < x1 ? 1 : -1);
    int vdir = (y0 < y1 ? 1 : -1);

    if (m == 0.0) {
        for (int x = x0; x != x1 + hdir; x += hdir)
        {
            render_dot(sb, x, y0, fgc);
        }
    } else if (m == INFINITY) {
        for (int y = y0; y != y1 + vdir; y += vdir)
        {
            render_dot(sb, x0, y, fgc);
        }
    } else if (m == -1.0 || m == 1.0) {
        for (int x = x0, y = y0; x != x1 + hdir; x += hdir, y += vdir)
        {
            render_dot(sb, x, y, fgc);
        }
    } else {

        // Generic slope case.
        // A line is y - y0 = m(x - x0) in screen dot space.
        // 
        // We scan either the x or the y coord depending on which one increases faster
        // and plug the scanned value into the line equation to get the other one.
        //
        // If the resulting value has a fractional part it means the line fragment sits in 2 adjacent dot boxes.
        // The fraction distance between the 2 integers tells us which box has the most of the line.
        // We will render the dot that has the largest fragment.
        //
        // If the distance is 0.5 then both boxes have equal fragment sizes, in which case we opt to render both.

        float yf = 0, xf = 0;

        if (m > -1.0 && m < 1.0) {
            for (int x = x0, y = y0; x != x1 + hdir;)
            {
                render_dot(sb, x, y, fgc);
                if (yf - y == -0.5) {
                    render_dot(sb, x, y - 1, fgc);
                }

                x += hdir;
                yf = m * ((float)x - x1) + y1;
                y = roundf(yf);
            }
        } else {
            for (int x = x0, y = y0; y != y1 + vdir;)
            {
                render_dot(sb, x, y, fgc);
                if (xf - x == -0.5) {
                    render_dot(sb, x - 1, y, fgc);
                }

                y += vdir;
                xf = ((float)y - y1) / m + x1;
                x = roundf(xf);
            }
        }
    }
}

void vtr_scan_line(struct vtr_canvas* vt, int x0, int y0, int x1, int y1)
{
    vtr_scan_linec(vt, x0, y0, x1, y1, VTR_COLOR_DEFAULT);
}

void vtr_scan_linec(struct vtr_canvas* vt, int x0, int y0, int x1, int y1, vtr_fgcolor_t fgc)
{
    int dx = x1 - x0, dy = y1 - y0;
    int xmin = 0, ymin = 0;
    int xmax = vt->xdots - 1, ymax = vt->ydots - 1;

    // precomputed edge test parameters: left, right, bottom, top
    int p[4] = { -dx, dx, -dy, dy };
    int q[4] = { x0 - xmin, xmax - x0, y0 - ymin, ymax - y0 };

    // find edge constraints on t in p * t <= q for each edge
    // p == 0:
    //  0 * t <= q, where q < 0 -> impossible, reject
    //  0 * t <= q, where q >= 0 -> trivial, no restriction on t from this edge
    //
    // p != 0:
    //  t <= q / p -> t is where we cross the edge boundary
    //  if q < 0 we enter the rect at t >= q / p -> tenter = max(tenter, t)
    //  if q > 0 we exit the rect at t <= q / p -> texit = min(texit, t)
    //
    // For line to be clipped to the rect we need tenter <= texit

    float tentry = 0, texit = 1;
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) {
                return;
            }
        } else {
            float t = (float)(q[i]) / p[i];
            if (p[i] < 0) {
                tentry = MAX(tentry, t);
            } else {
                texit = MIN(texit, t);
            }
        }
    }

    if (tentry <= texit) {
        scan_line(vt->cur_sb,
                  roundf(x0 + tentry * dx), roundf(y0 + tentry * dy),
                  roundf(x0 + texit * dx), roundf(y0 + texit * dy),
                  fgc);
    }
}

int vtr_trace_poly(struct vtr_canvas* vt, size_t nvertices, const struct vtr_vertex* vlist)
{
    return vtr_trace_polyc(vt, nvertices, vlist, VTR_COLOR_DEFAULT);
}

int vtr_trace_polyc(struct vtr_canvas* vt, size_t nvertices, const struct vtr_vertex* vlist, vtr_fgcolor_t fgc)
{
    assert(vt);
    assert(vlist);

    if (nvertices == 0) {
        return 0;
    }

    if (nvertices == 1) {
        vtr_render_dotc(vt, vlist[0].x, vlist[0].y, fgc);
        return 0;
    }

    if (nvertices == 2) {
        vtr_scan_linec(vt, vlist[0].x, vlist[0].y, vlist[1].x, vlist[1].y, fgc);
        return 0;
    }

    assert(nvertices >= 3);

    int cross = 0;
    int ymax = INT_MIN, ymin = INT_MAX;
    for (size_t i = 0; i < nvertices; i++) {
        ymin = MIN(ymin, vlist[i].y);
        ymax = MAX(ymax, vlist[i].y);

        struct vtr_vertex a, b, c;
        a = vlist[i];
        b = (i + 1 == nvertices ? vlist[0] : vlist[i + 1]);
        c = (i + 2 == nvertices ? vlist[0] : (i + 1 == nvertices ? vlist[1] : vlist[i + 2]));

        // Calculate 2d cross products and make sure they maintain the same sign
        int cross2  = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (cross2 == 0) {
            continue;
        } else if ((cross > 0 && cross2 < 0) || (cross < 0 && cross2 > 0)) {
            return -EINVAL;
        } else {
            cross = cross2;
        }
    }

    // Clip bounding box
    if ((ymin < 0 && ymax < 0) || (ymin >= vt->ydots && ymax >= vt->ydots)) {
        return 0;
    } else {
        ymin = CLAMP(ymin, 0, vt->ydots - 1);
        ymax = CLAMP(ymax, 0, vt->ydots - 1);
    }

    // Line-scan the convex polygon
    for (int y = ymin; y != ymax + 1; y++) {
        int xcepts[2];
        size_t ncepts = 0;

        for (size_t i = 0; i < nvertices; i++) {
            struct vtr_vertex a = vlist[i];
            struct vtr_vertex b = (i + 1 == nvertices ? vlist[0] : vlist[i + 1]);

            if (y == a.y && y == b.y) {
                // Special case: edge is horizontal and has no x intercepts.
                vtr_scan_linec(vt, a.x, a.y, b.x, b.y, fgc);
            } else if ((y == a.y || y == b.y) && (y == ymin || y == ymax)) {
                // Special case: x intercept is a min/max vertex
                int xcept = (y == a.y ? a.x : b.x);
                vtr_render_dotc(vt, xcept, y, fgc);
            } else if ((y >= a.y && y <= b.y) || (y >= b.y && y <= a.y)) {
                int xcept = (int)((float)(a.x - b.x) * (y - b.y) / (a.y - b.y) + b.x);

                // Intercepts can repeat if 2 edges meet at current y, ignore that
                switch(ncepts) {
                    case 2: if (xcepts[1] == xcept) continue;
                            /* fallthrough */
                    case 1: if (xcepts[0] == xcept) continue;
                            /* fallthrough */
                    case 0: break;
                }

                assert(ncepts < 2);
                xcepts[ncepts++] = xcept;
            }
        }

        if (ncepts == 2) {
            vtr_scan_linec(vt, xcepts[0], y, xcepts[1], y, fgc);
        } else if (ncepts == 1) {
            vtr_render_dotc(vt, xcepts[0], y, fgc);
        }
    }

    return 0;
}

int vtr_print_text(struct vtr_canvas* vt, uint16_t row, uint16_t col, const char* str)
{
    assert(vt);
    assert(str);

    if (row >= vt->nrows || col >= vt->ncols) {
        return -EINVAL;
    }

    while (col < vt->ncols && *str != '\0') {
        print_char(vt->cur_sb, row, col, *str);
        str++;
        col++;
    }

    return 0;
}
