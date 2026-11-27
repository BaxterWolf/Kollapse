/* fb.c - the 632x400 pixel renderer. Used by kollapsew.exe (window) and kollapse.exe --sixel. */
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat.h"
#include "core.h"
#include "fb.h"
#include "font8x8_basic.h"

#define CH_UP    '\x18'
#define CH_DOWN  '\x19'
#define CH_RIGHT '\x1a'
#define CH_LEFT  '\x1b'

uint32_t fb[SCREEN_W * SCREEN_H];
int fb_pressed = -1;

/* ---------- primitives ---------- */
static void fill(int x, int y, int w, int h, uint32_t c)
{
    for (int j = y < 0 ? 0 : y; j < y + h && j < SCREEN_H; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < SCREEN_W; i++) fb[j * SCREEN_W + i] = c;
}
static void px(int x, int y, uint32_t c) { if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) fb[y * SCREEN_W + x] = c; }
static void hline(int x, int y, int w, uint32_t c) { fill(x, y, w, 1, c); }
static void vline(int x, int y, int h, uint32_t c) { fill(x, y, 1, h, c); }
static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t r = 0;
    for (int s = 0; s < 24; s += 8) r |= (uint32_t)(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t) << s;
    return r;
}
static void raised(int x, int y, int w, int h)
{
    hline(x, y, w, C_LIGHT); vline(x, y + 1, h - 1, C_LIGHT);
    hline(x, y + h, w, C_DARK); vline(x + w, y, h + 1, C_DARK);
    fill(x + 1, y + 1, w - 1, h - 1, C_DESK);
}
static void sunken(int x, int y, int w, int h)
{
    hline(x, y, w + 1, C_DARK); vline(x, y + 1, h, C_DARK);
    hline(x + 1, y + h, w, C_LIGHT); vline(x + w, y + 1, h - 1, C_LIGHT);
    fill(x + 1, y + 1, w - 1, h - 1, C_BLACK);
}

/* ---------- font8x8: proportional (trimmed columns) or mono ---------- */
/* the font is ASCII only: Latin-1 / Windows-1252 accented letters (file names, song titles) draw as their plain letter */
static int glyph_ix(int ch)
{
    static const char latin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    ch &= 255;
    return ch >= 0xC0 ? latin[ch - 0xC0] : ch & 127;
}
static void glyph_span(int ch, int *lo, int *hi)
{
    int m = 0;
    for (int r = 0; r < 8; r++) m |= (uint8_t)font8x8_basic[glyph_ix(ch)][r];
    *lo = 8; *hi = -1;
    for (int i = 0; i < 8; i++) if (m >> i & 1) { if (i < *lo) *lo = i; *hi = i; }
}
static void draw_glyph(int x, int y, int ch, uint32_t c, int lo)
{
    if (ch >= CH_UP && ch <= CH_LEFT) {
        for (int k = 0; k < 4; k++)
            switch (ch) {
            case CH_UP:    hline(x + 3 - k, y + 2 + k, 1 + 2 * k, c); break;
            case CH_DOWN:  hline(x + 3 - k, y + 5 - k, 1 + 2 * k, c); break;
            case CH_LEFT:  vline(x + 2 + k, y + 4 - k, 1 + 2 * k, c); break;
            case CH_RIGHT: vline(x + 5 - k, y + 4 - k, 1 + 2 * k, c); break;
            }
        return;
    }
    for (int r = 0; r < 8; r++) {
        uint8_t bits = font8x8_basic[glyph_ix(ch)][r];
        for (int i = 0; i < 8; i++) if (bits >> i & 1) px(x + i - lo, y + r, c);
    }
}
static int char_w(int ch)
{
    int lo, hi;
    if (ch == ' ') return 4;
    if (ch >= CH_UP && ch <= CH_LEFT) return 8;
    glyph_span(ch, &lo, &hi);
    return hi < 0 ? 4 : hi - lo + 2;
}
static int text_w(const char *s) { int w = 0; while (*s) w += char_w((unsigned char)*s++); return w ? w - 1 : 0; }
static void text_clip(int x, int y, const char *s, uint32_t c, int maxx)
{
    for (; *s; s++) {
        int ch = (unsigned char)*s, lo, hi, cw = char_w(ch);
        if (x + cw - 1 > maxx) break;
        glyph_span(ch, &lo, &hi);
        if (ch >= CH_UP && ch <= CH_LEFT) lo = 0;
        if (ch != ' ') draw_glyph(x, y, ch, c, lo);
        x += cw;
    }
}
static void text(int x, int y, const char *s, uint32_t c) { text_clip(x, y, s, c, SCREEN_W); }
static void text_sh(int x, int y, const char *s, uint32_t c, int maxx) { text_clip(x + 1, y + 1, s, C_SHADOW, maxx + 1); text_clip(x, y, s, c, maxx); }
static void mono(int x, int y, const char *s, int n, uint32_t c) { for (int i = 0; i < n && s[i]; i++) if (s[i] != ' ') draw_glyph(x + i * 8, y, s[i], c, 0); }

