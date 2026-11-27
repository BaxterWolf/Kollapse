/* tui.c - the terminal front-end: truecolor text or Sixel pixels, over term.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "compat.h"
#include "core.h"
#include "fb.h"
#include "term.h"

#define MAXW 300
#define MAXH 150
#define TOP_ROWS 16

typedef struct { wchar_t ch; uint32_t fg, bg; } cell;
static cell g[MAXH][MAXW];
static int W = 100, H = 40;
static struct { int x0, y0, x1, y1, id, list, rows; } hot[16];
static int nhot;

static int cx(int x) { return x * W / SCREEN_W; }
static int cy(int y) { return y >= HINT_Y ? H - 1 : y >= TOP_H ? TOP_ROWS : y * TOP_ROWS / TOP_H; }

/* bar lengths in 1/8 cells; old conhost fonts lack U+2589..258F, so halves there */
static int eighths(int e)
{
#ifdef _WIN32
    static int coarse = -1;
    if (coarse < 0) coarse = !getenv("WT_SESSION") && !getenv("TERM_PROGRAM");
    if (coarse) return (e + 2) / 4 * 4;
#endif
    return e;
}

static void put(int x, int y, wchar_t ch, uint32_t fg, uint32_t bg)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    g[y][x].ch = ch; g[y][x].fg = fg; g[y][x].bg = bg;
}
static void box(int x0, int y0, int x1, int y1, uint32_t bg)
{
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) put(x, y, ' ', C_TEXT, bg);
}
static wchar_t wch(unsigned char c)
{
    switch (c) { case 0x18: return 0x2191; case 0x19: return 0x2193; case 0x1a: return 0x2192; case 0x1b: return 0x2190; }
    return c;
}
/* text that keeps the background already there, stops at column max */
static void str(int x, int y, const char *s, uint32_t fg, int max)
{
    for (; *s && x < max; s++, x++)
        if (x >= 0 && x < W && y >= 0 && y < H) { g[y][x].ch = wch((unsigned char)*s); g[y][x].fg = fg; }
}
static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t r = 0;
    for (int s = 0; s < 24; s += 8) r |= (uint32_t)(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t) << s;
    return r;
}
static void add_hot(int x0, int y0, int x1, int y1, int id, int list, int rows)
{
    if (nhot >= 16) return;
    hot[nhot].x0 = x0; hot[nhot].y0 = y0; hot[nhot].x1 = x1; hot[nhot].y1 = y1;
    hot[nhot].id = id; hot[nhot].list = list; hot[nhot++].rows = rows;
}

/* "KOLLAPSE" in a 4x5 pixel font over copper bars, drawn with half blocks (2 pixels per cell) */
static void draw_logo(int x0, int y0, int x1, int y1)
{
    static const char *glyph[8][5] = {
        { "X..X", "X.X.", "XX..", "X.X.", "X..X" }, { ".XX.", "X..X", "X..X", "X..X", ".XX." },
        { "X...", "X...", "X...", "X...", "XXXX" }, { "X...", "X...", "X...", "X...", "XXXX" },
        { ".XX.", "X..X", "XXXX", "X..X", "X..X" }, { "XXX.", "X..X", "XXX.", "X...", "X..." },
        { ".XXX", "X...", ".XX.", "...X", "XXX." }, { "XXXX", "X...", "XXX.", "X...", "XXXX" },
    };
    unsigned bars[8], ink[5];
    core_banner(bars, ink);
    int w = x1 - x0, ph = (y1 - y0) * 2, gap = w >= 8 * 5 + 2 ? 1 : 0, tw = 8 * (4 + gap) - gap;
    int ox = (w - tw) / 2, oy = (ph - 6) / 2, off = (int)(GetTickCount() / 80);
    for (int cy0 = 0; cy0 < y1 - y0; cy0++)
        for (int cx0 = 0; cx0 < w; cx0++) {
            uint32_t c[2];
            for (int h = 0; h < 2; h++) {
                int py = cy0 * 2 + h, gx = cx0 - ox, gy = py - oy;
                c[h] = bars[(py + off) % 8];
                for (int s = 0; s < 2; s++) {         /* s=0: shadow at +1,+1; s=1: letter */
                    int lx = gx - (s ? 0 : 1), ly = gy - (s ? 0 : 1), k = lx / (4 + gap), col = lx % (4 + gap);
                    if (lx >= 0 && ly >= 0 && ly < 5 && k < 8 && col < 4 && glyph[k][ly][col] == 'X') c[h] = s ? ink[ly] : C_DARK;
                }
            }
            put(x0 + cx0, y0 + cy0, 0x2580, c[0], c[1]);
        }
}

/* where each channel's scope / pan bar landed this frame, for click-to-mute */
static struct { int x0, y0, x1, y1, ch; } chit[96];
static int nchit;
static void add_chit(int x0, int y0, int x1, int y1, int ch)
{
    if (nchit >= 96) return;
    chit[nchit].x0 = x0; chit[nchit].y0 = y0; chit[nchit].x1 = x1; chit[nchit].y1 = y1; chit[nchit++].ch = ch;
}

