/* term_posix.c - POSIX tty: raw input, escape-sequence keys, SGR mouse, VT output. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include "core.h"
#include "term.h"

static struct termios saved;
static int opened, started;

void term_write(const char *s, size_t n)
{
    while (n) {
        ssize_t w = write(STDOUT_FILENO, s, n);
        if (w <= 0) return;
        s += w; n -= (size_t)w;
    }
}
static void put_s(const char *s) { term_write(s, strlen(s)); }

void term_close(void)
{
    if (!opened) return;
    if (started) put_s("\x1b[?1000l\x1b[?1006l\x1b[?80l\x1b[0m\x1b[?25h\x1b[?1049l");
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    opened = started = 0;
}
static void on_signal(int sig) { term_close(); signal(sig, SIG_DFL); raise(sig); }

int term_open(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) { fprintf(stderr, "kollapse: needs an interactive terminal\n"); return 0; }
    if (tcgetattr(STDIN_FILENO, &saved)) { perror("kollapse"); return 0; }
    struct termios raw = saved;                            /* every key as it comes; Ctrl+C is a key too */
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    opened = 1;
    atexit(term_close);
    signal(SIGTERM, on_signal); signal(SIGHUP, on_signal); signal(SIGQUIT, on_signal); signal(SIGSEGV, on_signal);
    return 1;
}

void term_start(void)
{
    put_s("\x1b[?1049h\x1b[?25l\x1b[?80h\x1b[2J\x1b[?1000h\x1b[?1006h");   /* alt screen, no cursor, sixel at top-left, mouse */
    started = 1;
}

void term_size(int *w, int *h)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) { *w = ws.ws_col; *h = ws.ws_row; }
}

void term_title(const char *t) { char b[256]; snprintf(b, sizeof b, "\x1b]0;%s\a", t); put_s(b); }

/* ---------- input: bytes from the tty, turned into keys and clicks ---------- */
static unsigned char in[1024];
static int nin;

static int fill_in(int ms)                                /* read whatever arrives within ms; 1 if anything did */
{
    fd_set r; struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    FD_ZERO(&r); FD_SET(STDIN_FILENO, &r);
    if (nin >= (int)sizeof in || select(STDIN_FILENO + 1, &r, NULL, NULL, &tv) <= 0) return 0;
    ssize_t k = read(STDIN_FILENO, in + nin, sizeof in - (size_t)nin);
    if (k <= 0) return 0;
    nin += (int)k;
    return 1;
}
static void eat(int k) { memmove(in, in + k, (size_t)(nin - k)); nin -= k; }

int term_query(const char *q, char end, char *buf, int max, int count)
{
    int n = 0, seen = 0;
    put_s(q);
    for (int waited = 0; waited < 300 && n < max - 1 && seen < count; ) {
        if (!fill_in(30)) { waited += 30; continue; }
        while (nin && n < max - 1) { if ((buf[n++] = (char)in[0]) == end) seen++; eat(1); }
    }
    buf[n] = 0;
    return n;
}

/* modifier parameter of a CSI sequence (1 + shift 1 + alt 2 + ctrl 4) -> K_SHIFT / K_CTRL bits */
static int mods(int m) { m = m > 1 ? m - 1 : 0; return (m & 1 ? K_SHIFT : 0) | (m & 4 ? K_CTRL : 0); }
static int with_mods(int key, int m)
{
    int b = mods(m);
    if (!(key >= K_UP && key <= K_END) && key != K_ENTER) b &= ~K_SHIFT;   /* the core takes Shift only there */
    return key | b;
}