/* ---------- widgets ---------- */
static void draw_button(const widget_t *w, int down)
{
    if (down) { sunken(w->x, w->y, w->w - 1, w->h - 1); fill(w->x + 1, w->y + 1, w->w - 2, w->h - 2, C_DESK); }
    else raised(w->x, w->y, w->w - 1, w->h - 1);
    text_sh(w->x + (w->w - text_w(w->text)) / 2 + down, w->y + (w->h - 8) / 2 + down, w->text, C_TEXT, w->x + w->w);
}

static void draw_list(const widget_t *w)
{
    int rows = (w->h - 2) / 11;
    for (int i = 0; i < rows; i++) {
        int num, flags; float g; char nb[8];
        const char *name = core_list(w->id, i, rows, &num, &flags, &g);
        if (!name) break;
        int y = w->y + 2 + i * 11, x = w->x + 3;
        if (flags & 1) fill(w->x, y - 1, w->w - 1, 10, C_SEL);
        else if (g > 0.02f) fill(w->x, y - 1, w->w - 1, 10, mix(C_BLACK, mix(C_BLACK, C_SCOPE, 0.35f), g));
        if (w->id == L_FILES) {
            if (flags & 2) draw_glyph(x, y, CH_RIGHT, C_SCOPE, 0);
            text_clip(x + 9, y, name, flags & 2 ? C_SCOPE : flags & 8 ? C_FX : flags & 4 ? C_INS : C_TEXT, w->x + w->w - 3);
        } else {
            sprintf(nb, "%02d", num % 100);
            text(x, y, nb, mix(C_LABEL, C_SCOPE, g));
            text_clip(x + 18, y, name, mix(C_TEXT, 0xFFFFFF, g), w->x + w->w - 3);
        }
    }
}