static void draw_scopes(void)
{
    int n = core_nchan();
    if (n == 0) n = 8;
    if (n > 32) n = 32;
    int per = (n + 1) / 2, top = cy(95), rows = (cy(170) - top) / 2;
    if (rows < 1) rows = 1;
    static const uint8_t bit[2][4] = { { 0x01, 0x02, 0x04, 0x40 }, { 0x08, 0x10, 0x20, 0x80 } };
    static uint8_t dots[MAXH][MAXW];
    for (int c = 0; c < n; c++) {
        int sx0 = cx(3 + (c % per) * 285 / per), sx1 = cx(3 + (c % per + 1) * 285 / per) - 1, sy = top + (c / per) * rows;
        int cols = sx1 - sx0, hd = rows * 4, wd = cols * 2, prev = hd / 2, pan, m; float vol;
        if (cols < 1) continue;
        core_chan(c, &pan, &vol, &m);
        memset(dots, 0, sizeof dots);
        for (int i = 0; i < wd; i++) {
            float v = core_scope(c, (float)i / wd);
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            int y = hd / 2 - (int)(v * (hd / 2 - 1));
            if (y >= hd) y = hd - 1;
            int a = prev < y ? prev : y, b = prev < y ? y : prev;
            for (int d = a; d <= b; d++) dots[d / 4][i / 2] |= bit[i & 1][d % 4];
            prev = y;
        }
        for (int r = 0; r < rows; r++)
            for (int k = 0; k < cols; k++) put(sx0 + k, sy + r, 0x2800 + dots[r][k], m ? C_MUTE : C_SCOPE, C_BLACK);
        if (cols >= 4) { char nb[4]; sprintf(nb, "%d", c + 1); str(sx0, sy, nb, m ? C_MUTE : C_LABEL, sx1); }
        add_chit(sx0, sy, sx1, sy + rows, c);
    }
}

static void draw_widget(const widget_t *w)
{
    int x0 = cx(w->x), x1 = cx(w->x + w->w), y0 = cy(w->y), y1 = cy(w->y + w->h);
    if (x1 <= x0) x1 = x0 + 1;
    char buf[256];
    switch (w->type) {
    case W_PANEL: box(x0, y0, x1, y1 > y0 ? y1 : y0 + 1, C_DESK); break;
    case W_SUNK: {                                     /* list well with a sunken bevel frame */
        int t = y0 - 1, b = y1, r = x1 - 1;
        box(x0 + 1, y0, r, y1, C_BLACK);
        for (int x = x0 + 1; x < r; x++) { put(x, t, 0x2500, C_DARK, C_DESK); put(x, b, 0x2500, C_LIGHT, C_DESK); }
        for (int y = y0; y < b; y++) { put(x0, y, 0x2502, C_DARK, C_DESK); put(r, y, 0x2502, C_LIGHT, C_DESK); }
        put(x0, t, 0x256D, C_DARK, C_DESK); put(r, t, 0x256E, C_DARK, C_DESK);
        put(x0, b, 0x2570, C_DARK, C_DESK); put(r, b, 0x256F, C_LIGHT, C_DESK);
        break;
    }
    case W_BG: box(x0, y0, x1, y1, C_BLACK); break;
    case W_BUTTON: {
        int y = cy(w->y + w->h / 2), len = (int)strlen(w->text), bw = x1 - x0;
        if (len > bw) len = bw;
        box(x0, y, x1, y + 1, C_LIGHT);
        char t[32]; snprintf(t, sizeof t, "%.*s", len, w->text);
        str(x0 + (bw - len) / 2, y, t, C_BLACK, x1);
        add_hot(x0, y, x1, y + 1, w->id, 0, 0);
        break;
    }
    case W_LABEL: y0 = cy(w->y + w->h / 2); str(x0, y0, w->text ? w->text : core_field(w->id, buf), C_TEXT, x1 + 1); break;
    case W_FIELD: y0 = cy(w->y + w->h / 2); box(x0, y0, x1 - 1, y0 + 1, C_BLACK); str(x0, y0, core_field(w->id, buf), C_TEXT, x1 - 1); break;
    case W_LIST: {
        int lx0 = x0 + 1, lx1 = x1 - 1, rows = y1 - y0;
        for (int i = 0; i < rows; i++) {
            int num, flags; float gl; char nb[8];
            const char *name = core_list(w->id, i, rows, &num, &flags, &gl);
            int y = y0 + i;
            if (!name) break;
            if (flags & 1) box(lx0, y, lx1, y + 1, C_SEL);
            else if (gl > 0.02f) box(lx0, y, lx1, y + 1, mix(C_BLACK, mix(C_BLACK, C_SCOPE, 0.35f), gl));
            if (w->id == L_FILES) {
                if (flags & 2) put(lx0, y, 0x25BA, C_SCOPE, g[y][lx0].bg);
                str(lx0 + 2, y, name, flags & 2 ? C_SCOPE : flags & 8 ? C_FX : flags & 4 ? C_INS : C_TEXT, lx1);
            } else {
                sprintf(nb, "%02d", num % 100);
                str(lx0, y, nb, mix(C_LABEL, C_SCOPE, gl), lx1);
                str(lx0 + 3, y, name, mix(C_TEXT, 0xFFFFFF, gl), lx1);
            }
        }
        add_hot(lx0, y0, lx1, y1, 0, w->id, rows);
        break;
    }
    case W_LOGO: draw_logo(x0, y0, x1, y1 + 1); break;   /* one extra row: 3 rows = 6 half-block pixels */
    case W_SCOPES: draw_scopes(); break;
    }
}

