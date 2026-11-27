/* term_win32.c - the Windows console under kollapse.exe (needs VT support: Windows Terminal, recent conhost). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "term.h"

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif

static HANDLE hout, hin;
static DWORD omode, imode;
static int opened, started;
static SMALL_RECT win;                                     /* visible part of the buffer: clicks are relative to it */

void term_write(const char *s, size_t n)
{
    static wchar_t *w; static size_t cap;
    if (n + 1 > cap) { cap = (n + 1) * 2; w = realloc(w, cap * sizeof *w); }
    int k = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, w, (int)cap);
    DWORD wr; WriteConsoleW(hout, w, k, &wr, NULL);
}

void term_close(void)
{
    if (!opened) return;
    static const char bye[] = "\x1b[?80l\x1b[0m\x1b[?25h\x1b[?1049l";
    if (started) term_write(bye, sizeof bye - 1);          /* leaving an alt screen never entered jumps the cursor home */
    SetConsoleMode(hout, omode); SetConsoleMode(hin, imode);
    opened = started = 0;
}
static BOOL WINAPI on_ctrl(DWORD t)
{
    /* close, logoff, shutdown: the console is going away and a normal exit can hang;
       settings are already saved */
    if (t >= CTRL_CLOSE_EVENT) TerminateProcess(GetCurrentProcess(), 0);
    term_close(); ExitProcess(0); return TRUE;
}

int term_open(void)
{
    hout = GetStdHandle(STD_OUTPUT_HANDLE); hin = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(hout, &omode) || !GetConsoleMode(hin, &imode)) {
        fprintf(stderr, "kollapse: needs an interactive console\n");
        return 0;
    }
    if (!SetConsoleMode(hout, omode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN)) {
        fprintf(stderr, "kollapse: this console has no VT support (use Windows Terminal)\n");
        return 0;
    }
    opened = 1;
    return 1;
}

/* called once the UI starts (after the Sixel probe, which needs the normal input mode) */
void term_start(void)
{
    SetConsoleMode(hin, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    static const char hi[] = "\x1b[?1049h\x1b[?25l\x1b[?80h\x1b[2J";   /* ?80h: sixel at top-left, no scrolling */
    term_write(hi, sizeof hi - 1);
    started = 1;
}

void term_size(int *w, int *h)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (!GetConsoleScreenBufferInfo(hout, &bi)) return;
    win = bi.srWindow;
    *w = bi.srWindow.Right - bi.srWindow.Left + 1; *h = bi.srWindow.Bottom - bi.srWindow.Top + 1;
}

void term_title(const char *t) { SetConsoleTitleA(t); }

int term_query(const char *q, char end, char *buf, int max, int count)
{
    DWORD old, wr;
    int n = 0, seen = 0;
    GetConsoleMode(hin, &old);
    SetConsoleMode(hin, old | 0x0200);                     /* ENABLE_VIRTUAL_TERMINAL_INPUT */
    WriteConsoleA(hout, q, (DWORD)strlen(q), &wr, NULL);
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 300 && n < max - 1 && seen < count; ) {
        if (WaitForSingleObject(hin, 30) != WAIT_OBJECT_0) continue;
        INPUT_RECORD ev[32]; DWORD got = 0;
        if (!ReadConsoleInputW(hin, ev, 32, &got)) break;
        for (DWORD i = 0; i < got && n < max - 1; i++)
            if (ev[i].EventType == KEY_EVENT && ev[i].Event.KeyEvent.bKeyDown && ev[i].Event.KeyEvent.uChar.UnicodeChar)
                if ((buf[n++] = (char)ev[i].Event.KeyEvent.uChar.UnicodeChar) == end) seen++;
    }
    buf[n] = 0;
    SetConsoleMode(hin, old);
    return n;
}

static int vk_to_key(KEY_EVENT_RECORD *k)
{
    DWORD st = k->dwControlKeyState;
    int key = core_vk(k->wVirtualKeyCode, (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0, (st & SHIFT_PRESSED) != 0);
    if (key) return key;
    wchar_t c = k->uChar.UnicodeChar;
    return (c >= 32 && c < 127) || core_ctrl_char(c) ? c : 0;
}

int term_poll(int ms, term_event *e)
{
    static INPUT_RECORD ev[32];
    static DWORD n, at;                                    /* events read but not handed out yet */
    e->type = TE_NONE;
    if (at >= n) {
        n = at = 0;
        if (WaitForSingleObject(hin, ms) != WAIT_OBJECT_0) return TE_NONE;
        if (!GetNumberOfConsoleInputEvents(hin, &n) || !n || !ReadConsoleInputW(hin, ev, 32, &n)) { n = 0; return TE_NONE; }
    }
    while (at < n) {
        INPUT_RECORD *r = &ev[at++];
        if (r->EventType == KEY_EVENT && r->Event.KeyEvent.bKeyDown) {
            if ((e->key = vk_to_key(&r->Event.KeyEvent))) { e->type = TE_KEY; return e->type; }
        } else if (r->EventType == MOUSE_EVENT && r->Event.MouseEvent.dwEventFlags == 0) {
            DWORD b = r->Event.MouseEvent.dwButtonState;
            if (!(b & (FROM_LEFT_1ST_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED))) continue;
            e->type = b & FROM_LEFT_1ST_BUTTON_PRESSED ? TE_CLICK : TE_RCLICK;
            e->x = r->Event.MouseEvent.dwMousePosition.X - win.Left; e->y = r->Event.MouseEvent.dwMousePosition.Y - win.Top;
            return e->type;
        }
    }
    return TE_NONE;
}