static void draw_logo(const widget_t *w)
{
    unsigned bars[8], ink[5];
    core_banner(bars, ink);
    raised(w->x, w->y, w->w - 1, w->h - 1);
    int off = (int)(GetTickCount() / 60);
    for (int y = 2; y < w->h - 2; y++) hline(w->x + 2, w->y + y, w->w - 4, bars[((y + off) / 3) % 8]);
    const char *t = "KOLLAPSE";
    int n = (int)strlen(t), sc = 3, sx = w->x + (w->w - n * 8 * sc) / 2, sy = w->y + (w->h - 7 * sc) / 2;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < n; k++)
            for (int r = 0; r < 8; r++) {
                uint8_t bits = font8x8_basic[(int)t[k]][r];
                for (int i = 0; i < 8; i++)
                    if (bits >> i & 1)
                        fill(sx + k * 8 * sc + i * sc + (pass ? 0 : 2), sy + r * sc + (pass ? 0 : 2), sc, sc,
                             pass ? (r < 4 ? ink[0] : ink[3]) : C_DARK);
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

int fb_hit(int mx, int my)               /* layout index of the button or list under (mx,my), -1 none */
{
    for (int i = layout_count - 1; i >= 0; i--) {
        const widget_t *w = &layout[i];
        if ((w->type == W_BUTTON || w->type == W_LIST) && mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) return i;
    }
    return -1;
}

void fb_click(int x, int y)              /* a whole click: scope/pan bar mute, list row, or button */
{
    if (fb_click_mute(x, y)) return;
    int i = fb_hit(x, y);
    if (i < 0) return;
    if (layout[i].type == W_LIST) core_click_list(layout[i].id, (y - layout[i].y - 1) / 11, (layout[i].h - 2) / 11);
    else core_action(layout[i].id);
}

int fb_click_mute(int x, int y)       /* framebuffer coords; 1 if it hit a scope or pan bar */
{
    for (int h = 0; h < nchit; h++)
        if (x >= chit[h].x0 && x < chit[h].x1 && y >= chit[h].y0 && y < chit[h].y1) { core_toggle_mute(chit[h].ch); return 1; }
    return 0;
}

void fb_click_solo(int x, int y)      /* right click on a scope or pan bar: solo that channel */
{
    for (int h = 0; h < nchit; h++)
        if (x >= chit[h].x0 && x < chit[h].x1 && y >= chit[h].y0 && y < chit[h].y1) { core_solo(chit[h].ch); return; }
}

static void draw_scopes(void)
{
    int n = core_nchan();
    if (n == 0) n = 8;
    if (n > 32) n = 32;
    int per = (n + 1) / 2, len = (285 - (per - 1) * 3) / per;
    for (int c = 0; c < n; c++) {
        int x0 = 3 + (c % per) * (len + 3), y0 = c < per ? 95 : 134, mid = y0 + 17, prev = mid;
        int pan, m; float vol;
        core_chan(c, &pan, &vol, &m);
        fill(x0, y0, len, 36, C_BLACK);
        for (int i = 0; i < len; i++) {
            float v = core_scope(c, (float)i / len);
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            int y = mid - (int)(v * 16), a = prev < y ? prev : y, b = prev < y ? y : prev;
            vline(x0 + i, a + 1, b - a + 1, m ? 0x6A1E1E : mix(C_BLACK, C_SCOPE, 0.35f));   /* shading follows the line: dark red when muted */
            vline(x0 + i, a, b - a + 1, m ? C_MUTE : C_SCOPE);
            prev = y;
        }
        if (len >= 16) { char nb[4]; sprintf(nb, "%d", c + 1); text(x0 + 2, y0 + 2, nb, m ? C_MUTE : C_LABEL); }
        add_chit(x0, y0, x0 + len, y0 + 36, c);
    }
}

/* pattern view: pan strip with a cube per channel, then rows scrolling past a fixed middle row */
static void draw_pattern(void)
{
    const int top = TOP_H + 1, h = HINT_Y - top - 1, nrows = 25, midk = 12, ry = PAT_Y + 1;
    fill(0, TOP_H, SCREEN_W, HINT_Y - TOP_H, C_DESK);
    sunken(2, top, 25, h); sunken(604, top, 26, h);
    int vis, wi = core_pick_width(576, 8, 8, &vis), cw = cell_widths[wi];
    int colw = 576 / (vis ? vis : 1), off = core_chan_off(vis), n = core_nchan(), rows = core_rows(), row = core_row();
    for (int v = 0; v < vis; v++) sunken(28 + v * colw, top, colw - 1, h);
    fill(3, ry + midk * 8 - 1, 626, 9, C_DESK);

    for (int v = 0; v < vis && off + v < n; v++) {
        int ch = off + v, x = 28 + v * colw, pan, m; float vol; char nb[4];
        core_chan(ch, &pan, &vol, &m);
        sprintf(nb, "%02d", (ch + 1) % 100);
        add_chit(x, top, x + colw - 1, PAT_Y, ch);
        int narrow = colw < 50, tx0 = narrow ? x + 3 : x + 20, tw = narrow ? colw - 7 : colw - 28;   /* pan track */
        if (!narrow) text(x + 3, top + 3, nb, m ? C_MUTE : C_LABEL);
        if (tw > 8) {
            hline(tx0, top + 6, tw, C_DIM); vline(tx0 + tw / 2, top + 4, 5, C_DIM);
            int cx = tx0 + pan * (tw - 6) / 255;
            fill(cx, top + 3, 6, 6, m ? C_MUTE : mix(mix(C_BLACK, C_SCOPE, 0.25f), C_SCOPE, vol * 1.5f));
            if (vol > 0.6f && !m) fill(cx + 1, top + 4, 2, 2, 0xFFFFFF);
        }
        int vx = narrow ? x + 3 : tx0, vmax = narrow ? colw - 6 : tw;    /* VU under the pan track, clear of the number */
        int vw = (int)(vmax * core_level(ch));                 /* green -> yellow -> red along the bar */
        for (int i = 0; i < vw; i++) vline(vx + i, PAT_Y - 3, 2, i > vmax * 0.85f ? 0xFF5050 : i > vmax * 0.6f ? 0xF0E060 : C_SCOPE);
        hline(x + 1, PAT_Y, colw - 2, C_DESK);
    }
    hline(3, PAT_Y, 24, C_DESK); hline(605, PAT_Y, 25, C_DESK);

    if (!rows) { text(240, ry + midk * 8, "pick a track, press Enter", C_TEXT); return; }
    for (int k = -midk; k < nrows - midk; k++) {
        int r = row + k, y = ry + (k + midk) * 8;
        if (r < 0 || r >= rows) continue;
        uint32_t bg = k == 0 ? 0 : r % 16 == 0 ? C_BARBG : r % 4 == 0 ? C_BEATBG : 0;
        char rn[12]; sprintf(rn, "%02X", r & 0xff);
        if (bg) { fill(3, y - 1, 24, 8, bg); fill(605, y - 1, 25, 8, bg); }
        mono(7, y, rn, 2, k ? C_LABEL : C_TEXT); mono(609, y, rn, 2, k ? C_LABEL : C_TEXT);
        for (int v = 0; v < vis && off + v < n; v++) {
            int ch = off + v, x = 28 + v * colw, pan, m; float vol;
            core_chan(ch, &pan, &vol, &m);
            char t[14]; core_cell(r, ch, t);
            if (bg) fill(x + 1, y - 1, colw - 2, 8, bg);
            static const int fs[4] = { 0, 4, 7, 10 }, fl[4] = { 3, 2, 2, 3 };
            const uint32_t fc[4] = { C_NOTE, C_INS, C_VOL, C_FX };
            int tx = x + (colw - cw * 8) / 2, crow, cch, coff, clen;
            for (int f = 0; f < 4 && fs[f] < cw; f++) {
                int empty = t[fs[f]] == '.' || (f == 0 && t[0] == '-');
                mono(tx + fs[f] * 8, y, t + fs[f], fl[f], m ? C_DIM : empty ? (k ? C_DIM : 0x8090A8) : fc[f]);
            }
            if (core_cursor(&crow, &cch, &coff, &clen) && r == crow && ch == cch && coff < cw) {   /* editor cursor */
                fill(tx + coff * 8 - 1, y - 1, clen * 8 + 1, 9, C_CURSOR);
                mono(tx + coff * 8, y, t + coff, clen, C_BLACK);
            }
        }
    }
}

/* ---------- lower-pane pages: message, sample, instrument, spectrum ---------- */
#define PANE_Y (TOP_H + 1)
#define PANE_H (HINT_Y - TOP_H - 2)

static void line(int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

/* text lines at the top of the pane; returns the y below them */
static int pane_text(int rows_max)
{
    int n, first;
    const char **l = core_text(rows_max, &n, &first);
    int y = PANE_Y + 4;
    for (int i = first; i < n && i - first < rows_max; i++, y += 9) {
        uint32_t c = !strncmp(l[i], "--", 2) ? C_FX : i == 0 ? C_TEXT : C_NOTE;
        if (i == core_text_sel()) fill(5, y - 1, SCREEN_W - 12, 9, C_SEL);   /* selected line (song page) */
        text_clip(8, y, l[i], c, SCREEN_W - 8);
    }
    return y + 2;
}

static void page_message(void)
{
    raised(0, PANE_Y - 1, SCREEN_W - 1, PANE_H + 1);
    sunken(3, PANE_Y + 1, SCREEN_W - 7, PANE_H - 3);
    pane_text((PANE_H - 8) / 9);
}

static void page_sample(void)
{
    raised(0, PANE_Y - 1, SCREEN_W - 1, PANE_H + 1);
    int y0 = pane_text(11), h = PANE_Y + PANE_H - y0 - 4, w = SCREEN_W - 10, mid = y0 + h / 2;
    sunken(4, y0, w + 1, h);
    float ls, le, pos, lo, hi;
    core_wave_marks(&ls, &le, &pos);
    if (ls >= 0) fill(5 + (int)(ls * w), y0 + 1, (int)((le - ls) * w), h - 1, 0x10202C);      /* loop region */
    float ra, rb; core_wave_range(&ra, &rb);                /* selected range */
    if (ra >= 0) fill(5 + (int)(ra * w), y0 + 1, (int)((rb - ra) * w) + 1, h - 1, 0x34284C);
    hline(5, mid, w, C_DIM);
    for (int i = 0; i < w; i++)
        if (core_wave(i, w, &lo, &hi)) vline(5 + i, mid - (int)(hi * (h / 2 - 2)), (int)((hi - lo) * (h / 2 - 2)) + 1, C_SCOPE);
    if (ls >= 0) { vline(5 + (int)(ls * w), y0 + 1, h - 1, C_FX); vline(5 + (int)(le * w), y0 + 1, h - 1, C_FX); }
    if (pos >= 0 && pos <= 1) vline(5 + (int)(pos * w), y0 + 1, h - 1, C_CURSOR);
}

static void env_box(int x, int y, int w, int h, int which)
{
    float xs[32], ys[32];
    int sus, lps, lpe, on, n = core_env(which, xs, ys, 32, &sus, &lps, &lpe, &on);
    sunken(x, y, w, h);
    text(x + 4, y + 3, which ? "Panning envelope" : "Volume envelope", core_env_sel(which) >= 0 ? C_FX : on ? C_TEXT : C_DIM);
    if (!n) { text(x + 4, y + 14, "(none)", C_DIM); return; }
    int gx = x + 6, gy = y + 14, gw = w - 12, gh = h - 20;
    if (which) hline(gx, gy + gh / 2, gw, C_DIM);         /* pan centre */
    if (lps >= 0 && lps < n && lpe < n) fill(gx + (int)(xs[lps] * gw), gy, (int)((xs[lpe] - xs[lps]) * gw) + 1, gh, 0x10202C);
    uint32_t c = on ? C_SCOPE : C_DIM;
    for (int i = 0; i < n; i++) {
        int px0 = gx + (int)(xs[i] * gw), py0 = gy + gh - (int)(ys[i] * gh);
        if (i) line(gx + (int)(xs[i - 1] * gw), gy + gh - (int)(ys[i - 1] * gh), px0, py0, c);
        int sp = core_env_sel(which);
        fill(px0 - (i == sp ? 2 : 1), py0 - (i == sp ? 2 : 1), i == sp ? 5 : 3, i == sp ? 5 : 3, i == sp ? C_FX : i == sus ? C_CURSOR : C_NOTE);
    }
}

static void page_message(void);
static void page_instr(void)
{
    if (core_instr_view() >= 2) { page_message(); return; }   /* keymap / vibrato: text */
    raised(0, PANE_Y - 1, SCREEN_W - 1, PANE_H + 1);
    int y0 = pane_text(5), h = PANE_Y + PANE_H - y0 - 4, w = (SCREEN_W - 14) / 2;
    env_box(4, y0, w, h, 0);
    env_box(8 + w, y0, w, h, 1);
}

static void page_spectrum(void)
{
    float bars[64], peaks[64];
    core_spectrum(bars, peaks, 64);
    raised(0, PANE_Y - 1, SCREEN_W - 1, PANE_H + 1);
    sunken(3, PANE_Y + 1, SCREEN_W - 7, PANE_H - 3);
    int x0 = 8, bw = (SCREEN_W - 16) / 64, base = PANE_Y + PANE_H - 6, h = PANE_H - 14;
    for (int i = 0; i < 64; i++) {
        int bh = (int)(bars[i] * h), x = x0 + i * bw;
        for (int y = 0; y < bh; y++) hline(x, base - y, bw - 2, y > h * 0.8f ? 0xFF5050 : y > h * 0.55f ? 0xF0E060 : C_SCOPE);
        hline(x, base - (int)(peaks[i] * h) - 1, bw - 2, 0xFFFFFF);
    }
}

static void draw_overlay(void)
{
    int n, sel; const char *title;
    const char **lines = core_overlay(&n, &sel, &title);
    if (!lines) return;
    int w = 0;
    for (int i = 0; i < n; i++) if ((int)strlen(lines[i]) * 8 > w) w = (int)strlen(lines[i]) * 8;
    if (text_w(title) + 16 > w) w = text_w(title) + 16;   /* the title fits too */
    w += 24;
    if (w > SCREEN_W - 8) w = SCREEN_W - 8;
    int h = n * 10 + 28, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    fill(x + 4, y + 4, w, h, C_SHADOW);
    raised(x, y, w, h);
    text_sh(x + (w - text_w(title)) / 2, y + 4, title, C_TEXT, x + w);
    sunken(x + 6, y + 15, w - 12, h - 21);
    for (int i = 0; i < n; i++) {
        int ly = y + 19 + i * 10;
        if (i == sel) fill(x + 7, ly - 1, w - 13, 10, C_SEL);
        uint32_t c = lines[i][0] == '-' ? C_FX : lines[i][0] == ' ' ? C_DIM : C_TEXT;
        mono(x + 12, ly, lines[i], 64, c);
    }
}

void fb_render(void)
{
    nchit = 0;
    fill(0, 0, SCREEN_W, SCREEN_H, C_DESK);
    for (int i = 0; i < layout_count; i++) {
        const widget_t *w = &layout[i];
        char buf[256];
        switch (w->type) {
        case W_PANEL:  raised(w->x, w->y, w->w, w->h); break;
        case W_SUNK:   sunken(w->x, w->y, w->w, w->h); break;
        case W_BG:     fill(w->x, w->y, w->w, w->h, C_BLACK); break;
        case W_BUTTON: draw_button(w, i == fb_pressed); break;
        case W_LABEL:  text_sh(w->x, w->y, w->text ? w->text : core_field(w->id, buf), C_TEXT, w->x + w->w); break;
        case W_FIELD:  sunken(w->x, w->y, w->w - 1, w->h - 1);
                       text_clip(w->x + 3, w->y + (w->h - 8) / 2 + 1, core_field(w->id, buf), C_TEXT, w->x + w->w - 3); break;
        case W_LIST:   draw_list(w); break;
        case W_LOGO:   draw_logo(w); break;
        case W_SCOPES: draw_scopes(); break;
        }
    }
    fill(2, 89, (int)(287 * core_progress()), 2, C_SCOPE);   /* song progress under the status text */
    switch (core_page()) {
    case P_MESSAGE: case P_SONG: case P_CONFIG: page_message(); break;
    case P_SAMPLE:   page_sample(); break;
    case P_INSTR:    page_instr(); break;
    case P_SPECTRUM: page_spectrum(); break;
    default:         draw_pattern();
    }
    draw_overlay();
}

/* Settings > Pixel scaling for the window versions: 0 fill the window, 1 keep the 632:400 shape, 2 crisp (whole-number scale) */
void fb_fit(int cw, int ch, int *ox, int *oy, int *w, int *h)
{
    int mode = core_pixel_fit(), s = cw / SCREEN_W < ch / SCREEN_H ? cw / SCREEN_W : ch / SCREEN_H;
    if (mode == 2 && s >= 1) { *w = SCREEN_W * s; *h = SCREEN_H * s; }
    else if (mode) {
        if ((long)cw * SCREEN_H > (long)ch * SCREEN_W) { *h = ch; *w = ch * SCREEN_W / SCREEN_H; }
        else { *w = cw; *h = cw * SCREEN_H / SCREEN_W; }
    } else { *w = cw; *h = ch; }
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
    *ox = (cw - *w) / 2; *oy = (ch - *h) / 2;
}

int fb_dump_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    uint32_t sz = 54 + SCREEN_W * SCREEN_H * 3;
    uint8_t h[54] = { 'B', 'M' };
    memcpy(h + 2, &sz, 4); h[10] = 54; h[14] = 40;
    int32_t w = SCREEN_W, hh = SCREEN_H; memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (int y = SCREEN_H - 1; y >= 0; y--)
        for (int x = 0; x < SCREEN_W; x++) { uint32_t c = fb[y * SCREEN_W + x]; fputc(c, f); fputc(c >> 8, f); fputc(c >> 16, f); }
    fclose(f);
    return 0;
}