/* pan strip (one row) + pattern rows + nothing else; hints row is a widget */
static void draw_pattern(void)
{
    int strip = TOP_ROWS, vu = strip + 1, top = strip + 2, bot = H - 1, n = bot - top, mid = top + n / 2;
    int vis, wi = core_pick_width(W - 6, 1, 1, &vis), cw = cell_widths[wi];
    if (vis < 1) vis = 1;
    int colw = (W - 6) / vis, off = core_chan_off(vis), nch = core_nchan(), rows = core_rows(), row = core_row();

    box(0, strip, W, strip + 1, C_DARK);
    for (int v = 0; v < vis && off + v < nch; v++) {
        int ch = off + v, x = 3 + v * colw, pan, m; float vol; char nb[4];
        core_chan(ch, &pan, &vol, &m);
        sprintf(nb, "%02d", (ch + 1) % 100);
        add_chit(x, strip, x + colw, strip + 1, ch);
        int narrow = colw < 8, t0 = narrow ? x : x + 3, tw = narrow ? colw - 1 : colw - 4;   /* narrow: cube only */
        if (!narrow) str(x, strip, nb, m ? C_MUTE : C_LABEL, x + colw);
        for (int i = 0; i < tw; i++) put(t0 + i, strip, i == tw / 2 ? 0x253C : 0x2500, C_DIM, C_DARK);
        if (tw > 1) {                                       /* 1-cell cube positioned in 1/8 cell steps */
            uint32_t cc = m ? C_MUTE : mix(mix(C_BLACK, C_SCOPE, 0.25f), vol > 0.6f ? mix(C_SCOPE, 0xFFFFFF, 0.6f) : C_SCOPE, vol * 1.5f);
            int e = eighths(pan * (tw - 1) * 8 / 255), i = e / 8, f = e % 8;
            if (!f) put(t0 + i, strip, 0x2588, cc, C_DARK);
            else {
                put(t0 + i, strip, 0x2590 - f, C_DARK, cc);     /* left f/8 empty, rest cube */
                put(t0 + i + 1, strip, 0x2590 - f, cc, C_DARK); /* left f/8 cube */
            }
        }
    }
    box(0, vu, W, vu + 1, C_BLACK);                        /* VU row: horizontal bars in 1/8 cells */
    for (int v = 0; v < vis && off + v < nch; v++) {      /* bars start under the pan track, clear of the number */
        int narrow = colw < 8, x = 3 + v * colw + (narrow ? 0 : 3), len = narrow ? colw - 1 : colw - 4;
        int w8 = eighths((int)(len * 8 * core_level(off + v)));
        for (int i = 0; i < len && w8 > 0; i++, w8 -= 8) {
            float t = (float)i / len;
            put(x + i, vu, w8 >= 8 ? 0x2588 : 0x2590 - w8, t > 0.85f ? 0xFF5050 : t > 0.6f ? 0xF0E060 : C_SCOPE, C_BLACK);
        }
    }

    for (int y = top; y < bot; y++) {
        int k = y - mid, r = row + k;
        uint32_t bg = k == 0 ? C_DESK : r >= 0 && r < rows && r % 16 == 0 ? C_BARBG : r >= 0 && r < rows && r % 4 == 0 ? C_BEATBG : C_BLACK;
        box(0, y, W, y + 1, bg);
        if (!rows) { if (k == 0) str(3, y, "pick a track, press Enter", C_TEXT, W); continue; }
        if (r < 0 || r >= rows) continue;
        float fade = 0.7f * (k < 0 ? -k : k) / (n / 2 + 1);    /* rows sink into the dark away from the play line */
        char rn[12]; sprintf(rn, "%02X", r & 0xff);
        str(0, y, rn, mix(k ? C_LABEL : C_TEXT, bg, fade), W);
        str(W - 2, y, rn, mix(k ? C_LABEL : C_TEXT, bg, fade), W);
        for (int v = 0; v < vis && off + v < nch; v++) {
            int ch = off + v, x = 3 + v * colw + (colw - 1 - cw) / 2, pan, m; float vol;
            core_chan(ch, &pan, &vol, &m);
            char t[14]; core_cell(r, ch, t);
            static const int fs[4] = { 0, 4, 7, 10 }, fl[4] = { 3, 2, 2, 3 };
            const uint32_t fc[4] = { C_NOTE, C_INS, C_VOL, C_FX };
            if (v) put(3 + v * colw - 1, y, 0x2502, C_DIM, bg);
            for (int f = 0; f < 4 && fs[f] < cw; f++) {
                int empty = t[fs[f]] == '.' || (f == 0 && t[0] == '-');
                for (int i = 0; i < fl[f]; i++)
                    put(x + fs[f] + i, y, wch((unsigned char)t[fs[f] + i]), mix(m ? C_DIM : empty ? (k ? C_DIM : 0x8090A8) : fc[f], bg, fade), bg);
            }
            int crow, cch, coff, clen;
            if (core_cursor(&crow, &cch, &coff, &clen) && r == crow && ch == cch && coff < cw)   /* editor cursor */
                for (int i = 0; i < clen; i++) put(x + coff + i, y, wch((unsigned char)t[coff + i]), C_BLACK, C_CURSOR);
        }
    }
}

/* ---------- lower-pane pages: message, sample, instrument, spectrum ---------- */
static int pane_text(int y0, int rows_max)                 /* returns the first free row */
{
    int n, first;
    const char **l = core_text(rows_max, &n, &first);
    for (int i = first; i < n && i - first < rows_max; i++, y0++) {
        if (i == core_text_sel()) box(0, y0, W, y0 + 1, C_SEL);   /* selected line (song page) */
        str(1, y0, l[i], !strncmp(l[i], "--", 2) ? C_FX : i == 0 ? C_TEXT : C_NOTE, W - 1);
    }
    return y0;
}

/* braille plot helpers: a dot grid of (cols*2) x (rows*4) over cells starting at (x0,y0) */
static uint8_t bdots[MAXH][MAXW];
static void bset(int x, int y, int cols, int rows)
{
    static const uint8_t bit[2][4] = { { 0x01, 0x02, 0x04, 0x40 }, { 0x08, 0x10, 0x20, 0x80 } };
    if (x >= 0 && y >= 0 && x < cols * 2 && y < rows * 4) bdots[y / 4][x / 2] |= bit[x & 1][y & 3];
}
static void bline(int x0, int y0, int x1, int y1, int cols, int rows)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        bset(x0, y0, cols, rows);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void bflush(int x0, int y0, int cols, int rows, uint32_t fg, uint32_t bg)
{
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < cols; k++)
            if (bdots[r][k] || g[y0 + r][x0 + k].ch == ' ') put(x0 + k, y0 + r, 0x2800 + bdots[r][k], fg, g[y0 + r][x0 + k].bg ? g[y0 + r][x0 + k].bg : bg);
}