/* one event from the front of `in`; returns bytes used, 0 if the sequence is incomplete */
static int parse(term_event *e)
{
    unsigned char c = in[0];
    e->type = TE_KEY;
    if (c != 0x1b) {
        e->key = c == '\r' ? K_ENTER : c == 127 || c == 8 ? K_BACK : c == '\t' ? K_TAB : c;
        if (c >= 0x80) e->type = TE_NONE;                  /* other UTF-8: not a key we use */
        else if (c < 32 && c != '\r' && c != '\t' && c != 8 && !core_ctrl_char(c)) e->type = TE_NONE;
        return 1;
    }
    if (nin < 2) return 0;
    if (in[1] == 'O') {                                    /* SS3: F1-F4, some arrows */
        if (nin < 3) return 0;
        switch (in[2]) {
        case 'P': e->key = K_F1; break; case 'Q': e->key = K_F2; break; case 'R': e->key = K_F3; break; case 'S': e->key = K_F4; break;
        case 'A': e->key = K_UP; break; case 'B': e->key = K_DOWN; break; case 'C': e->key = K_RIGHT; break; case 'D': e->key = K_LEFT; break;
        case 'H': e->key = K_HOME; break; case 'F': e->key = K_END; break; case 'M': e->key = K_ENTER; break;
        default: e->type = TE_NONE;
        }
        return 3;
    }
    if (in[1] != '[') { e->key = in[1] < 128 ? in[1] : 0; if (!e->key) e->type = TE_NONE; return 2; }   /* Alt+key: the key */
    int i = 2;                                             /* CSI: parameters, then a final byte */
    while (i < nin && ((in[i] >= '0' && in[i] <= '9') || in[i] == ';' || in[i] == '<' || in[i] == '?')) i++;
    if (i >= nin) return 0;
    char fin = (char)in[i], p[64];
    int len = i - 2 < 63 ? i - 2 : 63;
    memcpy(p, in + 2, (size_t)len); p[len] = 0;
    int used = i + 1, a = 0, b = 0, d = 0;
    if (p[0] == '<') {                                     /* SGR mouse: ESC[<button;x;yM (press) / m (release) */
        sscanf(p + 1, "%d;%d;%d", &a, &b, &d);
        e->type = TE_NONE;
        if (fin == 'M' && !(a & 32) && !(a & 64)) {        /* presses only: no motion, no wheel */
            if ((a & 3) == 0) e->type = TE_CLICK;
            else if ((a & 3) == 2) e->type = TE_RCLICK;
            e->x = b - 1; e->y = d - 1;
        }
        return used;
    }
    sscanf(p, "%d;%d;%d", &a, &b, &d);
    switch (fin) {
    case 'A': e->key = with_mods(K_UP, b); break;      case 'B': e->key = with_mods(K_DOWN, b); break;
    case 'C': e->key = with_mods(K_RIGHT, b); break;   case 'D': e->key = with_mods(K_LEFT, b); break;
    case 'H': e->key = with_mods(K_HOME, b); break;    case 'F': e->key = with_mods(K_END, b); break;
    case 'P': e->key = K_F1; break; case 'Q': e->key = K_F2; break; case 'R': e->key = K_F3; break; case 'S': e->key = K_F4; break;
    case 'Z': e->key = K_STAB; break;
    case 'u': e->key = a == 13 ? with_mods(K_ENTER, b) : a == 9 && (mods(b) & K_SHIFT) ? K_STAB : 0; break;   /* CSI-u keyboards */
    case '~':
        switch (a) {
        case 1: case 7: e->key = with_mods(K_HOME, b); break;
        case 4: case 8: e->key = with_mods(K_END, b); break;
        case 2: e->key = K_INS; break;
        case 3: e->key = K_DEL; break;
        case 5: e->key = with_mods(K_PGUP, b); break;
        case 6: e->key = with_mods(K_PGDN, b); break;
        case 11: e->key = K_F1; break; case 12: e->key = K_F2; break; case 13: e->key = K_F3; break; case 14: e->key = K_F4; break;
        case 15: e->key = K_F5; break; case 17: e->key = K_F6; break; case 18: e->key = K_F7; break; case 19: e->key = K_F8; break;
        case 20: e->key = K_F9; break; case 21: e->key = K_F10; break;
        case 24: e->key = K_F12; break;
        case 27: e->key = d == 13 ? with_mods(K_ENTER, b) : 0; break;   /* xterm modifyOtherKeys */
        default: e->key = 0;
        }
        break;
    default: e->key = 0;
    }
    if (e->type == TE_KEY && !e->key) e->type = TE_NONE;
    return used;
}

int term_poll(int ms, term_event *e)
{
    e->type = TE_NONE;
    if (!nin && !fill_in(ms)) return TE_NONE;
    while (nin) {
        int used = parse(e);
        if (!used) {                                       /* incomplete escape: wait a moment for the rest */
            if (fill_in(25)) continue;
            if (nin == 1 || in[1] != '[') { eat(1); e->type = TE_KEY; e->key = K_ESC; return e->type; }   /* a lone Esc */
            eat(nin); return TE_NONE;                      /* a broken sequence: drop it */
        }
        eat(used);
        if (e->type != TE_NONE) return e->type;
    }
    return TE_NONE;
}