static void draw_page(void)
{
    int top = TOP_ROWS, bot = H - 1, page = core_page();
    box(0, top, W, bot, C_BLACK);
    if (page == P_MESSAGE || page == P_SONG || page == P_CONFIG) { pane_text(top, bot - top); return; }
    if (page == P_SPECTRUM) {
        static const wchar_t lv[9] = { ' ', 0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587, 0x2588 };
        int n = (W - 2) / 2 > 64 ? 64 : (W - 2) / 2, rows = bot - top, bw;   /* bars fill the width */
        if (n < 1) n = 1;
        bw = (W - 2) / n;
        float bars[64], peaks[64];
        core_spectrum(bars, peaks, n);
        for (int i = 0; i < n; i++) {
            int h8 = (int)(bars[i] * rows * 8), pk = (int)(peaks[i] * rows);
            for (int r = 0; r < rows; r++) {
                int v = h8 - r * 8; if (v < 0) v = 0; if (v > 8) v = 8;
                float t = (float)r / rows;
                for (int b = 0; b < bw - (bw > 1); b++)
                    put(1 + i * bw + b, bot - 1 - r, r == pk && v < 8 ? 0x2594 : lv[v], r == pk && v < 8 ? 0xFFFFFF : t > 0.8f ? 0xFF5050 : t > 0.55f ? 0xF0E060 : C_SCOPE, C_BLACK);
            }
        }
        return;
    }
    if (page == P_SAMPLE) {
        int y0 = pane_text(top, 11), rows = bot - y0, cols = W - 2;
        if (rows < 2) return;
        float ls, le, pos, lo, hi;
        core_wave_marks(&ls, &le, &pos);
        if (ls >= 0) for (int r = 0; r < rows; r++) for (int x = (int)(ls * cols); x <= (int)(le * cols) && x < cols; x++) g[y0 + r][1 + x].bg = 0x10202C;
        float ra, rb; core_wave_range(&ra, &rb);
        if (ra >= 0) for (int r = 0; r < rows; r++) for (int x = (int)(ra * cols); x <= (int)(rb * cols) && x < cols; x++) g[y0 + r][1 + x].bg = 0x34284C;
        memset(bdots, 0, sizeof bdots);
        int hd = rows * 4, mid = hd / 2;
        for (int x = 0; x < cols * 2; x++) {
            bset(x, mid, cols, rows);
            if (core_wave(x, cols * 2, &lo, &hi)) bline(x, mid - (int)(hi * (mid - 1)), x, mid - (int)(lo * (mid - 1)), cols, rows);
        }
        bflush(1, y0, cols, rows, C_SCOPE, C_BLACK);
        if (ls >= 0) for (int r = 0; r < rows; r++) { put(1 + (int)(ls * (cols - 1)), y0 + r, 0x2502, C_FX, 0x10202C); put(1 + (int)(le * (cols - 1)), y0 + r, 0x2502, C_FX, 0x10202C); }
        if (pos >= 0 && pos <= 1) for (int r = 0; r < rows; r++) put(1 + (int)(pos * (cols - 1)), y0 + r, 0x2502, C_CURSOR, C_BLACK);
        return;
    }
    if (page == P_INSTR && core_instr_view() >= 2) { pane_text(top, bot - top); return; }   /* keymap / vibrato: text */
    if (page == P_INSTR) {
        int y0 = pane_text(top, 5), rows = bot - y0 - 1, cols = (W - 3) / 2;
        if (rows < 2) return;
        for (int e = 0; e < 2; e++) {
            float xs[32], ys[32];
            int sus, lps, lpe, on, n = core_env(e, xs, ys, 32, &sus, &lps, &lpe, &on), x0 = 1 + e * (cols + 1);
            str(x0, y0, e ? "Panning envelope" : "Volume envelope", core_env_sel(e) >= 0 ? C_FX : on ? C_TEXT : C_DIM, x0 + cols);
            if (!n) { str(x0, y0 + 1, "(none)", C_DIM, x0 + cols); continue; }
            if (lps >= 0 && lps < n && lpe < n)
                for (int r = 0; r < rows; r++) for (int x = (int)(xs[lps] * (cols - 1)); x <= (int)(xs[lpe] * (cols - 1)); x++) g[y0 + 1 + r][x0 + x].bg = 0x10202C;
            memset(bdots, 0, sizeof bdots);
            int wd = cols * 2 - 1, hd = rows * 4 - 1;
            for (int i = 1; i < n; i++) bline((int)(xs[i - 1] * wd), hd - (int)(ys[i - 1] * hd), (int)(xs[i] * wd), hd - (int)(ys[i] * hd), cols, rows);
            bflush(x0, y0 + 1, cols, rows, on ? C_SCOPE : C_DIM, C_BLACK);
            if (sus >= 0 && sus < n) put(x0 + (int)(xs[sus] * (cols - 1)), y0 + 1 + rows - 1 - (int)(ys[sus] * (rows - 1)), 0x25CF, C_CURSOR, C_BLACK);
            int sp = core_env_sel(e);
            if (sp >= 0 && sp < n) put(x0 + (int)(xs[sp] * (cols - 1)), y0 + 1 + rows - 1 - (int)(ys[sp] * (rows - 1)), 0x25C6, C_FX, C_BLACK);
        }
    }
}

static void draw_overlay(void)
{
    int n, sel; const char *title;
    const char **lines = core_overlay(&n, &sel, &title);
    if (!lines) return;
    int w = 0;
    for (int i = 0; i < n; i++) if ((int)strlen(lines[i]) > w) w = (int)strlen(lines[i]);
    if ((int)strlen(title) + 2 > w) w = (int)strlen(title) + 2;   /* the title fits too */
    w += 4;
    if (w > W - 2) w = W - 2;
    int h = n + 2, x0 = (W - w) / 2, y0 = (H - h) / 2;
    box(x0, y0, x0 + w, y0 + h, C_DESK);
    for (int x = x0; x < x0 + w; x++) { put(x, y0, 0x2550, C_LIGHT, C_DESK); put(x, y0 + h - 1, 0x2550, C_LIGHT, C_DESK); }
    for (int y = y0; y < y0 + h; y++) { put(x0, y, 0x2551, C_LIGHT, C_DESK); put(x0 + w - 1, y, 0x2551, C_LIGHT, C_DESK); }
    put(x0, y0, 0x2554, C_LIGHT, C_DESK); put(x0 + w - 1, y0, 0x2557, C_LIGHT, C_DESK);
    put(x0, y0 + h - 1, 0x255A, C_LIGHT, C_DESK); put(x0 + w - 1, y0 + h - 1, 0x255D, C_LIGHT, C_DESK);
    char t[128]; snprintf(t, sizeof t, " %s ", title);
    str(x0 + (w - (int)strlen(t)) / 2, y0, t, C_TEXT, x0 + w);
    for (int i = 0; i < n; i++) {
        if (i == sel) box(x0 + 1, y0 + 1 + i, x0 + w - 1, y0 + 2 + i, C_SEL);
        str(x0 + 2, y0 + 1 + i, lines[i], lines[i][0] == '-' ? C_FX : lines[i][0] == ' ' ? C_DIM : C_TEXT, x0 + w - 1);
    }
}

/* right click at cell (mx,my): solo the channel under it */
static void rclick(int mx, int my)
{
    for (int h = 0; h < nchit; h++)
        if (mx >= chit[h].x0 && mx < chit[h].x1 && my >= chit[h].y0 && my < chit[h].y1) { core_solo(chit[h].ch); return; }
}

/* left click at cell (mx,my), against what the last render() drew */
static void click(int mx, int my)
{
    for (int h = 0; h < nchit; h++)
        if (mx >= chit[h].x0 && mx < chit[h].x1 && my >= chit[h].y0 && my < chit[h].y1) { core_toggle_mute(chit[h].ch); return; }
    for (int h = 0; h < nhot; h++)
        if (mx >= hot[h].x0 && mx < hot[h].x1 && my >= hot[h].y0 && my < hot[h].y1) {
            if (hot[h].list) core_click_list(hot[h].list, my - hot[h].y0, hot[h].rows);
            else core_action(hot[h].id);
            return;
        }
}

static int is_header(const widget_t *w) { return w->type == W_LOGO || (w->type == W_LABEL && (w->id == F_FILES_HDR || w->id == F_INSTR_HDR)); }

static void render(void)
{
    nhot = nchit = 0;
    box(0, 0, W, H, C_DESK);
    for (int i = 0; i < layout_count; i++)
        if (!is_header(&layout[i])) draw_widget(&layout[i]);
    for (int i = 0; i < layout_count; i++)                 /* logo (3 rows) and list titles go on top */
        if (layout[i].type == W_LOGO) draw_widget(&layout[i]);
        else if (is_header(&layout[i])) {
            char buf[128], t[132];
            snprintf(t, sizeof t, " %s ", core_field(layout[i].id, buf));
            int x0 = cx(layout[i].x) + 1;
            str(x0, 0, t, C_TEXT, cx(layout[i].x + layout[i].w));
        }
    float p = core_progress();                             /* song progress tints the status line */
    int sy = cy(84), pw = (int)(cx(291) * p);
    for (int x = 0; x < pw && x < W; x++) g[sy][x].bg = mix(C_DESK, C_SEL, 0.9f);
    if (core_page() == P_PLAY || core_page() == P_EDIT) draw_pattern(); else draw_page();
    draw_overlay();
}

/* ---------- output: the cell grid as UTF-8 with truecolor escapes ---------- */
static int utf8(char *o, unsigned c)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
    o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63)); o[2] = (char)(0x80 | (c & 63)); return 3;
}
static char out[MAXW * MAXH * 48];
static void present(void)
{
    int o = 0; uint32_t fg = 0xFFFFFFFF, bg = 0xFFFFFFFF;
    for (int y = 0; y < H; y++) {
        o += sprintf(out + o, "\x1b[%d;1H", y + 1);
        for (int x = 0; x < W; x++) {
            cell *c = &g[y][x];
            if (c->fg != fg) { fg = c->fg; o += sprintf(out + o, "\x1b[38;2;%u;%u;%um", fg >> 16, fg >> 8 & 255, fg & 255); }
            if (c->bg != bg) { bg = c->bg; o += sprintf(out + o, "\x1b[48;2;%u;%u;%um", bg >> 16, bg >> 8 & 255, bg & 255); }
            o += utf8(out + o, c->ch ? (unsigned)c->ch : ' ');
        }
    }
    term_write(out, (size_t)o);
}

static int too_small(void)
{
    if (W >= 64 && H >= TOP_ROWS + 8) return 0;
    box(0, 0, W, H, C_BLACK);
    str(0, 0, "kollapse: terminal too small", C_TEXT, W);
    char b[48]; sprintf(b, "need 64x%d, have %dx%d", TOP_ROWS + 8, W, H);
    str(0, 1, b, C_LIGHT, W);
    return 1;
}

/* ---------- Sixel: the window version's pixels, drawn into the terminal ---------- */
static int cell_w = 10, cell_h = 20;                       /* terminal cell size in pixels (fallback guess) */
static int area_w, area_h, cell_known;                     /* text area in pixels (0 = unknown); cell size answered */
static int sx_maxw = 4096, sx_maxh = 4096;                 /* biggest Sixel image the terminal shows (xterm: 1000x1000) */
static int sx_col, sx_row;                                 /* where the image starts, in cells (centred when capped) */

/* DA1 lists 4 for Sixel. XTSMGRAPHICS asks the largest image first; most terminals ignore it */
static int sixel_supported(void)
{
    char buf[160]; int w, h;
    term_query("\x1b[?2;4;0S\x1b[c", 'c', buf, sizeof buf, 1);
    char *p = strstr(buf, "\x1b[?2;0;");
    if (p && sscanf(p, "\x1b[?2;0;%d;%dS", &w, &h) == 2 && w >= 64 && h >= 64) { sx_maxw = w < 4096 ? w : 4096; sx_maxh = h < 4096 ? h : 4096; }
    for (p = buf; (p = strstr(p, "\x1b[?")) && !strncmp(p, "\x1b[?2;", 5); p++) ;   /* skip to the DA reply */
    if (!p) return 0;
    for (p += 3; *p && *p != 'c'; ) {
        int v = atoi(p);
        if (v == 4) return 1;
        while (*p && *p != ';' && *p != 'c') p++;
        if (*p == ';') p++;
    }
    return 0;
}

/* ask the terminal for its size in pixels (XTWINOPS 14: ESC[4;h;wt) and its cell size (16: ESC[6;h;wt) */
static void query_cell_px(void)
{
    char buf[128]; int h, w; char *p;
    term_query("\x1b[14t\x1b[16t", 't', buf, sizeof buf, 2);
    cell_known = 0;
    if ((p = strstr(buf, "\x1b[6;")) && sscanf(p, "\x1b[6;%d;%dt", &h, &w) == 2 && h > 4 && w > 2) { cell_h = h; cell_w = w; cell_known = 1; }
    area_w = area_h = 0;
    if ((p = strstr(buf, "\x1b[4;")) && sscanf(p, "\x1b[4;%d;%dt", &h, &w) == 2 && h > 16 && w > 16) { area_h = h; area_w = w; }
}
static char *sx;                                           /* output buffer */
static size_t sxn, sxcap;
static void sxput(const char *s, size_t n)
{
    if (sxn + n > sxcap) { sxcap = (sxn + n) * 2; sx = realloc(sx, sxcap); }
    memcpy(sx + sxn, s, n); sxn += n;
}
static void sxf(const char *fmt, int a, int b, int c, int d)
{
    char t[64]; int n = sprintf(t, fmt, a, b, c, d); sxput(t, n);
}
/* Sixel image outW x outH, nearest neighbour, padL/padT transparent.
   Per 6-row band: bucket by colour, then run-length encode */
/* opaque Sixel image TW x TH: UI in ix,iy,iw,ih, black elsewhere so nothing stale survives a resize.
   Per 6-row band: bucket by colour, then run-length encode */
static void sixel_frame(int TW, int TH, int ix, int iy, int iw, int ih)   /* into sx / sxn */
{
    static unsigned char idx[SCREEN_W * SCREEN_H];
    static uint32_t pal[256];
    static short slot[4096];                               /* colour hash -> palette index + 1 */
    static unsigned char cbits[256][4096];                 /* per band: 6-bit column for each colour */
    static int xmap[4096];
    int np = 1, cube = 0;                                  /* palette entry 0 is black (the background) */
    if (TW > 4096) TW = 4096;
    if (TH > 4096) TH = 4096;
    memset(slot, 0, sizeof slot);
    pal[0] = 0; slot[0] = 1;                               /* hash of 0 is 0 */
    for (int i = 0; i < SCREEN_W * SCREEN_H && !cube; i++) {   /* exact palette while it fits */
        uint32_t c = fb[i] & 0xffffff;
        int hs = (int)((c * 2654435761u) >> 20) & 4095;
        while (slot[hs] && pal[slot[hs] - 1] != c) hs = (hs + 1) & 4095;
        if (!slot[hs]) { if (np == 255) { cube = 1; break; } pal[np] = c; slot[hs] = (short)++np; }
        idx[i] = (unsigned char)(slot[hs] - 1);
    }
    if (cube) {                                            /* too many colours: 6x6x6 cube (entry 0 is black) */
        np = 216;
        for (int i = 0; i < 216; i++) pal[i] = (uint32_t)((i / 36) * 51) << 16 | (uint32_t)((i / 6 % 6) * 51) << 8 | (uint32_t)(i % 6 * 51);
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            uint32_t c = fb[i];
            idx[i] = (unsigned char)(((c >> 16 & 255) + 25) / 51 * 36 + ((c >> 8 & 255) + 25) / 51 * 6 + ((c & 255) + 25) / 51);
        }
    }
    for (int x = 0; x < TW; x++) xmap[x] = x >= ix && x < ix + iw ? (x - ix) * SCREEN_W / iw : -1;
    sxn = 0;
    if (sx_col || sx_row) sxf("\x1b[?80l\x1b[%d;%dH\x1bP0;1;0q", sx_row + 1, sx_col + 1, 0, 0);   /* centred: draw at the cursor */
    else sxput("\x1b[H\x1bP0;1;0q", 11);                    /* top-left (DECSDM from term_start), then DCS: start Sixel */
    sxf("\"1;1;%d;%d", TW, TH, 0, 0);
    for (int i = 0; i < np; i++) sxf("#%d;2;%d;%d;%d", i, (int)(pal[i] >> 16 & 255) * 100 / 255, (int)(pal[i] >> 8 & 255) * 100 / 255, (int)(pal[i] & 255) * 100 / 255);
    int used[256], nused;
    static unsigned char seen[256];
    for (int band = 0; band < TH; band += 6) {
        nused = 0;
        for (int k = 0; k < 6 && band + k < TH; k++) {
            int y = band + k, inside = y >= iy && y < iy + ih;
            const unsigned char *row = inside ? idx + (y - iy) * SCREEN_H / ih * SCREEN_W : NULL;
            for (int x = 0; x < TW; x++) {
                int c = inside && xmap[x] >= 0 ? row[xmap[x]] : 0;
                if (!seen[c]) { seen[c] = 1; used[nused++] = c; memset(cbits[c], 0, TW); }
                cbits[c][x] |= (unsigned char)(1 << k);
            }
        }
        for (int u = 0; u < nused; u++) {
            int c = used[u];
            seen[c] = 0;
            if (u) sxput("$", 1);
            sxf("#%d", c, 0, 0, 0);
            const unsigned char *bits = cbits[c];
            for (int x = 0; x < TW; ) {
                int v = bits[x], run = 1;
                while (x + run < TW && bits[x + run] == v) run++;
                if (x + run == TW && !v) break;            /* trailing empty run: skip */
                if (run > 3) sxf("!%d%c", run, 63 + v, 0, 0);
                else for (int r = 0; r < run; r++) { char ch = (char)(63 + v); sxput(&ch, 1); }
                x += run;
            }
        }
        sxput("-", 1);
    }
    sxput("\x1b\\", 2);
    if (sx_col || sx_row) sxput("\x1b[?80h", 6);
}

void term_start(void);

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--dump")) {   /* kollapse --dump W H [track] : plain-text frame ~2 s in */
        if (argc >= 4) { W = atoi(argv[2]); H = atoi(argv[3]); }
        if (W > MAXW) W = MAXW;
        if (H > MAXH) H = MAXH;
        char *args[2] = { argv[0], argc >= 5 ? argv[4] : NULL };
        core_init(argc >= 5 ? 2 : 1, args, 0);
        const char *keys = getenv("KOLLAPSE_KEYS");          /* test hook: see core_hook_key; @x,y. = click */
        while (keys && *keys) {
            int cxp, cyp, used;
            if (*keys == '@' && sscanf(keys + 1, "%d,%d.%n", &cxp, &cyp, &used) == 2) { render(); click(cxp, cyp); keys += 1 + used; }
            else core_key(core_hook_key(&keys));
            if (*keys == '~') { keys++; continue; }        /* ~ = the next key comes right away (chords) */
            for (int i = 0; i < 5; i++) { Sleep(16); core_update(0.016); }
        }
        for (int i = 0; i < 120; i++) { Sleep(16); core_update(0.016); }
        if (getenv("KOLLAPSE_SIXEL_BENCH")) {              /* test: time 40 render+encode frames at a scale */
            int s = atoi(getenv("KOLLAPSE_SIXEL_BENCH"));
            DWORD t0 = GetTickCount(); size_t bytes = 0;
            for (int i = 0; i < 40; i++) { core_update(0.016); fb_render(); sixel_frame(SCREEN_W * s, SCREEN_H * s, 0, 0, SCREEN_W * s, SCREEN_H * s); bytes += sxn; }
            printf("scale %d: %.1f ms/frame, %u KB/frame\n", s, (GetTickCount() - t0) / 40.0, (unsigned)(bytes / 40 / 1024));
            return 0;
        }
        if (getenv("KOLLAPSE_SIXEL_OUT")) {                /* test: write one Sixel frame + the same frame as BMP */
            fb_render();
            int ow = SCREEN_W, oh = SCREEN_H;              /* KOLLAPSE_SIXEL_SIZE=WxH: any output size */
            if (getenv("KOLLAPSE_SIXEL_SIZE")) sscanf(getenv("KOLLAPSE_SIXEL_SIZE"), "%dx%d", &ow, &oh);
            int tw = ow, th = oh, rx = 0, ry = 0, rw = ow, rh = oh;   /* KOLLAPSE_SIXEL_RECT=TW,TH,x,y,w,h: letterboxed */
            if (getenv("KOLLAPSE_SIXEL_RECT")) sscanf(getenv("KOLLAPSE_SIXEL_RECT"), "%d,%d,%d,%d,%d,%d", &tw, &th, &rx, &ry, &rw, &rh);
            sixel_frame(tw, th, rx, ry, rw, rh);
            FILE *f = fopen(getenv("KOLLAPSE_SIXEL_OUT"), "wb");
            if (f) { fwrite(sx, 1, sxn, f); fclose(f); }
            return getenv("KOLLAPSE_SIXEL_BMP") ? fb_dump_bmp(getenv("KOLLAPSE_SIXEL_BMP")) : 0;
        }
        if (!too_small()) render();
        if (getenv("KOLLAPSE_HTML")) {                     /* colour dump for checking the look in a browser */
            printf("<html><head><style>b{display:inline-block;width:9px;height:17px;overflow:hidden;text-align:center;font-weight:normal}"
                   "</style></head><body style='background:#000;margin:0'><pre style='font:15px/17px Consolas,\"Segoe UI Symbol\",monospace;margin:0'>");
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    char u[8]; int n = utf8(u, g[y][x].ch ? (unsigned)g[y][x].ch : ' '); u[n] = 0;
                    printf("<b style='color:#%06X;background:#%06X'>%s</b>", g[y][x].fg, g[y][x].bg,
                           u[0] == '<' ? "&lt;" : u[0] == '&' ? "&amp;" : u[0] == '>' ? "&gt;" : u);
                }
                printf("\n");
            }
            printf("</pre></body></html>\n");
            return 0;
        }
        for (int y = 0; y < H; y++) {
            static char line[MAXW * 4 + 2]; int o = 0;
            for (int x = 0; x < W; x++) o += utf8(line + o, g[y][x].ch ? (unsigned)g[y][x].ch : ' ');
            line[o] = 0;
            puts(line);
        }
        return 0;
    }

    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("kollapse %s\n", KOLLAPSE_VERSION); return 0; }
    if (!term_open()) return 1;
    if (argc > 1 && !strcmp(argv[1], "--probe")) {        /* what does this terminal report? */
        char da[128]; int sxok = sixel_supported();
        term_query("\x1b[c", 'c', da, sizeof da, 1);
        query_cell_px();
        term_close();
        for (char *q = da; *q; q++) if (*q == 0x1b) *q = '^';  /* make the escape visible */
        printf("device attributes: %s\nsixel: %s (largest image %dx%d)\ncell size: %s %dx%d px\ntext area: %dx%d px\nmode kollapse will use: %s\n",
               da[0] ? da : "(no reply)", sxok ? "yes" : "no", sx_maxw, sx_maxh, cell_known ? "" : "(guess)", cell_w, cell_h, area_w, area_h,
               sxok ? "pixel graphics (Sixel)" : "text");
        return 0;
    }
    /* pixel graphics (Sixel) by default; --text / --simple: draw with text characters; --sixel: force Sixel */
    int sixel = 1, forced = 0;
    for (int i = 1; i < argc; i++) {
        int skip = 0;
        if (!strcmp(argv[i], "--text") || !strcmp(argv[i], "--simple")) { sixel = 0; skip = 1; }
        else if (!strcmp(argv[i], "--sixel")) {
            sixel = forced = 1;
            skip = 1 + (i + 1 < argc && (argv[i + 1][0] == '1' || argv[i + 1][0] == '2') && !argv[i + 1][1]);  /* old "--sixel 2" */
        }
        if (skip) { for (int j = i; j + skip < argc; j++) argv[j] = argv[j + skip]; argc -= skip; i--; }
    }
    if (sixel && !forced && !sixel_supported()) sixel = 0;  /* terminal can't draw Sixel: text mode */
    core_pixel_mode = sixel;
    core_init(argc, argv, 0);
#ifdef _WIN32
    core_stall_guard();
#endif
    term_start();
    term_title("Kollapse");

    DWORD last = GetTickCount();
    /* Sixel: one TW x TH image, UI in ix,iy,iw,ih, the rest black */
    int lastW = 0, lastH = 0, refit = 1, keep = 0, TW = SCREEN_W, TH = SCREEN_H, ix = 0, iy = 0, iw = SCREEN_W, ih = SCREEN_H;
    for (int quit = 0; !quit;) {
        term_size(&W, &H);
        if (W > MAXW) W = MAXW;
        if (H > MAXH) H = MAXH;
        if (keep != core_pixel_fit()) { keep = core_pixel_fit(); refit = 1; }
        if (sixel && (W != lastW || H != lastH || refit)) {
            lastW = W; lastH = H; refit = 0;
            query_cell_px();
            if (!cell_known && area_w) { cell_w = area_w / W; cell_h = area_h / H; }   /* only the pixel size came back */
            TW = W * cell_w; TH = H * cell_h;              /* exactly the cell grid */
            int capped = TW > sx_maxw || TH > sx_maxh;     /* bigger than the terminal will draw (xterm fullscreen) */
            sx_col = sx_row = 0;
            if (TW > sx_maxw) { sx_col = (TW - sx_maxw) / 2 / cell_w; TW = sx_maxw; }   /* largest allowed, centred on cells */
            if (TH > sx_maxh) { sx_row = (TH - sx_maxh) / 2 / cell_h; TH = sx_maxh; }
            if (capped && TH > (H - 1 - sx_row) * cell_h) TH = (H - 1 - sx_row) * cell_h;   /* drawn at the cursor, an image
                                                              touching the last line would scroll the screen */
            if (keep || capped) {                          /* 1 = keep the 632:400 shape, 2 = crisp (whole-number scale) */
                double s = (double)TW / SCREEN_W < (double)TH / SCREEN_H ? (double)TW / SCREEN_W : (double)TH / SCREEN_H;
                if (keep == 2 && s >= 1) s = (int)s;      /* every UI pixel the same size: no uneven columns */
                iw = (int)(SCREEN_W * s); ih = (int)(SCREEN_H * s); ix = (TW - iw) / 2; iy = (TH - ih) / 2;
            } else { ix = iy = 0; iw = TW; ih = TH; }      /* default: fill the terminal */
            if (!capped) term_write("\x1b[2J", 4);
            else {                                         /* xterm, image smaller than the screen: black margins
                                                              (its own background is white) */
                term_write("\x1b[0;40m\x1b[2J", 11);
                for (int y = 0; y < H; y++) {
                    char row[MAXW + 16]; int n = sprintf(row, "\x1b[%d;1H", y + 1);
                    memset(row + n, ' ', (size_t)W); term_write(row, (size_t)(n + W));
                }
                term_write("\x1b[0m", 4);
            }
        }
        DWORD now = GetTickCount();
        core_update((now - last) / 1000.0);
        last = now;
        char title[160]; static char shown[160];           /* now playing in the tab title */
        core_title(title, sizeof title);
        if (strcmp(title, shown)) { term_title(title); strcpy(shown, title); }
        if (sixel) { fb_render(); sixel_frame(TW, TH, ix, iy, iw, ih); term_write(sx, sxn); }
        else { if (!too_small()) render(); present(); }

        DWORD spent = GetTickCount() - now;                /* ~30 fps: wait only for what's left of 33 ms */
        term_event e;
        for (int wait = spent < 33 ? 33 - (int)spent : 1; term_poll(wait, &e) != TE_NONE; wait = 0) {
            if (e.type == TE_KEY) { if (core_key(e.key)) { quit = 1; break; } }   /* Ctrl+C: copy in the editor, quit elsewhere */
            else if (!sixel) { if (e.type == TE_CLICK) click(e.x, e.y); else rclick(e.x, e.y); }
            else if (iw > 0 && ih > 0) {                   /* cell centre -> framebuffer pixel */
                int px = ((e.x - sx_col) * cell_w + cell_w / 2 - ix) * SCREEN_W / iw, py = ((e.y - sx_row) * cell_h + cell_h / 2 - iy) * SCREEN_H / ih;
                if (e.type == TE_CLICK) fb_click(px, py); else fb_click_solo(px, py);
            }
        }
    }
    term_close();
    return 0;
}
