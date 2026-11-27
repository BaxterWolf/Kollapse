/* core.c - player core: folder scan, playback, layout table, input. Shared by both front-ends. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include "compat.h"
#ifndef _WIN32
#include "mpris.h"
#endif
#ifdef _WIN32
#include <mmsystem.h>
#else
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"
#endif
#include "xmp.h"
#include "miniz.h"
#include "core.h"
#include "xmsave.h"
#include "modland.h"

/* ---------------- layout ---------------- */
const widget_t layout[] = {
    /* logo + song info */
    { W_LOGO,    0,   0, 291, 30 },
    { W_PANEL,   0,  30, 291, 47 },
    { W_LABEL,   4,  34,  32,  8, "Song" },
    { W_FIELD,  40,  32, 247, 11, 0, F_TITLE },
    { W_LABEL,   4,  45,  32,  8, "Type" },
    { W_FIELD,  40,  43, 110, 11, 0, F_TYPE },
    { W_LABEL, 156,  45,  24,  8, "Chn" },
    { W_FIELD, 182,  43,  24, 11, 0, F_CHANS },
    { W_LABEL, 211,  45,  24,  8, "Vol" },
    { W_FIELD, 237,  43,  50, 11, 0, F_VOL },
    { W_LABEL,   4,  56,  32,  8, "BPM" },
    { W_FIELD,  40,  54,  30, 11, 0, F_BPM },
    { W_LABEL,  76,  56,  24,  8, "Spd" },
    { W_FIELD, 102,  54,  24, 11, 0, F_SPEED },
    { W_LABEL, 132,  56,  24,  8, "Ord" },
    { W_FIELD, 158,  54,  48, 11, 0, F_ORD },
    { W_LABEL, 211,  56,  24,  8, "Ptn" },
    { W_FIELD, 237,  54,  50, 11, 0, F_PTN },
    { W_LABEL,   4,  67,  32,  8, "Row" },
    { W_FIELD,  40,  65,  30, 11, 0, F_ROW },
    { W_LABEL,  76,  67,  30,  8, "Time" },
    { W_FIELD, 110,  65,  96, 11, 0, F_TIME },
    { W_BUTTON,211,  64,  77, 12, "Menu  Esc", A_MENU },

    /* status bar */
    { W_PANEL,   0,  77, 291, 15 },
    { W_LABEL,   4,  80, 186,  8, 0, F_STATUS },
    { W_LABEL, 196,  80,  91,  8, 0, F_MODE },

    /* scopes */
    { W_PANEL,   0,  92, 291, 81 },
    { W_SCOPES,  3,  95, 285, 75 },

    /* files */
    { W_PANEL, 291,   0, 170, 173 },
    { W_LABEL, 295,   3, 160,  8, 0, F_FILES_HDR },
    { W_SUNK,  293,  13, 165, 158 },
    { W_LIST,  294,  14, 164, 157, 0, L_FILES },

    /* instruments */
    { W_PANEL, 461,   0, 171, 173 },
    { W_LABEL, 465,   3, 160,  8, 0, F_INSTR_HDR },
    { W_SUNK,  463,  13, 166, 158 },
    { W_LIST,  464,  14, 165, 157, 0, L_INSTR },

    /* key hints */
    { W_PANEL,   0, HINT_Y, 632, 12 },
    { W_LABEL,   4, HINT_Y + 2, 624, 8, 0, F_HINTS },
};
const int layout_count = sizeof layout / sizeof layout[0];

/* ---------------- state ---------------- */
#define MAXFILES 1024
#define RATE     44100
#define NBUF     4
#define FRAMES   1024

static int    fsel, fscroll, iscroll, ended_auto;
static char   dirname[MAX_PATH], status[128];

static xmp_context ctx;
static struct xmp_module_info mi;
static int    loaded, s3m_style;    /* s3m_style: show effects as S3M/IT letters */
static volatile int state;            /* 0 stopped, 1 playing, 2 paused */
static volatile int ended;
/* themes: DESK LIGHT DARK TEXT SHADOW BEATBG BARBG NOTE INS VOL FX DIM SCOPE SEL CURSOR MUTE */
static const struct { const char *name; unsigned c[16]; unsigned logo, bg; } themes[] = {   /* logo: the banner's colour, bg: panels */
    { "Kollapse",  { 0x55657C, 0x9CAAC2, 0x232B38, 0xF2F2F2, 0x1A202A, 0x142030, 0x1A2A40, 0xE6EDF5, 0x7FD4FF, 0x8CF08C, 0xFFB060, 0x3C4A5E, 0x5CFF96, 0x2E5FA8, 0xF0E060, 0xE05050 }, 0x4F6FD0, 0x000000 },
    { "Amiga",     { 0x0055AA, 0x6699DD, 0x002255, 0xFFFFFF, 0x001133, 0x002A55, 0x003870, 0xFFFFFF, 0xFFAA33, 0xAADDFF, 0xFF8800, 0x3366AA, 0xFFFFFF, 0xFF8800, 0xFFDD55, 0xFF3333 }, 0xFF8800, 0x000000 },
    { "Amber",     { 0x5A3A10, 0xA87830, 0x24160A, 0xFFC060, 0x180E04, 0x201408, 0x2C1C0A, 0xFFD080, 0xFFB040, 0xE8A040, 0xFF9020, 0x5C4020, 0xFFB030, 0x8A5A10, 0xFFE0A0, 0xFF5030 }, 0xD88A20, 0x000000 },
    { "Phosphor",  { 0x1E4A2A, 0x4F9A60, 0x0C2012, 0xB8FFC8, 0x08180C, 0x0A1C10, 0x102818, 0xC8FFD4, 0x7CFF9C, 0x5CE07C, 0xA8FF60, 0x2A5A36, 0x40FF70, 0x1F7A3A, 0xE0FFB0, 0xFF5050 }, 0x2CC858, 0x000000 },
    { "Midnight",  { 0x3A2C5C, 0x7A68B0, 0x1A1230, 0xF4EEFF, 0x100A20, 0x1C1238, 0x261848, 0xF0E8FF, 0x5CE1FF, 0xFF7AD9, 0xFFB45C, 0x4A3C70, 0xFF5CC8, 0x6A3CB8, 0x5CFFE0, 0xFF4A6A }, 0xC040D0, 0x000000 },
    { "Paper",     { 0xBDB5A3, 0xE6DFCF, 0x857D6C, 0x1C1A16, 0x6E675A, 0xE2DCCD, 0xD6CEBD, 0x1C1A16, 0x0E5E9E, 0x1F7A30, 0xA4560E, 0xB3AB99, 0x23406E, 0xA9C2E6, 0xB07800, 0xC03030 }, 0x4A6AAA, 0xF2EEE4 },
    { "Mono",      { 0x6A6A6A, 0xB0B0B0, 0x2C2C2C, 0xF0F0F0, 0x1C1C1C, 0x161616, 0x222222, 0xFFFFFF, 0xC8C8C8, 0xA8A8A8, 0xE0E0E0, 0x464646, 0xFFFFFF, 0x5A5A5A, 0xFFFFFF, 0xE05050 }, 0x8C8C8C, 0x000000 },
};
#define NTHEMES (int)(sizeof themes / sizeof *themes)
unsigned core_pal[18];
static int theme, skip_secs = 4, subsongs = 1, interp = 2, stereo = 70, autosave_min = 1, pixel_fit;
int core_pixel_fit(void) { return pixel_fit; }
int core_pixel_mode;
/* the banner gradient (8 bands, dark -> light -> dark) and letter shades (5 rows) in the theme's colour */
static unsigned lerp_rgb(unsigned a, unsigned b, float t)
{
    unsigned r = 0;
    for (int sh = 0; sh < 24; sh += 8) { int x = (a >> sh) & 255, y = (b >> sh) & 255; r |= (unsigned)(x + (y - x) * t + 0.5f) << sh; }
    return r;
}
void core_banner(unsigned bars[8], unsigned ink[5])
{
    unsigned a = themes[theme].logo;
    static const float d[8] = { 0.25f, 0.6f, 1, 0, 0, 0, 1, 0.6f }, l[8] = { 0, 0, 0, 0.6f, 0.9f, 0.6f, 0, 0 };
    for (int i = 0; i < 8; i++) bars[i] = l[i] > 0 ? lerp_rgb(a, 0xFFFFFF, l[i]) : lerp_rgb(0, a, d[i]);
    for (int i = 0; i < 5; i++) ink[i] = lerp_rgb(0xFFFFFF, a, i < 2 ? 0 : (i - 1) * 0.12f);
}
static void apply_theme(void)
{
    if (theme < 0 || theme >= NTHEMES) theme = 0;
    memcpy(core_pal, themes[theme].c, sizeof themes[0].c);
    unsigned bg = themes[theme].bg;
    core_pal[16] = bg;
    int light = ((bg >> 16 & 255) + (bg >> 8 & 255) + (bg & 255)) > 384;   /* labels: the bevel colour on dark panels, */
    core_pal[17] = light ? lerp_rgb(C_TEXT, bg, 0.45f) : C_LIGHT;         /* a soft text colour on light ones */
}
static volatile int fade_left = -1, quiet, loops0 = -1;   /* song endings: fade frames left, silent frames in a row, loop count at start */
static int repeat, modified;                            /* (defined again further down) */
static void play_seq(int k);
static int fade_secs = 8, cur_seq, nseq, seqs[64];      /* subsongs: the ones worth playing (>= 10 s) */
static int    volume = 100, muted[XMP_MAX_CHANNELS];
static struct xmp_frame_info fi;      /* what is audible right now */
static float  glow[256];
static int    overlay, osel, chan_off;
static int    page, is_it;                /* is_it: real IT file (a few effect letters differ from S3M) */
/* defined further down (structural edits, sample picker, song page) */
static int    song_sel, npick, pick_sel;
static char  *name_edit; static int name_max;   /* typing a name (song title, sample, instrument) */
static int    rec = 1;                    /* editor: keys write notes (record) or only play them (jam) */
#define NSET 5                                     /* song page: settings lines before the order list */
static char  *pick[1024];
static void   new_song(void);
static void   autosave_tick(void);
static void   export_tick(void);
static void   autosave_done(void);
static void   autosave_check(void);
static void   export_wav(void);
static int    cfg_key(int k);
static void   cfg_text(int rows, int *first);
static int    is_fav(const char *path);
static void   fav_toggle(void);
static void   fav_open(void);
static const char **ml_overlay(int *n, int *sel, const char **title);
static int    ml_key(int k);
static void   ml_tick(void);
static void   ml_show(void);
static void   autosave_dir(char *out, size_t n);
static void   pick_open(void);
static void   pick_enter(int append);
static int    pick_isdir(int i);
static void   pick_scan(void);
static char   pick_dir[MAX_PATH];
static int    song_key(int k);
static void   song_text(int rows, int *first);
static unsigned char orig_head[80];      /* first bytes of the loaded file: format sniffing, XM flags */

/* editor: cursor row/channel/column (0 note, 1-2 ins digits, 3-4 vol digits, 5 fx, 6-7 fx param), pattern */
static int    erow, ech, ecol, epat, octave = 4, cur_ins, cur_sub, text_scroll;
#define UNDO 8192
static struct { short pat, row, ch; struct xmp_event old; int group; } undo[UNDO];   /* cells by position: survive reloads */
/* whole-song snapshots (XM bytes) taken before structural edits and other non-cell changes */
#define SNAPS 32
static struct { void *data; size_t len; int group; } snaps[SNAPS];
static int    nsnaps, snap_key = -1;
static size_t snap_bytes;
static int    uhead, ucount, ugroup;             /* one group per user action (a paste undoes in one go) */
static int    estep = 1, sel_on, sel_r0, sel_c0, sel_r1, sel_c1;   /* edit step; block = anchor (r0,c0) .. (r1,c1) */
static struct { int rows, chans; struct xmp_event *ev; } clip;      /* copied block */

/* visuals */
static float  level[XMP_MAX_CHANNELS], spec[64], peak[64], peak_hold[64];
static volatile int media_cmd;           /* set by the media-key thread: 1 play/pause 2 next 3 prev 4 stop */
static volatile int loop_pos = -1;       /* order position being looped (Shift+Enter in the editor), -1 none */
static volatile int media_ok = -1;       /* how many of the 4 media keys we got (-1: not yet known) */

/* ---------------- audio: waveOut thread, 4 x 1024 frames (~93 ms) ---------------- */
static CRITICAL_SECTION cs;
static short    abuf[NBUF][FRAMES * 2];
static struct xmp_frame_info snap[NBUF];
static volatile int next_done;        /* index of buffer playing now (oldest queued) */
#ifdef _WIN32
static HWAVEOUT wo;
static WAVEHDR  hdr[NBUF];
static HANDLE   aev;
#endif

/* note preview: one voice mixed over the song, playing or stopped */
static struct { const struct xmp_sample *s; double pos, step; float vol; int frames, on; } pv;

static void mix_preview(short *buf)
{
    const struct xmp_sample *s = pv.s;
    for (int i = 0; i < FRAMES && pv.on; i++) {
        long p = (long)pv.pos;
        if (p >= s->len) {
            if (!(s->flg & XMP_SAMPLE_LOOP) || s->lpe <= s->lps) { pv.on = 0; break; }
            pv.pos = s->lps + fmod(pv.pos - s->lps, s->lpe - s->lps);
            p = (long)pv.pos;
        }
        float v = s->flg & XMP_SAMPLE_16BIT ? ((short *)s->data)[p] / 32768.0f : ((signed char *)s->data)[p] / 128.0f;
        float env = pv.frames < RATE ? 1.0f : 1.0f - (pv.frames - RATE) / (RATE * 0.5f);  /* 1 s, then 0.5 s fade */
        if (env <= 0) { pv.on = 0; break; }
        int o = (int)(v * pv.vol * env * 16000);
        for (int c = 0; c < 2; c++) {
            int x = buf[i * 2 + c] + o;
            buf[i * 2 + c] = (short)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        pv.pos += pv.step;
        pv.frames++;
    }
}

/* song endings: fade over the last fade_secs before the loop point, end at the loop, end early
   after skip_secs of silence. Off in the editor, with repeat-track and with unsaved edits. */
static volatile int fade_total;
static void song_end(short *b, const struct xmp_frame_info *f)
{
    int peak = 0;
    for (int k = 0; k < FRAMES * 2; k++) { int v = b[k] < 0 ? -b[k] : b[k]; if (v > peak) peak = v; }
    quiet = peak < 96 ? quiet + FRAMES : 0;
    if (loops0 < 0) loops0 = f->loop_count;
    if (f->loop_count > loops0) { ended = 1; state = 0; memset(b, 0, FRAMES * 4); return; }   /* came round to the loop point */
    int fade_ms = fade_secs * 1000;
    if (fade_secs > 0 && fade_left < 0 && f->total_time > 2 * fade_ms && f->time >= f->total_time - fade_ms) {
        fade_left = fade_total = (int)((double)(f->total_time - f->time) * RATE / 1000) + FRAMES;   /* ends at the loop */
    }
    int any_muted = 0;                                     /* muting/solo makes the mix quiet on purpose */
    for (int c = 0; c < mi.mod->chn; c++) any_muted |= muted[c];
    if (skip_secs && !any_muted && quiet >= skip_secs * RATE && f->time > 5000) { ended = 1; state = 0; return; }   /* silent tail */
    if (fade_left >= 0) {
        float total = (float)(fade_total > 0 ? fade_total : 1);
        for (int k = 0; k < FRAMES; k++) {
            float g = (fade_left - k) / total;
            if (g < 0) g = 0;
            g *= g;                                        /* sounds more even than a straight line */
            b[k * 2] = (short)(b[k * 2] * g); b[k * 2 + 1] = (short)(b[k * 2 + 1] * g);
        }
        fade_left -= FRAMES;
        if (fade_left <= 0) { ended = 1; state = 0; fade_left = -1; }
    }
}

static void fill(int i)
{
    EnterCriticalSection(&cs);
    if (loaded && state == 1) {
        int r = xmp_play_buffer(ctx, abuf[i], sizeof abuf[i], 0);
        xmp_get_frame_info(ctx, &snap[i]);
        if (loop_pos >= 0 && snap[i].pos != loop_pos) xmp_set_position(ctx, loop_pos);   /* pattern loop: jump back */
        if (r < 0) { ended = 1; state = 0; memset(abuf[i], 0, sizeof abuf[i]); }
        else if (repeat != 1 && loop_pos < 0 && page != P_EDIT && !modified) song_end(abuf[i], &snap[i]);
    } else {
        memset(abuf[i], 0, sizeof abuf[i]);
        snap[i] = snap[(i + NBUF - 1) % NBUF];
    }
    if (pv.on) mix_preview(abuf[i]);
    /* KOLLAPSE_MUTE: silence, same timing. Not the device volume: Windows keeps that per exe */
    static int mute = -1;
    if (mute < 0) mute = getenv("KOLLAPSE_MUTE") != NULL;
    if (mute) memset(abuf[i], 0, sizeof abuf[i]);
    LeaveCriticalSection(&cs);
}

#ifdef _WIN32
/* media keys: thread-level hotkeys, so they work in the console too and while not focused */
static DWORD WINAPI media_thread(LPVOID arg)
{
    static const UINT vk[4] = { 0xB3, 0xB0, 0xB1, 0xB2 };   /* VK_MEDIA_PLAY_PAUSE, NEXT, PREV, STOP */
    int got[4] = { 0 };
    SetTimer(NULL, 0, 2000, NULL);                         /* keys another app (or another Kollapse) held: try again */
    MSG m = { .message = WM_TIMER };
    do {
        if (m.message == WM_HOTKEY) media_cmd = (int)m.wParam;
        else if (m.message == WM_TIMER && media_ok != 4) {
            int ok = 0;
            for (int i = 0; i < 4; i++) ok += got[i] || (got[i] = RegisterHotKey(NULL, i + 1, 0x4000 /* MOD_NOREPEAT */, vk[i]) != 0);
            media_ok = ok;
        }
    } while (GetMessage(&m, NULL, 0, 0) > 0);
    return 0;
}

static DWORD WINAPI audio_thread(LPVOID p)
{
    for (int i = 0; i < NBUF; i++) { fill(i); waveOutWrite(wo, &hdr[i], sizeof hdr[i]); }
    for (int i = 0;; ) {
        WaitForSingleObject(aev, INFINITE);
        while (hdr[i].dwFlags & WHDR_DONE) {
            next_done = (i + 1) % NBUF;
            fill(i);
            waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
            i = (i + 1) % NBUF;
        }
    }
    return 0;
}

static void audio_start(void)
{
    WAVEFORMATEX wf = { 0 };
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = RATE; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4; wf.nAvgBytesPerSec = RATE * 4;
    aev = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)aev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        strcpy(status, "no audio device - visuals only");
        return;
    }
    for (int i = 0; i < NBUF; i++) {
        hdr[i].lpData = (LPSTR)abuf[i]; hdr[i].dwBufferLength = sizeof abuf[i];
        waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]);
    }
    /* full channel volume: Windows keeps it per exe and the mixer does not show it */
    waveOutSetVolume(wo, 0xFFFFFFFF);
    CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}

/* terminal only: a closed console blocks our writes and sends no close event,
   so pause the sound while frames stop */
static volatile DWORD last_frame;
static DWORD WINAPI stall_thread(LPVOID a)
{
    for (int paused = 0;; Sleep(200)) {
        int stalled = GetTickCount() - last_frame > 1000;
        if (stalled != paused) { if (stalled) waveOutPause(wo); else waveOutRestart(wo); paused = stalled; }
    }
    return 0;
}
void core_stall_guard(void) { last_frame = GetTickCount(); if (wo) CreateThread(NULL, 0, stall_thread, NULL, 0, NULL); }
#else
/* Linux: miniaudio pulls from the same NBUF ring; a buffer is refilled once played */
static ma_device dev;
static int ring_cur, ring_off;
static void ma_cb(ma_device *d, void *out, const void *in, ma_uint32 n)
{
    short *o = out;
    (void)d; (void)in;
    while (n) {
        int take = FRAMES - ring_off < (int)n ? FRAMES - ring_off : (int)n;
        memcpy(o, abuf[ring_cur] + ring_off * 2, (size_t)take * 4);
        o += take * 2; n -= take; ring_off += take;
        if (ring_off == FRAMES) {                          /* this one is done: refill it, move on */
            fill(ring_cur);
            ring_cur = (ring_cur + 1) % NBUF; ring_off = 0;
            next_done = ring_cur;
        }
    }
}
static void audio_start(void)
{
    for (int i = 0; i < NBUF; i++) fill(i);
    ma_device_config c = ma_device_config_init(ma_device_type_playback);
    c.playback.format = ma_format_s16; c.playback.channels = 2; c.sampleRate = RATE;
    c.periodSizeInFrames = FRAMES; c.dataCallback = ma_cb;
    if (ma_device_init(NULL, &c, &dev) != MA_SUCCESS || ma_device_start(&dev) != MA_SUCCESS)
        strcpy(status, "no audio device - visuals only");
}
#endif

/* ---------------- browser (current folder) + playlist (folder a track was started from) ---------------- */
#define PATHLEN 1024                           /* "C:\dir\pack.zip|inner/path.xm" can pass MAX_PATH */
typedef struct { char *name; int dir; } entry_t;   /* dir: 0 track, 1 folder, 2 zip archive */
static entry_t ents[MAXFILES];
static int    nents, nmods;                    /* nmods: tracks in this folder */
static char  *pl[MAXFILES];                    /* playlist: full paths ("zip|inner" for tracks inside zips) */
static int    npl, order[MAXFILES], opos;      /* play order (shuffled or 0..n-1) and position in it */
static int    shuffle, repeat;                 /* repeat: 0 off, 1 track, 2 list */
static char   playing[PATHLEN];                /* full path of the loaded track */
static char   zipview[MAX_PATH];               /* zip being browsed, "" when browsing a real folder */

static int has_ext(const char *n, const char *list)   /* list like ".mod.xm." */
{
    const char *e = strrchr(n, '.');
    char k[8];
    if (!e || strlen(e) > 5) return 0;
    snprintf(k, sizeof k, "%s.", e);
    for (char *c = k; *c; c++) *c = (char)tolower((unsigned char)*c);
    return strstr(list, k) != NULL;
}
static int is_zipmod(const char *n) { return has_ext(n, ".mdz.xmz.s3z.itz."); }   /* one zipped module */
#define MOD_EXTS ".mod.xm.s3m.it.stm.669.far.mtm.ptm.ult.mdl.okt.okta.mmd0.mmd1.mmd2.mmd3.med.digi.dbm.dsym.dtm.psm.gdm.amf.musx.gmc.coco.sfx.fnk.imf.liq.rtm."
static int is_module(const char *n)
{
    char pre[8]; const char *dot = strchr(n, '.');          /* Amiga style: "mod.title", "med.title" */
    if (dot && dot > n && dot - n < 6) {
        snprintf(pre, sizeof pre, ".%.*s.", (int)(dot - n), n);
        for (char *c = pre; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (strstr(MOD_EXTS, pre)) return 1;
    }
    return has_ext(n, MOD_EXTS) || is_zipmod(n);
}
static int is_zip(const char *n)    { return has_ext(n, ".zip."); }
static int is_m3u(const char *n)    { return has_ext(n, ".m3u.m3u8."); }
static const char *base(const char *p)
{
    const char *b = p;
    for (const char *c = p; *c; c++) if (*c == '\\' || *c == '/' || *c == '|') b = c + 1;
    return b;
}

/* ---------------- zip: read the archive into memory, inflate one member with miniz ---------------- */
static void *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = len > 0 && len < (256 << 20) ? malloc(len + 1) : NULL;   /* 256 MB cap */
    if (buf && fread(buf, 1, len, f) != (size_t)len) { free(buf); buf = NULL; }
    if (buf) buf[len] = 0;                                 /* playlists and favorites are read as text (strtok) */
    fclose(f);
    *n = len;
    return buf;
}
static int zip_open(const char *path, mz_zip_archive *z, void **buf)
{
    size_t n;
    memset(z, 0, sizeof *z);
    if (!(*buf = read_file(path, &n))) return 0;
    if (mz_zip_reader_init_mem(z, *buf, n, 0)) return 1;
    free(*buf);
    return 0;
}
static void zip_close(mz_zip_archive *z, void *buf) { mz_zip_reader_end(z); free(buf); }

/* inflate `inner` from the zip (NULL: first module, else first file - for .mdz). name gets the member's name */
static void *zip_extract(const char *zpath, const char *inner, size_t *n, char *name, int namelen)
{
    mz_zip_archive z; void *zbuf, *out = NULL;
    if (!zip_open(zpath, &z, &zbuf)) return NULL;
    int pick = -1, nf = (int)mz_zip_reader_get_num_files(&z);
    for (int pass = 0; pass < 2 && pick < 0; pass++)
        for (int i = 0; i < nf && pick < 0; i++) {
            char fn[PATHLEN];
            if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
            mz_zip_reader_get_filename(&z, i, fn, sizeof fn);
            if (inner ? !strcmp(fn, inner) : pass == 1 || is_module(fn)) { pick = i; snprintf(name, namelen, "%s", fn); }
        }
    if (pick >= 0) out = mz_zip_reader_extract_to_heap(&z, pick, n, 0);
    zip_close(&z, zbuf);
    return out;
}

static int cmp_ent(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    static const int rank[3] = { 2, 0, 1 };                 /* folders, then zips, then tracks */
    if (x->dir != y->dir) return rank[x->dir] - rank[y->dir];
    if (!strcmp(x->name, "..")) return -1;
    if (!strcmp(y->name, "..")) return 1;
    return _stricmp(x->name, y->name);
}

/* ---------------- settings: %APPDATA%\kollapse.ini, saved on every change ---------------- */
static char ini[MAX_PATH], lastdir[MAX_PATH], dl_dir[MAX_PATH];

static void save_settings(void)
{
    char b[16];
    if (!ini[0]) return;
    sprintf(b, "%d", volume);  WritePrivateProfileStringA("kollapse", "volume", b, ini);
    sprintf(b, "%d", shuffle); WritePrivateProfileStringA("kollapse", "shuffle", b, ini);
    sprintf(b, "%d", repeat);  WritePrivateProfileStringA("kollapse", "repeat", b, ini);
    sprintf(b, "%d", fade_secs);    WritePrivateProfileStringA("kollapse", "fade", b, ini);
    sprintf(b, "%d", skip_secs);    WritePrivateProfileStringA("kollapse", "skip_silence", b, ini);
    sprintf(b, "%d", subsongs);     WritePrivateProfileStringA("kollapse", "subsongs", b, ini);
    sprintf(b, "%d", interp);       WritePrivateProfileStringA("kollapse", "interpolation", b, ini);
    sprintf(b, "%d", stereo);       WritePrivateProfileStringA("kollapse", "stereo", b, ini);
    sprintf(b, "%d", autosave_min); WritePrivateProfileStringA("kollapse", "autosave", b, ini);
    sprintf(b, "%d", pixel_fit);    WritePrivateProfileStringA("kollapse", "pixel_fit", b, ini);
    WritePrivateProfileStringA("kollapse", "theme", themes[theme].name, ini);
    if (dl_dir[0]) WritePrivateProfileStringA("kollapse", "downloads", dl_dir, ini);
    if (dirname[0]) WritePrivateProfileStringA("kollapse", "lastdir", dirname, ini);
}

static void load_settings(void)
{
    char app[MAX_PATH];
    if (!GetEnvironmentVariableA("APPDATA", app, MAX_PATH)) return;
#ifdef _WIN32
    snprintf(ini, sizeof ini, "%s\\kollapse.ini", app);
#else
    snprintf(ini, sizeof ini, "%s/kollapse", app); CreateDirectoryA(ini, NULL);
    snprintf(ini, sizeof ini, "%s/kollapse/kollapse.ini", app);
#endif
    volume  = GetPrivateProfileIntA("kollapse", "volume", 100, ini);
    shuffle = GetPrivateProfileIntA("kollapse", "shuffle", 0, ini) != 0;
    repeat  = GetPrivateProfileIntA("kollapse", "repeat", 0, ini);
    GetPrivateProfileStringA("kollapse", "lastdir", "", lastdir, MAX_PATH, ini);
    GetPrivateProfileStringA("kollapse", "downloads", "", dl_dir, MAX_PATH, ini);
    fade_secs = GetPrivateProfileIntA("kollapse", "fade", -1, ini);
    if (fade_secs < 0 || fade_secs > 60) { fade_secs = 8; WritePrivateProfileStringA("kollapse", "fade", "8", ini); }   /* seconds; 0 = stop at the loop */
    if (!dl_dir[0]) {                                      /* default: Music\Kollapse, written so it can be changed */
        char home[MAX_PATH];
        if (GetEnvironmentVariableA("USERPROFILE", home, MAX_PATH)) snprintf(dl_dir, sizeof dl_dir, "%s" SEPS "Music" SEPS "Kollapse", home);
        WritePrivateProfileStringA("kollapse", "downloads", dl_dir, ini);
    }
    skip_secs    = GetPrivateProfileIntA("kollapse", "skip_silence", 4, ini);
    subsongs     = GetPrivateProfileIntA("kollapse", "subsongs", 1, ini) != 0;
    interp       = GetPrivateProfileIntA("kollapse", "interpolation", XMP_INTERP_SPLINE, ini);
    stereo       = GetPrivateProfileIntA("kollapse", "stereo", 70, ini);
    autosave_min = GetPrivateProfileIntA("kollapse", "autosave", 1, ini);
    pixel_fit    = GetPrivateProfileIntA("kollapse", "pixel_fit", 0, ini) % 3;
    char tn[32];
    GetPrivateProfileStringA("kollapse", "theme", "Kollapse", tn, sizeof tn, ini);
    for (int i = 0; i < NTHEMES; i++) if (!_stricmp(tn, themes[i].name)) theme = i;
    if (volume < 0 || volume > 200) volume = 100;          /* hand-edited file: keep values sane */
    if (repeat < 0 || repeat > 2) repeat = 0;
    if (skip_secs < 0 || skip_secs > 30) skip_secs = 4;
    if (interp < 0 || interp > 2) interp = XMP_INTERP_SPLINE;
    if (stereo < 0 || stereo > 100) stereo = 70;
    if (autosave_min < 0 || autosave_min > 30) autosave_min = 1;
    apply_theme();
}

static void add_ent(const char *name, int dir)
{
    if (nents >= MAXFILES) return;
    ents[nents].name = _strdup(name); ents[nents++].dir = dir; nmods += !dir;
}

static void scan(void)
{
    for (int i = 0; i < nents; i++) free(ents[i].name);
    nents = nmods = fsel = fscroll = 0;
    GetCurrentDirectoryA(MAX_PATH, dirname);
    if (zipview[0] && is_m3u(zipview)) {                  /* a playlist: [..] plus its tracks (full paths) */
        add_ent("..", 1);
        size_t n; char *t = read_file(zipview, &n), dir[MAX_PATH];
        snprintf(dir, sizeof dir, "%s", zipview);
        char *sl = strrchr(dir, SEP); if (sl) sl[1] = 0; else dir[0] = 0;
        for (char *ln = t ? strtok(t, "\r\n") : NULL; ln; ln = strtok(NULL, "\r\n")) {
            while (*ln == ' ' || *ln == '\t') ln++;
            if (!*ln || *ln == '#') continue;
            char full[PATHLEN];                            /* relative entries are relative to the playlist */
            if (is_abs_path(ln)) snprintf(full, sizeof full, "%s", ln);
            else snprintf(full, sizeof full, "%s%s", dir, ln);
            add_ent(full, 0);
        }
        free(t);
        save_settings();
        return;                                            /* keep the playlist's order */
    } else if (zipview[0]) {                              /* inside a zip: [..] plus the modules it holds */
        mz_zip_archive z; void *zbuf;
        add_ent("..", 1);
        if (zip_open(zipview, &z, &zbuf)) {
            int nf = (int)mz_zip_reader_get_num_files(&z);
            for (int i = 0; i < nf; i++) {
                char fn[PATHLEN];
                mz_zip_reader_get_filename(&z, i, fn, sizeof fn);
                if (!mz_zip_reader_is_file_a_directory(&z, i) && is_module(fn) && !is_zipmod(fn)) add_ent(fn, 0);
            }
            zip_close(&z, zbuf);
        } else snprintf(status, sizeof status, "can't read %s", base(zipview));
    } else {
        if (!is_root_path(dirname)) add_ent("..", 1);     /* not at a drive root */
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA("*", &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                int d = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
                if (fd.cFileName[0] == '.' || fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
                if (d) add_ent(fd.cFileName, 1);
                else if (is_zip(fd.cFileName) || is_m3u(fd.cFileName)) add_ent(fd.cFileName, 2);
                else if (is_module(fd.cFileName)) add_ent(fd.cFileName, 0);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    qsort(ents, nents, sizeof *ents, cmp_ent);
    save_settings();                                       /* remembers the folder */
}

/* full path of list entry k: "dir\name", or "pack.zip|inner" inside a zip */
static void entry_path(int k, char *out, int len)
{
    if (zipview[0] && is_m3u(zipview)) snprintf(out, len, "%s", ents[k].name);
    else if (zipview[0]) snprintf(out, len, "%s|%s", zipview, ents[k].name);
    else snprintf(out, len, "%s%s%s", dirname, is_root_path(dirname) ? "" : SEPS, ents[k].name);
}

static void enter_dir(const char *name)
{
    char from[MAX_PATH];
    if (zipview[0] && !strcmp(name, "..")) {              /* leave the zip, land on it in the list */
        strcpy(from, base(zipview));
        zipview[0] = 0;
        scan();
        for (int i = 0; i < nents; i++) if (ents[i].dir == 2 && !_stricmp(ents[i].name, from)) fsel = i;
        return;
    }
    strcpy(from, base(dirname));
    if (!SetCurrentDirectoryA(name)) { snprintf(status, sizeof status, "can't open %s", name); return; }
    scan();
    if (!strcmp(name, ".."))                              /* land on the folder we came out of */
        for (int i = 0; i < nents; i++) if (ents[i].dir && !_stricmp(ents[i].name, from)) fsel = i;
    if (!loaded) snprintf(status, sizeof status, "%d tracks, %d folders", nmods, nents - nmods);
}

static void open_zip(const char *name)
{
    char path[MAX_PATH]; mz_zip_archive z; void *zbuf;
    snprintf(path, sizeof path, "%s%s%s", dirname, is_root_path(dirname) ? "" : SEPS, name);  /* name points into ents[], which scan() frees */
    if (!is_m3u(path)) {
        if (!zip_open(path, &z, &zbuf)) { snprintf(status, sizeof status, "can't read %s", base(path)); return; }
        zip_close(&z, zbuf);
    }
    strcpy(zipview, path);
    scan();
    if (!loaded) snprintf(status, sizeof status, "%d tracks in %s", nmods, base(zipview));
}

static int modified;                      /* the loaded song has unsaved edits */
static void snaps_clear(void);
static int pat_rows(int p);

/* load a module image. keep_view: rebuild of the edited song, keep page and cursor, stay stopped */
static int load_mem(const void *mem, size_t memlen, const char *path, int keep_view)
{
    EnterCriticalSection(&cs);
    if (loaded) { xmp_end_player(ctx); xmp_release_module(ctx); loaded = 0; }
    state = 0; ended = 0; fade_left = -1; quiet = 0; loops0 = -1;
    memset(&fi, 0, sizeof fi);
    memset(snap, 0, sizeof snap);
    /* effect style comes from the file's signature, not its name (plenty of .xm files are really IT) */
    unsigned char head[80] = { 0 };
    memcpy(head, mem, memlen < sizeof head ? memlen : sizeof head);
    int sig_it_s3m = !memcmp(head, "IMPM", 4) || !memcmp(head + 44, "SCRM", 4);
    memcpy(orig_head, head, sizeof head);
    is_it = !memcmp(head, "IMPM", 4);
    pv.on = 0;                                             /* preview voice points into the old module */
    int ok = xmp_load_module_from_memory(ctx, mem, (long)memlen) == 0 && xmp_start_player(ctx, RATE, 0) == 0;
    if (ok) {
        loaded = 1; state = keep_view ? 0 : 1;
        if (path != playing) snprintf(playing, sizeof playing, "%s", path);
        xmp_get_module_info(ctx, &mi);
        s3m_style = sig_it_s3m;
        nseq = 0; cur_seq = 0;                             /* subsongs worth playing */
        for (int q = 0; q < mi.num_sequences && nseq < 64; q++) if (mi.seq_data[q].duration >= 10000) seqs[nseq++] = q;
        if (!nseq) seqs[nseq++] = 0;
        xmp_set_player(ctx, XMP_PLAYER_INTERP, interp);
        xmp_set_player(ctx, XMP_PLAYER_MIX, stereo);
        xmp_set_player(ctx, XMP_PLAYER_VOLUME, volume);
        for (int c = 0; c < XMP_MAX_CHANNELS; c++) xmp_channel_mute(ctx, c, muted[c]);
        if (!keep_view && nseq > 1) snprintf(status, sizeof status, "subsong 1/%d  %s", nseq, base(path));
        else if (!keep_view) snprintf(status, sizeof status, "playing %s", base(path));
    } else {
        playing[0] = 0;
        snprintf(status, sizeof status, "can't load %s", base(path));
    }
    LeaveCriticalSection(&cs);
    if (!keep_view) { uhead = ucount = 0; snaps_clear(); loop_pos = -1; }   /* a different song: fresh history */
    memset(glow, 0, sizeof glow);
    if (!keep_view || !ok) {
        chan_off = 0; iscroll = 0; modified = 0;
        erow = ech = ecol = cur_ins = cur_sub = text_scroll = 0; epat = loaded && mi.mod->len ? mi.mod->xxo[0] : 0;
    }
    if (loaded) {                                          /* keep the cursor inside the (possibly smaller) song */
        if (epat >= mi.mod->pat) epat = 0;
        if (ech >= mi.mod->chn) ech = mi.mod->chn - 1;
        if (cur_ins >= mi.mod->ins) cur_ins = mi.mod->ins ? mi.mod->ins - 1 : 0;
        if (erow >= pat_rows(epat)) erow = 0;
    }
    return ok;
}

static void load(const char *path)
{
    char p[PATHLEN], inner[PATHLEN] = "";
    snprintf(p, sizeof p, "%s", path);                    /* path may point into pl[] we are about to keep */
    /* read / inflate first, outside the lock, so audio keeps running meanwhile */
    char *bar = strchr(p, '|');
    void *mem = NULL; size_t memlen = 0;
    if (bar) { *bar = 0; mem = zip_extract(p, bar + 1, &memlen, inner, sizeof inner); *bar = '|'; }
    else if (is_zipmod(p)) mem = zip_extract(p, NULL, &memlen, inner, sizeof inner);
    else mem = read_file(p, &memlen);
    if (!mem) { snprintf(status, sizeof status, "can't read %s", base(p)); return; }
    if (load_mem(mem, memlen, p, 0)) autosave_check();
    free(mem);                                             /* libxmp keeps its own copy */
}

/* play order: identity, or a shuffle with `first` moved to the front */
static void make_order(int first)
{
    for (int i = 0; i < npl; i++) order[i] = i;
    opos = first;
    if (!shuffle) return;
    for (int i = npl - 1; i > 0; i--) { int j = rand() % (i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < npl; i++) if (order[i] == first) { order[i] = order[0]; order[0] = first; }
    opos = 0;
}

/* unsaved edits: the first attempt to replace the song only warns; doing it again (same key) discards */
static int armed, armed_key, last_key;
static int discard_ok(void)
{
    if (!modified) return 1;
    if (armed && armed_key == last_key) { armed = 0; modified = 0; return 1; }
    armed = 1; armed_key = last_key;
    snprintf(status, sizeof status, "unsaved edits! Ctrl+S saves - again to discard");
    return 0;
}

static void play_entry(int i)
{
    if (i < 0 || i >= nents) return;
    if (ents[i].dir == 2) { open_zip(ents[i].name); return; }
    if (!ents[i].dir && !discard_ok()) return;
    if (ents[i].dir) { enter_dir(ents[i].name); return; }
    for (int k = 0; k < npl; k++) free(pl[k]);
    npl = 0;
    int first = 0;
    for (int k = 0; k < nents; k++) {                     /* playlist = every track in this folder (or zip) */
        char full[PATHLEN];
        if (ents[k].dir) continue;
        if (k == i) first = npl;
        entry_path(k, full, sizeof full);
        pl[npl++] = _strdup(full);
    }
    make_order(first);
    load(pl[order[opos]]);
}

/* next/previous in the playlist; at the ends wrap only with repeat-all */
static void step(int d)
{
    if (!npl) return;
    if (modified && ended_auto) { snprintf(status, sizeof status, "song ended - unsaved edits kept"); return; }
    if (!discard_ok()) return;
    int p = opos + d;
    if (p < 0 || p >= npl) {
        if (repeat != 2) { if (d > 0 && ended_auto) snprintf(status, sizeof status, "end of list"); return; }
        if (shuffle) { int last = order[opos]; make_order(rand() % npl); if (npl > 1 && order[0] == last) make_order((last + 1) % npl); }
        p = d > 0 ? 0 : npl - 1;
    }
    opos = p;
    load(pl[order[opos]]);
}

/* is there anything to show in the current folder (tracks or subfolders)? */
static int folder_has_stuff(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*", &fd);
    int found = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do found = fd.cFileName[0] != '.' && ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || is_module(fd.cFileName));
    while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

void core_init(int argc, char **argv, int prefer_last)
{
    InitializeCriticalSection(&cs);
    apply_theme();
    ctx = xmp_create_context();
    srand(GetTickCount());
    load_settings();
    char pick[MAX_PATH] = "";
    int use_last = argc > 1 && !strcmp(argv[1], "-l");
    if (argc <= 1) use_last = prefer_last || !folder_has_stuff();
    if (use_last && lastdir[0]) SetCurrentDirectoryA(lastdir);  /* falls back to the current folder if gone */
    else if (argc > 1) {                             /* kollapse <dir> or kollapse <file> */
        DWORD a = GetFileAttributesA(argv[1]);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) SetCurrentDirectoryA(argv[1]);
        else if (a != INVALID_FILE_ATTRIBUTES) {
            char full[MAX_PATH], *b;
            GetFullPathNameA(argv[1], MAX_PATH, full, &b);
            if (b) { strcpy(pick, b); b[0] = 0; SetCurrentDirectoryA(full); }
        }
    }
    scan();
    if (!nents || (nents == 1 && ents[0].dir && !nmods)) snprintf(status, sizeof status, "no modules here");
    else snprintf(status, sizeof status, "%d tracks, %d folders - Enter to play", nmods, nents - nmods);
    audio_start();
#ifdef _WIN32
    CreateThread(NULL, 0, media_thread, NULL, 0, NULL);
#else
    media_ok = mpris_start() ? 4 : 0;                      /* media keys over D-Bus */
#endif
    for (int i = 0; pick[0] && i < nents; i++) if (!_stricmp(ents[i].name, pick)) { fsel = i; play_entry(i); break; }
    if (zipview[0] && nmods && !loaded) { fsel = 1; play_entry(1); }   /* kollapse pack.zip: open it and start playing */
}

static void pause_toggle(void);
static void restart(void);
static void play_from(int loop);
static void stop(void);
static void step(int d);

/* in-place radix-2 FFT, n a power of two */
static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * 3.14159265358979 / len;
        float wr = (float)cos(a), wi = (float)sin(a);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int u = i + k, v = u + len / 2;
                float tr = re[v] * cr - im[v] * ci, ti = re[v] * ci + im[v] * cr;
                re[v] = re[u] - tr; im[v] = im[u] - ti; re[u] += tr; im[u] += ti;
                float t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

/* 64 log-spaced bands 40 Hz..16 kHz from the buffer being heard right now */
static void update_spectrum(double dt)
{
    static float re[FRAMES], im[FRAMES];
    EnterCriticalSection(&cs);
    const short *b = abuf[next_done];
    for (int i = 0; i < FRAMES; i++) {                     /* mono + Hann window */
        float w = 0.5f - 0.5f * (float)cos(2 * 3.14159265 * i / (FRAMES - 1));
        re[i] = (b[i * 2] + b[i * 2 + 1]) / 65536.0f * w; im[i] = 0;
    }
    LeaveCriticalSection(&cs);
    fft(re, im, FRAMES);
    for (int k = 0; k < 64; k++) {
        double f0 = 40 * pow(400.0, k / 64.0), f1 = 40 * pow(400.0, (k + 1) / 64.0);
        int b0 = (int)(f0 * FRAMES / RATE), b1 = (int)(f1 * FRAMES / RATE);
        if (b1 <= b0) b1 = b0 + 1;
        float m = 0;
        for (int j = b0; j < b1 && j < FRAMES / 2; j++) { float v = sqrtf(re[j] * re[j] + im[j] * im[j]); if (v > m) m = v; }
        float db = 20 * log10f(m / (FRAMES / 4) + 1e-6f), v = (db + 60) / 60;   /* -60..0 dB -> 0..1 */
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        spec[k] = v > spec[k] ? v : spec[k] - (float)dt * 1.5f;
        if (spec[k] < 0) spec[k] = 0;
        if (spec[k] >= peak[k]) { peak[k] = spec[k]; peak_hold[k] = 0.6f; }
        else if ((peak_hold[k] -= (float)dt) < 0) peak[k] -= (float)dt * 0.6f;
        if (peak[k] < 0) peak[k] = 0;
    }
}

void core_update(double dt)
{
#ifdef _WIN32
    last_frame = GetTickCount();
#endif
    EnterCriticalSection(&cs);
    if (loaded) fi = snap[next_done];
    LeaveCriticalSection(&cs);
#ifndef _WIN32
    { int c = mpris_poll(state, loaded ? (mi.mod->name[0] ? mi.mod->name : base(playing)) : ""); if (c) media_cmd = c; }
#endif
    switch (media_cmd) {                                   /* media keys */
    case 1: pause_toggle(); break;
    case 2: step(1); break;
    case 3: step(-1); break;
    case 4: stop(); break;
    }
    media_cmd = 0;
    if (loaded && state == 1 && page == P_EDIT) { epat = fi.pattern; erow = fi.row; }   /* editor follows playback */
    float rel = (float)exp(-dt * 10);                      /* VU: instant attack, ~100 ms release */
    for (int c = 0; c < XMP_MAX_CHANNELS; c++) {
        float pk = 0;
        if (loaded && c < mi.mod->chn)
            for (int i = 0; i < 24; i++) { float v = fabsf(core_scope(c, i / 24.0f)); if (v > pk) pk = v; }
        level[c] = pk > level[c] * rel ? pk : level[c] * rel;
    }
    if (page == P_SPECTRUM) update_spectrum(dt);
    autosave_tick();
    export_tick();
    ml_tick();
    float k = (float)exp(-dt * 6);
    for (int i = 0; i < 256; i++) glow[i] *= k;
    if (loaded && state == 1)
        for (int c = 0; c < mi.mod->chn; c++) {
            struct xmp_channel_info *ci = &fi.channel_info[c];
            float v = ci->volume / 64.0f;
            if (v > glow[ci->instrument] && !muted[c]) glow[ci->instrument] = v > 1 ? 1 : v;
        }
    if (ended) {                                       /* track finished: next subsong, repeat it, or move on */
        ended = 0;
        if (repeat != 1 && subsongs && cur_seq + 1 < nseq && !modified) play_seq(cur_seq + 1);
        else if (repeat == 1 && !modified) load(playing);       /* edited songs aren't reloaded from disk */
        else if (repeat == 1) restart();
        else { ended_auto = 1; step(1); ended_auto = 0; }
    }
}

/* ---------------- overlay ---------------- */
static const char *menu_items[] = {
    "Help / keys          F1",
    "Play / restart       F5",
    "Pause / resume       F6",
    "Stop                 F8",
    "Next track           N",
    "Previous track       P",
    "Shuffle on / off     S",
    "Repeat off/1/all     R",
    "Parent folder        Backspace",
    "Song message         F9",
    "Spectrum             F7",
    "Pattern editor       F2",
    "Samples              F3",
    "Instruments          F4",
    "Save as XM           Ctrl+S",
    "New song             Ctrl+N",
    "Load sample (WAV)    Ctrl+O",
    "Song / order list    F10",
    "Export WAV           Ctrl+E",
    "Autosave folder",
    "Search Modland       Ctrl+F",
    "Favorites            Shift+F",
    "Settings             C",
    "Downloads folder",
    "Quit                 Ctrl+Q",
};
static const int menu_keys[] = { K_F1, K_F5, K_F6, K_F8, 'n', 'p', 's', 'r', K_BACK, K_F9, K_F7, K_F2, K_F3, K_F4, K_CTRL | 's', K_CTRL | 'n', K_CTRL | 'o', K_F10, K_CTRL | 'e', K_CTRL | 'w', K_CTRL | 'f', 'F', 'c', K_CTRL | 'd', 17 };
#define N_MENU_ACTIVE (int)(sizeof menu_keys / sizeof *menu_keys)
static char media_line[80] = "media keys";
static int help_page;
static const char *help_lines[] = {
    "-- player --",
    "Up/Down PgUp/PgDn  pick a track or [folder]",
    "Enter              play track / open folder",
    "Backspace          parent folder",
    "S                  shuffle on / off",
    "R                  repeat: off / track / all",
    "F / Shift+F        star a track (favorite) / open favorites",
    "C                  settings: theme, fade, silence skip, sound...",
    "[ ]                previous / next subsong (files with several songs)",
    "                   songs that loop fade out, silence skips (C to set)",
    "F5                 play / restart song",
    "F6 or Space        pause / resume",
    "F8                 stop",
    "N / P              next / previous track",
    "Left / Right       skip back / ahead an order",
    "+ / -              volume",
    "1..9, 0            mute / unmute channel 1-10",
    "click pan bar/scope mute / unmute any channel",
    "< / >              scroll channels",
    "F2 F3 F4           pattern editor / samples / instruments",
    "F7 F9              spectrum / song message (again: back)",
    media_line,
    "Esc or F12         menu (Esc on a page: back to player)",
    "Ctrl+Q             quit",
    "",
    "-- pattern editor (F2) --",
    "arrows Tab PgUp/Dn move    Ctrl+PgUp/Dn  pattern",
    "Z S X D C V..  Q 2 W 3 E.. notes (FT2 piano)",
    "[ ]                octave down / up",
    "`                  key off      Delete  clear",
    "0-9 A-F G-Z        instrument, volume, effect",
    "Ctrl+Up/Down       instrument   Ctrl+Z  undo",
    "Ctrl+R             record on/off (off = jam: keys only play)",
    "                   while playing, notes land on the row you hear",
    "\\                  solo the cursor channel (again: all on)",
    "                   right-click a scope / pan bar: solo it",
    "Shift+arrows       select a block",
    "Ctrl+C / X / P     copy / cut / paste (also Ctrl+V)",
    "Ctrl+J             interpolate volume / effect over the block",
    "Ctrl+S             save as name-kollapse.xm",
    "",
    "-- composing --",
    "Ctrl+N             new song (8 channels, 1 pattern)",
    "Ctrl+O             load a WAV into the selected instrument",
    "Ctrl+E             export the song to name-kollapse.wav",
    "Enter (editor)     play from the cursor",
    "Shift+Enter        loop this pattern from the cursor (F8 stops)",
    "Ctrl+Z             undo - also patterns, samples, envelopes,",
    "                   orders and settings (newest first)",
    "F10                song page: title, BPM, speed, channels,",
    "                   order list (Ins dup, Del, N new, C clone)",
    "Enter (F3)         rename the sample; Shift+Enter the instrument",
    "",
    "-- Modland search (Ctrl+F) --",
    "type               artist / title words (all must match)",
    "Up/Down Enter      pick, download and play",
    "Ctrl+R             fetch a fresh index    Esc  close",
    "                   downloads go to the downloads= folder in kollapse.ini",
};
static const char *fx_xm_lines[] = {
    "",
    "-- effects (XM / MOD) --",
    "0xy arpeggio            8xx panning",
    "1xx slide up            9xx sample offset",
    "2xx slide down          Axy volume slide (x up, y down)",
    "3xx portamento to note  Bxx jump to order",
    "4xy vibrato             Cxx set volume (00-40)",
    "5xy 3xx + volume slide  Dxx break to row (decimal)",
    "6xy 4xy + volume slide  Fxx speed (<20) / BPM (>=20)",
    "7xy tremolo             Gxx global volume  Hxy global vol slide",
    "Kxx key off at tick     Lxx envelope position",
    "Pxy panning slide       Rxy retrigger + volume",
    "Txy tremor              X1x/X2x extra fine slide up/down",
    "E1x/E2x fine slide up/down   E5x finetune   E6x pattern loop",
    "E9x retrigger   EAx/EBx fine vol up/down   ECx cut   EDx delay",
    "EEx pattern delay       E3x glissando  E4x/E7x vib/trem wave",
    "volume column: - + slide, \x19 \x18 fine slide, S V vibrato,",
    "               P pan, \x1b \x1a pan slide, M portamento",
};
static const char *fx_s3m_lines[] = {
    "",
    "-- effects (S3M / IT) --",
    "Axx speed               Jxy arpeggio",
    "Bxx jump to order       Kxy H00 + volume slide",
    "Cxx break to row        Lxy G00 + volume slide",
    "Dxy volume slide        Mxx channel volume (IT)",
    "Exx/Fxx slide down/up   Nxy channel vol slide (IT)",
    "Gxx portamento to note  Oxx sample offset",
    "Hxy vibrato             Pxy panning slide (IT)",
    "Ixy tremor              Qxy retrigger",
    "Rxy tremolo             Txx tempo (BPM)",
    "Uxy fine vibrato        Vxx global volume",
    "Wxy global vol slide    Xxx panning",
    "Yxy panbrello           Zxx MIDI macro",
    "S1x glissando  S2x finetune  S3x/S4x vib/trem wave",
    "S8x pan  SBx loop  SCx cut  SDx delay  SEx row delay",
    "volume column: A B fine vol up/down, C D vol slide,",
    "               E F pitch slide, G portamento, H vibrato, P pan",
};

const char **core_overlay(int *n, int *sel, const char **title)
{
    if (overlay == 1) { *n = sizeof menu_items / sizeof *menu_items; *sel = osel; *title = "Menu"; return menu_items; }
    if (overlay == 2) {
        snprintf(media_line, sizeof media_line, "media keys         play-pause, next, prev, stop %s",
#ifdef _WIN32
                 media_ok == 4 ? "(active)" : media_ok > 0 ? "(some taken by another app)" : "(taken by another app)");
#else
                 media_ok == 4 ? "(active, MPRIS)" : "(off: no D-Bus)");
#endif
        static const char *all[128];                       /* keys, then the effects of the song's format */
        int nh = sizeof help_lines / sizeof *help_lines, nf = s3m_style ? sizeof fx_s3m_lines / sizeof *fx_s3m_lines : sizeof fx_xm_lines / sizeof *fx_xm_lines;
        memcpy(all, help_lines, nh * sizeof *all);
        memcpy(all + nh, s3m_style ? fx_s3m_lines : fx_xm_lines, nf * sizeof *all);
        /* help pages: long sections split evenly so each fits a small terminal */
        enum { HELP_ROWS = 16 };
        static const char *pg[128]; static char cont[64];
        int start[64], len[64], cont_of[64], np = 0, tot = nh + nf;
        for (int i = 0; i < tot; ) {                       /* section: heading .. next heading, minus blank lines at the ends */
            int e = i + 1;
            while (e < tot && strncmp(all[e], "-- ", 3)) e++;
            int L = e - i;
            while (L > 1 && !all[i + L - 1][0]) L--;
            int parts = (L + HELP_ROWS - 1) / HELP_ROWS, size = (L + parts - 1) / parts;
            for (int k = 0; k < parts && np < 64; k++) { start[np] = i + k * size; len[np] = k + 1 < parts ? size : L - k * size; cont_of[np++] = k ? i : -1; }
            i = e;
        }
        if (help_page >= np) help_page = np - 1;
        if (help_page < 0) help_page = 0;
        int a = start[help_page], cnt = 0;
        if (cont_of[help_page] >= 0) {                     /* a continued section repeats its heading */
            snprintf(cont, sizeof cont, "%s (continued)", all[cont_of[help_page]]);
            pg[cnt++] = cont;
        }
        for (int k = 0; k < len[help_page]; k++) if (cnt || all[a + k][0]) pg[cnt++] = all[a + k];
        static char ttl[64];
        snprintf(ttl, sizeof ttl, "Kollapse " KOLLAPSE_VERSION "  Help %d/%d   Left/Right page   Esc close", help_page + 1, np);
        *n = cnt; *sel = -1; *title = ttl; return pg;
    }
    if (overlay == 3) {                                /* sample picker: a 16-line window around the selection */
        static char ttl[96];
        static const char *none[1] = { " (no .wav files or folders here)" };
        int first = pick_sel < 8 ? 0 : pick_sel - 8;
        if (first > npick - 16) first = npick - 16 > 0 ? npick - 16 : 0;
        snprintf(ttl, sizeof ttl, "Load sample into instrument %02d", cur_ins + 1);
        *title = ttl;
        if (!npick) { *n = 1; *sel = -1; return none; }
        *n = npick - first < 16 ? npick - first : 16; *sel = pick_sel - first;
        return (const char **)(pick + first);
    }
    if (overlay == 4) return ml_overlay(n, sel, title);
    return NULL;
}

/* ---------------- input ---------------- */
static void restart(void)
{
    if (!loaded) { play_entry(fsel); return; }
    EnterCriticalSection(&cs);
    xmp_restart_module(ctx); state = 1; ended = 0; loop_pos = -1; fade_left = -1; quiet = 0; loops0 = -1; cur_seq = 0;
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, "playing %s", base(playing));
}

/* editor: Enter plays the song from the cursor, Shift+Enter loops the cursor's pattern from the cursor row */
static void play_from(int loop)
{
    if (!loaded) return;
    int pos = fi.pos >= 0 && fi.pos < mi.mod->len && mi.mod->xxo[fi.pos] == epat ? fi.pos : -1;
    for (int i = 0; pos < 0 && i < mi.mod->len; i++) if (mi.mod->xxo[i] == epat) pos = i;
    if (pos < 0) { snprintf(status, sizeof status, "pattern %02X isn't in the order list (F10 adds it)", epat); return; }
    int row = erow < pat_rows(epat) ? erow : 0;
    EnterCriticalSection(&cs);
    xmp_set_position(ctx, pos);
    xmp_set_row(ctx, row);
    state = 1; ended = 0; fade_left = -1; quiet = 0; loops0 = -1;
    loop_pos = loop ? pos : -1;
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, loop ? "looping pattern %02X (F8 stops)" : "playing from pattern %02X row %02X", epat, row);
}
/* subsongs ([ and ] in the player): jump to another song stored in the same file */
static void play_seq(int k)
{
    if (!loaded || k < 0 || k >= nseq) return;
    EnterCriticalSection(&cs);
    cur_seq = k;
    xmp_set_position(ctx, mi.seq_data[seqs[k]].entry_point);
    state = 1; ended = 0; fade_left = -1; quiet = 0; loops0 = -1; loop_pos = -1;
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, "subsong %d/%d  %s", k + 1, nseq, base(playing));
}
static void pause_toggle(void)
{
    if (!loaded) { play_entry(fsel); return; }
    state = state == 1 ? 2 : 1;
    snprintf(status, sizeof status, "%s %s", state == 1 ? "playing" : "paused", base(playing));
}
static int sel_is_playing(void)
{
    char p[PATHLEN];
    if (!loaded || fsel >= nents || ents[fsel].dir) return 0;
    entry_path(fsel, p, sizeof p);
    return !_stricmp(p, playing);
}
static void toggle_shuffle(void)
{
    int cur = npl ? order[opos] : 0;
    shuffle = !shuffle;
    if (npl) make_order(cur);                              /* keep the current track, reorder the rest */
    save_settings();
}
static void stop(void)
{
    if (!loaded) return;
    EnterCriticalSection(&cs);
    state = 0; loop_pos = -1; xmp_restart_module(ctx); fade_left = -1; quiet = 0; loops0 = -1; cur_seq = 0;
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, "stopped");
}
static void seek(int d)
{
    if (!loaded) return;
    EnterCriticalSection(&cs);
    if (d > 0) xmp_next_position(ctx); else xmp_prev_position(ctx);
    LeaveCriticalSection(&cs);
}
static void set_volume(int d)
{
    volume += d;
    if (volume < 0) volume = 0;
    if (volume > 200) volume = 200;
    EnterCriticalSection(&cs);
    if (loaded) xmp_set_player(ctx, XMP_PLAYER_VOLUME, volume);
    LeaveCriticalSection(&cs);
    save_settings();
}
static void toggle_mute(int c)
{
    if (c < 0 || c >= XMP_MAX_CHANNELS || (loaded && c >= mi.mod->chn)) return;
    muted[c] = !muted[c];
    EnterCriticalSection(&cs);
    if (loaded) xmp_channel_mute(ctx, c, muted[c]);
    LeaveCriticalSection(&cs);
}
/* solo: only this channel plays; soloing the channel that is already alone brings every channel back */
static void solo(int c)
{
    if (!loaded || c < 0 || c >= mi.mod->chn) return;
    int alone = !muted[c];
    for (int i = 0; i < mi.mod->chn; i++) if (i != c && !muted[i]) alone = 0;
    EnterCriticalSection(&cs);
    for (int i = 0; i < mi.mod->chn; i++) { muted[i] = alone ? 0 : i != c; xmp_channel_mute(ctx, i, muted[i]); }
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, alone ? "all channels on" : "solo channel %d", c + 1);
}
void core_solo(int ch) { solo(ch); }

/* ---------------- pattern editor ---------------- */
static int fx_s3m(int t, int *p);
static int fx_xm(int t);
static int vol_fx(int t, int p, char out[3]);
static void save_xm(void);

static int view_pat(void) { return page == P_EDIT ? epat : fi.pattern; }
static int pat_rows(int p) { return loaded && p >= 0 && p < mi.mod->pat ? mi.mod->xxp[p]->rows : 0; }
static struct xmp_event *ev_at(int p, int r, int c)
{
    if (!pat_rows(p) || c < 0 || c >= mi.mod->chn) return NULL;
    struct xmp_track *t = mi.mod->xxt[mi.mod->xxp[p]->index[c]];
    return r >= 0 && r < t->rows ? &t->event[r] : NULL;
}
static int next_group(void) { snap_key = -1; return ++ugroup; }   /* a new user action */
static void push_undo(int p, int r, int c)                 /* remember a cell under the current undo group */
{
    struct xmp_event *e = ev_at(p, r, c);
    if (!e) return;
    undo[uhead % UNDO].pat = (short)p; undo[uhead % UNDO].row = (short)r; undo[uhead % UNDO].ch = (short)c;
    undo[uhead % UNDO].old = *e; undo[uhead % UNDO].group = ugroup;
    modified = 1;
    uhead++;
    if (ucount < UNDO) ucount++;
}
static void edit_begin(struct xmp_event *e)            /* one-cell edit at the cursor: its own undo group, then lock */
{
    (void)e;
    next_group();
    push_undo(epat, erow, ech);
    EnterCriticalSection(&cs);
}

/* the song as XM bytes (for snapshots, autosave and export) */
static void *song_bytes(size_t *n);
static void snapshot_push(void)                            /* before a structural / non-cell change */
{
    size_t n;
    void *buf = loaded ? song_bytes(&n) : NULL;
    if (!buf) return;
    while (nsnaps && (nsnaps == SNAPS || snap_bytes + n > ((size_t)96 << 20))) {   /* keep 32 / 96 MB */
        free(snaps[0].data); snap_bytes -= snaps[0].len;
        memmove(snaps, snaps + 1, (nsnaps - 1) * sizeof *snaps); nsnaps--;
    }
    snaps[nsnaps].data = buf; snaps[nsnaps].len = n; snaps[nsnaps].group = ++ugroup; nsnaps++;
    snap_bytes += n;
}
static void snap_coalesce(int key)                         /* repeated tweaks of the same thing = one undo step */
{
    if (key != snap_key) { snapshot_push(); snap_key = key; }
}
static void snaps_clear(void) { while (nsnaps) free(snaps[--nsnaps].data); snap_bytes = 0; snap_key = -1; }
static void edit_end(void)
{
    LeaveCriticalSection(&cs);
    int n = pat_rows(epat);
    if (state != 1 && n) erow = (erow + estep) % n;        /* edit step; while playing the cursor follows the song */
}
/* on to the field's next character; after its last one, step down */
static void field_end(void)
{
    static const signed char last[8] = { 0, 2, 2, 4, 4, 7, 7, 7 }, first[8] = { 0, 1, 1, 3, 3, 5, 5, 5 };
    if (ecol < last[ecol]) { LeaveCriticalSection(&cs); ecol++; return; }
    ecol = first[ecol];
    edit_end();
}
static int load_mem(const void *mem, size_t memlen, const char *path, int keep_view);
static void do_undo(void)                                  /* newest action first: cell group or song snapshot */
{
    int cg = ucount ? undo[(uhead - 1) % UNDO].group : -1, sg = nsnaps ? snaps[nsnaps - 1].group : -1;
    if (cg < 0 && sg < 0) { snprintf(status, sizeof status, "nothing to undo"); return; }
    if (sg > cg) {                                         /* put the whole song back */
        nsnaps--;
        snap_bytes -= snaps[nsnaps].len;
        char keep[PATHLEN]; snprintf(keep, sizeof keep, "%s", playing);
        int keepmod = 1;
        load_mem(snaps[nsnaps].data, snaps[nsnaps].len, keep, 1);
        free(snaps[nsnaps].data);
        modified = keepmod; snap_key = -1;
        snprintf(status, sizeof status, "undone (song restored)");
        return;
    }
    int n = 0;
    EnterCriticalSection(&cs);
    while (ucount && undo[(uhead - 1) % UNDO].group == cg) {  /* the whole action, newest first */
        uhead--; ucount--; n++;
        struct xmp_event *e = ev_at(undo[uhead % UNDO].pat, undo[uhead % UNDO].row, undo[uhead % UNDO].ch);
        if (e) *e = undo[uhead % UNDO].old;
    }
    LeaveCriticalSection(&cs);
    snap_key = -1;
    snprintf(status, sizeof status, "undid %d cell%s", n, n == 1 ? "" : "s");
}

/* ---------------- blocks: Shift+arrows select, Ctrl+C/X/V, Delete, < > transpose ---------------- */
static void block_range(int *r0, int *c0, int *r1, int *c1)   /* the block, or just the cursor cell */
{
    if (!sel_on) { *r0 = *r1 = erow; *c0 = *c1 = ech; return; }
    *r0 = sel_r0 < sel_r1 ? sel_r0 : sel_r1; *r1 = sel_r0 < sel_r1 ? sel_r1 : sel_r0;
    *c0 = sel_c0 < sel_c1 ? sel_c0 : sel_c1; *c1 = sel_c0 < sel_c1 ? sel_c1 : sel_c0;
}
static void block_copy(int cut)
{
    int r0, c0, r1, c1;
    block_range(&r0, &c0, &r1, &c1);
    free(clip.ev);
    clip.rows = r1 - r0 + 1; clip.chans = c1 - c0 + 1;
    clip.ev = calloc((size_t)clip.rows * clip.chans, sizeof *clip.ev);
    if (!clip.ev) { clip.rows = clip.chans = 0; return; }
    if (cut) { next_group(); EnterCriticalSection(&cs); }
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            struct xmp_event *e = ev_at(epat, r, c);
            if (!e) continue;
            clip.ev[(r - r0) * clip.chans + (c - c0)] = *e;
            if (cut) { push_undo(epat, r, c); memset(e, 0, sizeof *e); }
        }
    if (cut) LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, "%s %dx%d", cut ? "cut" : "copied", clip.rows, clip.chans);
}
static void block_paste(void)
{
    if (!clip.ev) { snprintf(status, sizeof status, "nothing copied (Ctrl+C)"); return; }
    next_group();
    EnterCriticalSection(&cs);
    for (int r = 0; r < clip.rows; r++)
        for (int c = 0; c < clip.chans; c++) {
            struct xmp_event *e = ev_at(epat, erow + r, ech + c);   /* clipped at the pattern's edges */
            if (e) { push_undo(epat, erow + r, ech + c); *e = clip.ev[r * clip.chans + c]; }
        }
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, "pasted %dx%d", clip.rows, clip.chans);
}
static void block_clear(void)
{
    int r0, c0, r1, c1;
    block_range(&r0, &c0, &r1, &c1);
    next_group();
    EnterCriticalSection(&cs);
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) { struct xmp_event *e = ev_at(epat, r, c); if (e) { push_undo(epat, r, c); memset(e, 0, sizeof *e); } }
    LeaveCriticalSection(&cs);
    sel_on = 0;
}
static void block_transpose(int d)
{
    int r0, c0, r1, c1, n = 0;
    block_range(&r0, &c0, &r1, &c1);
    next_group();
    EnterCriticalSection(&cs);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            struct xmp_event *e = ev_at(epat, r, c);
            if (e && e->note >= 1 && e->note <= 120 && e->note + d >= 1 && e->note + d <= 120) { push_undo(epat, r, c); e->note += d; n++; }
        }
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, "transposed %d note%s %s", n, n == 1 ? "" : "s", d > 0 ? "up" : "down");
}
/* Ctrl+J: slide volume (and a shared effect's value) evenly across the block */
static void block_interpolate(void)
{
    int r0, c0, r1, c1, n = 0;
    block_range(&r0, &c0, &r1, &c1);
    if (r1 - r0 < 2) { snprintf(status, sizeof status, "select at least 3 rows (Shift+arrows)"); return; }
    next_group();
    EnterCriticalSection(&cs);
    for (int c = c0; c <= c1; c++) {
        struct xmp_event *a = ev_at(epat, r0, c), *b = ev_at(epat, r1, c);
        if (!a || !b) continue;
        int vol = a->vol && b->vol;
        int fx = (a->fxt || a->fxp) && a->fxt == b->fxt && (a->fxt != 0x0e || (a->fxp >> 4) == (b->fxp >> 4));
        int ext = a->fxt == 0x0e;                          /* Exy: only the low digit is a value */
        int pa = ext ? a->fxp & 15 : a->fxp, pb = ext ? b->fxp & 15 : b->fxp;
        for (int r = r0 + 1; r < r1 && (vol || fx); r++) {
            struct xmp_event *e = ev_at(epat, r, c);
            if (!e) continue;
            push_undo(epat, r, c);
            int t = r - r0, len = r1 - r0;
            if (vol) { e->vol = a->vol + ((b->vol - a->vol) * t + (b->vol >= a->vol ? len / 2 : -len / 2)) / len; e->f2t = e->f2p = 0; }
            if (fx) { int v = pa + ((pb - pa) * t + (pb >= pa ? len / 2 : -len / 2)) / len; e->fxt = a->fxt; e->fxp = ext ? (a->fxp & 0xf0) | v : v; }
            n++;
        }
    }
    LeaveCriticalSection(&cs);
    if (n) snprintf(status, sizeof status, "interpolated %d cells", n);
    else snprintf(status, sizeof status, "nothing to interpolate: give the first and last row a volume or the same effect");
}

/* FT2/Schism piano: two rows, semitone offset from the current octave, -1 if not a piano key */
static int piano(int c)
{
    static const char lo[] = "zsxdcvgbhnjm,l.;/", hi[] = "q2w3er5t6y7ui9o0p";
    const char *p;
    if (!c) return -1;
    if ((p = strchr(lo, c))) return (int)(p - lo);
    if ((p = strchr(hi, c))) return 12 + (int)(p - hi);
    return -1;
}

static void preview(int ins, int note)
{
    if (!loaded || ins < 0 || ins >= mi.mod->ins || note < 1 || note > 120) return;
    struct xmp_instrument *xi = &mi.mod->xxi[ins];
    if (!xi->nsm) return;
    int k = xi->map[note - 1].ins;
    if (k >= xi->nsm) k = 0;
    struct xmp_subinstrument *su = &xi->sub[k];
    if (su->sid < 0 || su->sid >= mi.mod->smp) return;
    struct xmp_sample *s = &mi.mod->xxs[su->sid];
    if (!s->data || s->len <= 0) return;
    double semis = note - 61 + su->xpo + xi->map[note - 1].xpo + su->fin / 128.0;   /* libxmp C-5 = 8363 Hz */
    EnterCriticalSection(&cs);
    pv.s = s; pv.pos = 0; pv.step = 8363.0 * pow(2, semis / 12) / RATE;
    pv.vol = su->vol / 64.0f; pv.frames = 0; pv.on = 1;
    LeaveCriticalSection(&cs);
}

/* effect letter (+param) as shown -> libxmp internal effect. Inverse of fx_s3m / fx_xm. */
static int s3m_code(int L, int p, int *t, int *q)
{
    static const unsigned char tab[26] = {   /* A..Z; 0 = special */
        0xa3, 0x0b, 0, 0x0a, 0x02, 0x01, 0x03, 0x04, 0x1d, 0xb4, 0x06, 0x05, 0x80,
        0x81, 0x09, 0x89, 0x1b, 0x07, 0, 0, 0xac, 0x10, 0x11, 0x08, 0x8a, 0xbe };
    *q = p;
    if (L == 'C') { *t = is_it ? 0x8e : 0x0d; return 1; }
    if (L == 'T') { *t = is_it ? 0x87 : 0xab; return 1; }
    if (L == 'S') {
        int l = p & 15;
        switch (p >> 4) {
        case 0x1: *t = 0x0e; *q = 0x30 | l; return 1;     /* glissando */
        case 0x2: *t = 0x0e; *q = 0x50 | l; return 1;     /* finetune */
        case 0x3: *t = 0x0e; *q = 0x40 | l; return 1;     /* vibrato waveform */
        case 0x4: *t = 0x0e; *q = 0x70 | l; return 1;     /* tremolo waveform */
        case 0x5: *t = 0x8b; *q = l; return 1;            /* panbrello waveform */
        case 0x6: *t = 0x0e; *q = 0xe0 | l; return 1;     /* pattern delay (ticks) */
        case 0x7: *t = 0x83; *q = l; return 1;            /* instrument function */
        case 0x8: *t = 0x08; *q = l << 4; return 1;       /* set panning (shows as Xx0) */
        case 0x9: if (l >= 0xe) { *t = 0x8f; *q = l - 0xe; } else { *t = 0x8d; *q = l; } return 1;
        case 0xa: *t = 0x8c; *q = l; return 1;            /* high offset */
        case 0xb: *t = 0x0e; *q = 0x60 | l; return 1;     /* pattern loop */
        case 0xc: *t = 0x0e; *q = 0xc0 | l; return 1;     /* note cut */
        case 0xd: *t = 0x0e; *q = 0xd0 | l; return 1;     /* note delay */
        case 0xe: *t = 0x88; *q = l; return 1;            /* row delay */
        case 0xf: *t = 0xbd; *q = l; return 1;            /* macro select */
        }
        return 0;
    }
    if (L < 'A' || L > 'Z' || !tab[L - 'A']) return 0;
    *t = tab[L - 'A'];
    return 1;
}
static int xm_code(int L)
{
    if (L >= '0' && L <= '9') return L - '0';
    if (L >= 'A' && L <= 'F') return L - 'A' + 10;
    switch (L) { case 'G': return 0x10; case 'H': return 0x11; case 'K': return 0x14; case 'L': return 0x15;
                 case 'P': return 0x19; case 'R': return 0x1b; case 'T': return 0x1d; case 'X': return 0x21; }
    return -1;
}

/* volume column: typed key -> the letter vol_fx shows. XM: - + d(own) u(p) s v p l r m.  IT: a-h, p */
static int vol_letter(int c)
{
    if (s3m_style) return (c >= 'a' && c <= 'h') || c == 'p' ? toupper(c) : 0;
    switch (c) {
    case '-': return '-';  case '+': return '+';   case 'd': return '\x19'; case 'u': return '\x18';
    case 's': return 'S';  case 'v': return 'V';   case 'p': return 'P';
    case 'l': return '\x1b'; case 'r': return '\x1a'; case 'm': return 'M';
    }
    return 0;
}
/* shown letter + value 0-15 -> libxmp's f2t/f2p (inverse of vol_fx) */
static int vol_encode(int L, int v, int *t, int *p)
{
    /* libxmp stores "slide up 0" like "slide down 0" (same for vibrato speed 0 / pan slide left 0), so those
       would flip to the other letter. A value of 0 does nothing for them anyway: their minimum is 1. */
    if (!s3m_style && !v && (L == '+' || L == 'S' || L == '\x1b')) v = 1;
    if (s3m_style) {
        static const int g[10] = { 0x00, 0x01, 0x04, 0x08, 0x10, 0x20, 0x40, 0x60, 0x80, 0xff };
        switch (L) {
        case 'A': *t = 0xc2; *p = v; return 1;  case 'B': *t = 0xc3; *p = v; return 1;
        case 'C': *t = 0xc0; *p = v; return 1;  case 'D': *t = 0xc1; *p = v; return 1;
        case 'E': *t = 0x02; *p = v << 2; return 1;  case 'F': *t = 0x01; *p = v << 2; return 1;
        case 'G': *t = 0x03; *p = g[v > 9 ? 9 : v]; return 1;
        case 'H': *t = 0x04; *p = v; return 1;  case 'P': *t = 0x08; *p = v << 4; return 1;
        }
        return 0;
    }
    switch (L) {
    case '-': *t = 0xa4; *p = v; return 1;           case '+': *t = 0xa4; *p = v << 4; return 1;
    case '\x19': *t = 0x0e; *p = 0xb0 | v; return 1; case '\x18': *t = 0x0e; *p = 0xa0 | v; return 1;
    case 'S': *t = 0x04; *p = v << 4; return 1;      case 'V': *t = 0x04; *p = v; return 1;
    case 'P': *t = 0x08; *p = v << 4; return 1;      case 'M': *t = 0x03; *p = v << 4; return 1;
    case '\x1b': *t = 0xb5; *p = v << 4; return 1;   case '\x1a': *t = 0xb5; *p = v; return 1;
    }
    return 0;
}

static int edit_key(int k)                                 /* 1 = handled */
{
    int rows = pat_rows(epat), nch = loaded ? mi.mod->chn : 0, still = state != 1;
    if (!rows) return 0;
    if (erow >= rows) erow = rows - 1;
    int base_k = k & ~K_SHIFT, move = base_k >= K_UP && base_k <= K_END;
    if (move && (k & K_SHIFT)) {                           /* Shift+movement: grow the block from where it started */
        if (!sel_on) { sel_on = 1; sel_r0 = erow; sel_c0 = ech; }
        k = base_k;
        if (k == K_LEFT || k == K_RIGHT) {                 /* sideways the block grows by whole channels */
            if (k == K_LEFT && ech > 0) ech--;
            if (k == K_RIGHT && ech < nch - 1) ech++;
            sel_r1 = erow; sel_c1 = ech;
            return 1;
        }
    } else if (move || k == K_TAB || k == K_STAB) sel_on = 0;   /* plain movement drops the block */
    switch (k) {                                           /* block keys */
    case K_CTRL | 'c': block_copy(0); return 1;
    case K_CTRL | 'x': block_copy(1); sel_on = 0; return 1;
    case K_CTRL | 'v': block_paste(); return 1;
    case '<': block_transpose(-1); return 1;
    case '>': block_transpose(1); return 1;
    case K_CTRL | K_LEFT:  if (estep > 0) estep--; snprintf(status, sizeof status, "edit step %d", estep); return 1;
    case K_CTRL | K_RIGHT: if (estep < 16) estep++; snprintf(status, sizeof status, "edit step %d", estep); return 1;
    case K_ENTER: play_from(0); return 1;
    case K_ENTER | K_SHIFT: play_from(1); return 1;
    case K_CTRL | 'j': block_interpolate(); return 1;
    case K_CTRL | 'r': rec = !rec; snprintf(status, sizeof status, rec ? "record: keys write notes" : "jam: keys only play notes (Ctrl+R records)"); return 1;
    case '\\': solo(ech); return 1;
    }
    if (k == K_DEL && sel_on) { block_clear(); return 1; }
    int handled = 1;
    switch (k) {
    case K_UP:   if (still) erow = (erow + rows - 1) % rows; break;
    case K_DOWN: if (still) erow = (erow + 1) % rows; break;
    case K_PGUP: if (still) erow = erow >= 16 ? erow - 16 : 0; break;
    case K_PGDN: if (still) erow = erow + 16 < rows ? erow + 16 : rows - 1; break;
    case K_HOME: if (still) erow = 0; break;
    case K_END:  if (still) erow = rows - 1; break;
    case K_LEFT:  if (ecol > 0) ecol--; else if (ech > 0) { ech--; ecol = 7; } break;
    case K_RIGHT: if (ecol < 7) ecol++; else if (ech < nch - 1) { ech++; ecol = 0; } break;
    case K_TAB:  if (ech < nch - 1) ech++; ecol = 0; break;
    case K_STAB: if (ech > 0) ech--; ecol = 0; break;
    case K_CTRL | K_PGUP: if (still && epat > 0) epat--; sel_on = 0; return 1;
    case K_CTRL | K_PGDN: if (still && epat < mi.mod->pat - 1) epat++; sel_on = 0; return 1;
    default: handled = 0;
    }
    if (handled) { if (sel_on) { sel_r1 = erow; sel_c1 = ech; } return 1; }
    struct xmp_event *e = ev_at(epat, erow, ech);
    int c = k < 128 ? tolower(k) : 0, hex = -1;
    if (!rec) {                                            /* jam: the piano plays, nothing is written */
        int s = piano(c);
        if (s >= 0 && octave * 12 + s + 1 <= 120) preview(cur_ins, octave * 12 + s + 1);
        return s >= 0;
    }
    if (!e) return 0;
    if (k == K_DEL) {
        edit_begin(e);
        if (ecol == 0) e->note = e->ins = 0;
        else if (ecol <= 2) e->ins = 0;
        else if (ecol <= 4) e->vol = e->f2t = e->f2p = 0;
        else e->fxt = e->fxp = 0;
        edit_end();
        return 1;
    }
    if (ecol == 0) {
        int s = piano(c);
        if (k == '`') { edit_begin(e); e->note = XMP_KEY_OFF; e->ins = 0; edit_end(); return 1; }
        if (s < 0) return 0;
        int note = octave * 12 + s + 1;
        if (note > 120) return 1;
        int ch = ech, row = erow, chord = 0, keep = erow;
        {                                                  /* notes pressed together form a chord, one per channel */
            static DWORD chord_t; static int chord_pat = -1, chord_row, chord_ch;
            DWORD now = GetTickCount();
            if (now - chord_t < 90 && chord_pat == epat) { row = chord_row; ch = chord_ch + 1 < nch ? chord_ch + 1 : chord_ch; chord = 1; }
            chord_t = now; chord_pat = epat; chord_row = row; chord_ch = ch;
            if (!(e = ev_at(epat, row, ch))) return 1;
        }
        next_group();
        push_undo(epat, row, ch);
        EnterCriticalSection(&cs);
        e->note = note;
        if (mi.mod->ins) e->ins = cur_ins + 1;
        edit_end();
        if (chord) erow = keep;                            /* the first note already stepped the cursor */
        preview(cur_ins, note);
        return 1;
    }
    if (c >= '0' && c <= '9') hex = c - '0'; else if (c >= 'a' && c <= 'f') hex = c - 'a' + 10;
    if (ecol <= 2) {                                       /* instrument: decimal digits */
        if (hex < 0 || hex > 9) return 0;
        edit_begin(e);
        int v = e->ins % 100;
        v = ecol == 1 ? hex * 10 + v % 10 : v / 10 * 10 + hex;
        e->ins = v > mi.mod->ins ? mi.mod->ins : v;
        field_end();
        return 1;
    }
    if (ecol <= 4) {                                       /* volume (decimal) or a volume-column effect */
        char cur[3]; int letter = !e->vol && vol_fx(e->f2t, e->f2p, cur) ? cur[0] : 0, t, p;
        int L = ecol == 3 ? vol_letter(c ? c : k) : 0;
        if (L) {                                           /* effect letter: keep the old value if there was one */
            int v = letter ? (int)(strchr("0123456789ABCDEF", cur[1]) - "0123456789ABCDEF") : 0;
            if (!vol_encode(L, v, &t, &p)) return 1;
            edit_begin(e); e->vol = 0; e->f2t = t; e->f2p = p; field_end();
            return 1;
        }
        if (ecol == 4 && letter) {                         /* effect value: one hex digit */
            if (hex < 0 || !vol_encode(letter, hex, &t, &p)) return hex >= 0;
            edit_begin(e); e->f2t = t; e->f2p = p; field_end();
            return 1;
        }
        if (hex < 0 || hex > 9) return 0;                  /* plain volume 00-64 */
        edit_begin(e);
        int v = e->vol ? e->vol - 1 : 0;
        v = ecol == 3 ? hex * 10 + v % 10 : v / 10 * 10 + hex;
        e->vol = (v > 64 ? 64 : v) + 1; e->f2t = e->f2p = 0;
        field_end();
        return 1;
    }
    /* effect: edit the letter + param as displayed, then convert back to libxmp's codes */
    int has = e->fxt || e->fxp, p = e->fxp;
    int L = has ? (s3m_style ? fx_s3m(e->fxt, &p) : fx_xm(e->fxt)) : 0;
    if (!has) { p = 0; L = s3m_style ? 0 : '0'; }
    if (ecol == 5) {
        if (!isalnum(c)) return 0;
        L = toupper(c);
        if (L == 'S' && !(p >> 4)) p = 0x10 | (p & 15);   /* S0x means nothing: start at S1x */
    } else {
        if (hex < 0) return 0;
        if (!L) { snprintf(status, sizeof status, "type the effect letter first"); return 1; }
        p = ecol == 6 ? (hex << 4) | (p & 15) : (p & 0xf0) | hex;
    }
    int t, q;
    if (s3m_style) { if (!s3m_code(L, p, &t, &q)) return 1; }
    else { if ((t = xm_code(L)) < 0) return 1; q = p; }
    edit_begin(e); e->fxt = t; e->fxp = q; field_end();
    return 1;
}

/* sample / instrument / message pages */
static int sample_key(int k);
static const struct xmp_sample *cur_sample(int *sid);
static int instr_key(int k);
static int page_key(int k)                                 /* 1 = handled */
{
    int nins = loaded ? mi.mod->ins : 0, s;
    if (page == P_MESSAGE) {
        switch (k) {
        case K_UP: text_scroll--; break;     case K_DOWN: text_scroll++; break;
        case K_PGUP: text_scroll -= 20; break; case K_PGDN: text_scroll += 20; break;
        case K_HOME: text_scroll = 0; break;   case K_END: text_scroll = 1 << 20; break;
        default: return 0;
        }
        if (text_scroll < 0) text_scroll = 0;
        return 1;
    }
    if (page == P_SAMPLE) {
        if (sample_key(k)) return 1;
        if (k < 128 && (s = piano(tolower(k))) >= 0) { preview(cur_ins, octave * 12 + s + 1); return 1; }
        return 0;
    }
    if (page == P_INSTR) return instr_key(k);
    (void)nins;
    return 0;
}

/* sample page: ↑/↓ field, ←/→ change, PgUp/PgDn big steps, Shift+←/→ loop points by one sample,
   Tab / Shift+Tab other sample of the instrument. Edits go straight into the playing song. */
static const char *sfield_name[9] = { "Loop", "Loop start", "Loop end", "Volume", "Finetune", "Rel. note", "Panning", "Range start", "Range end" };
static int sfield;
static int srange_a, srange_b;
static void sample_op(int op);
static int keymap_key(int k);
static int vibrato_key(int k);
static void keymap_text(int rows, int *first);
static void vibrato_text(void);
static int sample_key(int k)
{
    if (!loaded || cur_ins >= mi.mod->ins) return 0;
    struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    switch (k) {
    case K_UP:   if (sfield > 0) sfield--; return 1;
    case K_DOWN: if (sfield < 8) sfield++; return 1;
    case K_CTRL | 'x': sample_op('x'); return 1;
    case K_CTRL | 't': sample_op('t'); return 1;
    case K_CTRL | 'r': sample_op('r'); return 1;
    case K_CTRL | 'l': sample_op('l'); return 1;
    case K_TAB:  if (cur_sub < xi->nsm - 1) cur_sub++; return 1;
    case K_STAB: if (cur_sub > 0) cur_sub--; return 1;
    case K_ENTER: {                                        /* rename the sample */
        const struct xmp_sample *s = cur_sample(NULL);
        if (s) { snapshot_push(); snap_key = -1; name_edit = (char *)s->name; name_max = 22; }
        return 1;
    }
    case K_ENTER | K_SHIFT: snapshot_push(); snap_key = -1; name_edit = xi->name; name_max = 22; return 1;   /* rename the instrument */
    }
    int d = k == K_RIGHT ? 1 : k == K_LEFT ? -1 : k == K_PGUP ? 16 : k == K_PGDN ? -16 : 0, fine = 0;
    if (k == (K_RIGHT | K_SHIFT)) { d = 1; fine = 1; }
    if (k == (K_LEFT | K_SHIFT))  { d = -1; fine = 1; }
    if (!d || cur_sub >= xi->nsm) return d != 0;
    int sid = xi->sub[cur_sub].sid;
    if (sid < 0 || sid >= mi.mod->smp) return 1;
    struct xmp_sample *s = &mi.mod->xxs[sid];
    struct xmp_subinstrument *su = &xi->sub[cur_sub];
    int lstep = fine ? 1 : (s->len / 512 > 1 ? s->len / 512 : 1);
    if (sfield < 7) snap_coalesce((1 << 24) | cur_ins << 12 | cur_sub << 8 | sfield);   /* undo point (repeats coalesce) */
    EnterCriticalSection(&cs);
    switch (sfield) {
    case 0: {                                              /* off -> forward -> pingpong */
        int t = !(s->flg & XMP_SAMPLE_LOOP) ? 0 : s->flg & XMP_SAMPLE_LOOP_BIDIR ? 2 : 1;
        t = (t + (d > 0 ? 1 : 2)) % 3;
        s->flg &= ~(XMP_SAMPLE_LOOP | XMP_SAMPLE_LOOP_BIDIR);
        if (t) s->flg |= XMP_SAMPLE_LOOP | (t == 2 ? XMP_SAMPLE_LOOP_BIDIR : 0);
        if (t && (s->lpe <= s->lps || s->lpe > s->len)) { s->lps = 0; s->lpe = s->len; }
        break;
    }
    case 1: s->lps += d * lstep; if (s->lps < 0) s->lps = 0; if (s->lps > s->lpe - 1) s->lps = s->lpe - 1; break;
    case 2: s->lpe += d * lstep; if (s->lpe > s->len) s->lpe = s->len; if (s->lpe < s->lps + 1) s->lpe = s->lps + 1; break;
    case 3: su->vol += d; if (su->vol < 0) su->vol = 0; if (su->vol > 64) su->vol = 64; break;
    case 4: su->fin += d; if (su->fin < -128) su->fin = -128; if (su->fin > 127) su->fin = 127; break;
    case 5: su->xpo += d; if (su->xpo < -96) su->xpo = -96; if (su->xpo > 96) su->xpo = 96; break;
    case 6: su->pan += d; if (su->pan < 0) su->pan = 0; if (su->pan > 255) su->pan = 255; break;
    case 7: case 8:                                        /* range for cut / trim / reverse / normalize */
        if (srange_b <= srange_a) { srange_a = 0; srange_b = s->len; }
        if (sfield == 7) { srange_a += d * lstep; if (srange_a < 0) srange_a = 0; if (srange_a > srange_b - 1) srange_a = srange_b - 1; }
        else { srange_b += d * lstep; if (srange_b > s->len) srange_b = s->len; if (srange_b < srange_a + 1) srange_b = srange_a + 1; }
        LeaveCriticalSection(&cs);
        return 1;                                          /* only a selection: the song is not modified */
    }
    LeaveCriticalSection(&cs);
    modified = 1;
    return 1;
}

/* instrument page: Tab volume/pan envelope, ←/→ point, ↑/↓ value (Shift x8), Shift+←/→ move point in time,
   Ins add / Del remove point, E on/off, S sustain here, B / N loop start / end here, O loop off,
   , . fadeout, Enter preview */
static int env_which, env_pt;
static int instr_key(int k)
{
    if (!loaded || cur_ins >= mi.mod->ins) return 0;
    struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    struct xmp_envelope *e = env_which ? &xi->pei : &xi->aei;
    int c = k < 128 ? tolower(k) : 0;
    if (k == K_TAB) { env_which = (env_which + 1) % 4; env_pt = 0; return 1; }      /* vol env, pan env, keymap, vibrato */
    if (k == K_STAB) { env_which = (env_which + 3) % 4; env_pt = 0; return 1; }
    if (env_which == 2) return keymap_key(k);
    if (env_which == 3) return vibrato_key(k);
    if (k == K_ENTER) { preview(cur_ins, 61); return 1; }
    if (c == ',' || c == '.') { snap_coalesce((2 << 24) | cur_ins << 12 | 0xff); xi->rls += c == '.' ? 64 : -64; if (xi->rls < 0) xi->rls = 0; if (xi->rls > 0xfff * 2) xi->rls = 0xfff * 2; modified = 1; return 1; }
    if (e->npt <= 0 || e->npt > 12) {                      /* no envelope yet: Ins starts one */
        if (k != K_INS && c != 'e') return k < 128 || k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN;
        snapshot_push(); snap_key = -1;
        EnterCriticalSection(&cs);
        e->npt = 2; e->data[0] = 0; e->data[1] = env_which ? 32 : 64; e->data[2] = 32; e->data[3] = env_which ? 32 : 0;
        e->flg = 1; e->sus = e->lps = e->lpe = 0;
        LeaveCriticalSection(&cs);
        env_pt = 0; modified = 1;
        return 1;
    }
    if (env_pt >= e->npt) env_pt = e->npt - 1;
    short *x = &e->data[env_pt * 2], *y = x + 1;
    int lo = env_pt ? e->data[(env_pt - 1) * 2] + 1 : 0, hi = env_pt < e->npt - 1 ? e->data[(env_pt + 1) * 2] - 1 : 0x7fff;
    if (k != K_LEFT && k != K_RIGHT) {                     /* undo point for anything that changes the envelope */
        int bk = k & ~K_SHIFT, moves = bk == K_UP || bk == K_DOWN || k == (K_LEFT | K_SHIFT) || k == (K_RIGHT | K_SHIFT);
        if (moves) snap_coalesce((3 << 24) | cur_ins << 12 | env_which << 8 | env_pt);
        else if (k == K_INS || k == K_DEL || (c && strchr("esbno", c))) { snapshot_push(); snap_key = -1; }
    }
    EnterCriticalSection(&cs);
    switch (k) {
    case K_LEFT:  if (env_pt > 0) env_pt--; break;
    case K_RIGHT: if (env_pt < e->npt - 1) env_pt++; break;
    case K_UP: case K_DOWN: case K_UP | K_SHIFT: case K_DOWN | K_SHIFT: {
        int d = ((k & ~K_SHIFT) == K_UP ? 1 : -1) * (k & K_SHIFT ? 8 : 1);
        *y += d; if (*y < 0) *y = 0; if (*y > 64) *y = 64; modified = 1; break;
    }
    case K_LEFT | K_SHIFT: case K_RIGHT | K_SHIFT:
        if (env_pt) { *x += k == (K_RIGHT | K_SHIFT) ? 1 : -1; if (*x < lo) *x = lo; if (*x > hi) *x = hi; modified = 1; }
        break;
    case K_INS:
        if (e->npt < 12) {
            int nx = env_pt < e->npt - 1 ? (x[0] + x[2]) / 2 : x[0] + 10, ny = *y;
            if (env_pt < e->npt - 1 && nx <= x[0]) break;    /* no room between the two points */
            memmove(x + 2, x, (e->npt - env_pt) * 2 * sizeof *x);
            x[2] = (short)nx; x[3] = (short)ny;
            e->npt++; env_pt++;
            if (e->sus > env_pt - 1) e->sus++;
            if (e->lps > env_pt - 1) e->lps++;
            if (e->lpe > env_pt - 1) e->lpe++;
            modified = 1;
        }
        break;
    case K_DEL:
        if (e->npt > 2 && env_pt > 0) {
            memmove(x, x + 2, (e->npt - env_pt - 1) * 2 * sizeof *x);
            e->npt--;
            if (e->sus >= env_pt && e->sus) e->sus--;
            if (e->lps >= env_pt && e->lps) e->lps--;
            if (e->lpe >= env_pt && e->lpe) e->lpe--;
            if (env_pt >= e->npt) env_pt = e->npt - 1;
            modified = 1;
        }
        break;
    default:
        switch (c) {
        case 'e': e->flg ^= 1; break;
        case 's': if (e->flg & 2 && e->sus == env_pt) e->flg &= ~2; else { e->flg |= 2; e->sus = env_pt; } break;
        case 'b': e->flg |= 4; e->lps = env_pt; if (e->lpe < env_pt) e->lpe = env_pt; break;
        case 'n': e->flg |= 4; e->lpe = env_pt; if (e->lps > env_pt) e->lps = env_pt; break;
        case 'o': e->flg &= ~4; break;
        default: LeaveCriticalSection(&cs); return k < 128;    /* other letters do nothing here */
        }
        modified = 1;
    }
    LeaveCriticalSection(&cs);
    return 1;
}

#ifdef _WIN32
int core_vk(int vk, int ctrl, int shift)
{
    int k = 0;
    switch (vk) {
    case VK_UP: k = K_UP; break;       case VK_DOWN: k = K_DOWN; break;
    case VK_LEFT: k = K_LEFT; break;   case VK_RIGHT: k = K_RIGHT; break;
    case VK_PRIOR: k = K_PGUP; break;  case VK_NEXT: k = K_PGDN; break;
    case VK_HOME: k = K_HOME; break;   case VK_END: k = K_END; break;
    case VK_RETURN: k = K_ENTER; break; case VK_ESCAPE: k = K_ESC; break;
    case VK_BACK: k = K_BACK; break;   case VK_DELETE: k = K_DEL; break;
    case VK_TAB: k = shift ? K_STAB : K_TAB; break;
    case VK_F1: k = K_F1; break; case VK_F2: k = K_F2; break; case VK_F3: k = K_F3; break;
    case VK_F4: k = K_F4; break; case VK_F5: k = K_F5; break; case VK_F6: k = K_F6; break;
    case VK_F7: k = K_F7; break; case VK_F8: k = K_F8; break; case VK_F9: k = K_F9; break;
    case VK_F10: k = K_F10; break; case VK_INSERT: k = K_INS; break;
    case VK_F12: k = K_F12; break;
    }
    if (k && shift && ((k >= K_UP && k <= K_END) || k == K_ENTER)) k |= K_SHIFT;   /* Shift+movement: block select; Shift+Enter */
    return k && ctrl ? k | K_CTRL : k;
}
#endif

int core_page(void) { return page; }

/* test hook (KOLLAPSE_KEYS): one key token from *s. j/k down/up, E enter, B backspace, M esc,
   #1..#9 F-keys, #e esc, #u/#d ctrl+up/down, #s ctrl+s, #z ctrl+z, #D delete, #t tab, #< #> left/right;
   anything else is typed as itself */
int core_hook_key(const char **s)
{
    int c = *(*s)++;
    if (c != '#') return c == 'j' ? K_DOWN : c == 'k' ? K_UP : c == 'E' ? K_ENTER : c == 'B' ? K_BACK : c == 'M' ? K_ESC : c;
    c = *(*s)++;
    if (c >= '1' && c <= '9') { static const int f[9] = { K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9 }; return f[c - '1']; }
    switch (c) {
    case 'e': return K_ESC;  case 'u': return K_CTRL | K_UP;  case 'd': return K_CTRL | K_DOWN;
    case 's': return 19;     case 'z': return 26;             case 'D': return K_DEL;  case 't': return K_TAB;
    case 'N': return 14;     case 'o': return 15;             case '0': return K_F10;  case 'I': return K_INS;
    case 'c': return 3;      case 'x': return 24;             case 'v': return 22;
    case 'J': return K_DOWN | K_SHIFT;  case 'K': return K_UP | K_SHIFT;
    case 'H': return K_LEFT | K_SHIFT;  case 'L': return K_RIGHT | K_SHIFT;
    case '[': return K_CTRL | K_LEFT;   case ']': return K_CTRL | K_RIGHT;
    case 'T': return 20;     case 'R': return 18;             case 'Y': return 12;
    case 'S': return K_ENTER | K_SHIFT;  case 'X': return 5;  case 'j': return 10;  case 'F': return 6;
    case 'P': return K_CTRL | K_PGDN;   case 'p': return K_CTRL | K_PGUP;
    case '<': return K_LEFT; case '>': return K_RIGHT;  case 'h': return K_HOME;
    }
    return 0;
}

int core_key(int k)
{
    if (k == 17) return 1;                             /* Ctrl+Q */
    if (k == 3) { if (page != P_EDIT) return 1; k = K_CTRL | 'c'; }   /* Ctrl+C: copy in the editor, quit elsewhere */
    if (k == 22 || k == 16) k = K_CTRL | 'v';           /* Ctrl+P too: terminals keep Ctrl+V for their own paste */
    if (k == 24) k = K_CTRL | 'x';
    if (k == 19) k = K_CTRL | 's';                     /* Ctrl+S/Z/N/O arrive as control characters */
    if (k == 26) k = K_CTRL | 'z';
    if (k == 14) k = K_CTRL | 'n';
    if (k == 15) k = K_CTRL | 'o';
    if (k == 20) k = K_CTRL | 't';                     /* Ctrl+T / R / L: sample trim, reverse, normalize */
    if (k == 18) k = K_CTRL | 'r';
    if (k == 12) k = K_CTRL | 'l';
    if (k == 5) k = K_CTRL | 'e';                      /* Ctrl+E: export WAV */
    if (k == 10) k = K_CTRL | 'j';                     /* Ctrl+J: interpolate */
    if (k == 6) k = K_CTRL | 'f';                      /* Ctrl+F: Modland search */
    if (armed && k != armed_key) armed = 0;            /* the "again to discard" warning is for one key only */
    last_key = k;
    if (name_edit) {                                   /* typing a name: Enter / Esc ends it */
        size_t l = strlen(name_edit);
        int setting = name_edit == dl_dir;
        if (k == K_ENTER || k == K_ESC || (!loaded && !setting)) { name_edit = NULL; if (setting) save_settings(); }
        else if (k == K_BACK && l) name_edit[l - 1] = 0;
        else if (k >= 32 && k < 127 && (int)l < name_max) { name_edit[l] = (char)k; name_edit[l + 1] = 0; }
        if (!setting) modified = 1;
        return 0;
    }
    if (overlay == 4) { ml_key(k); return 0; }          /* Modland search */
    if (overlay == 3) {                                /* sample picker */
        switch (k) {
        case K_ESC: case K_CTRL | 'o': overlay = 0; break;
        case K_UP:   if (pick_sel > 0) pick_sel--; break;
        case K_DOWN: if (pick_sel < npick - 1) pick_sel++; break;
        case K_PGUP: pick_sel = pick_sel > 15 ? pick_sel - 15 : 0; break;
        case K_PGDN: pick_sel = pick_sel + 15 < npick ? pick_sel + 15 : npick - 1; break;
        case K_ENTER: pick_enter(0); break;
        case K_INS:   if (pick_sel < npick && !pick_isdir(pick_sel)) pick_enter(1); break;   /* add as an extra sample */
        case K_BACK: if (npick && !strcmp(pick[0], "[..]")) { pick_sel = 0; pick_enter(0); } break;
        }
        return 0;
    }
    if (overlay) {
        int n = overlay == 1 ? N_MENU_ACTIVE : 0;
        switch (k) {
        case K_ESC: case K_F12: overlay = 0; return 0;
        case K_F1: if (overlay == 2) help_page++; else { overlay = 2; help_page = 0; } return 0;   /* F1 again: next page */
        case K_LEFT: case K_PGUP: if (overlay == 2 && help_page > 0) help_page--; return 0;
        case K_RIGHT: case K_PGDN: case ' ': if (overlay == 2) help_page++; return 0;
        case K_UP:   if (overlay != 2 && osel > 0) osel--; return 0;
        case K_DOWN: if (overlay != 2 && osel < n - 1) osel++; return 0;
        case K_ENTER:
            if (overlay == 2) { overlay = 0; return 0; }
            overlay = 0;
            k = menu_keys[osel];
            if (k == 17) return 1;
            break;
        default: return 0;
        }
    }
    int target = k == K_F2 ? P_EDIT : k == K_F3 ? P_SAMPLE : k == K_F4 ? P_INSTR : k == K_F7 ? P_SPECTRUM : k == K_F9 ? P_MESSAGE : k == K_F10 ? P_SONG : -1;
    if (target >= 0) { page = page == target ? P_PLAY : target; text_scroll = 0; return 0; }
    switch (k) {                                       /* everywhere */
    case K_CTRL | 'n': new_song(); return 0;
    case K_CTRL | 'o': pick_open(); return 0;
    case K_CTRL | 'e': export_wav(); return 0;
    case K_CTRL | 'f': ml_show(); return 0;
    case K_CTRL | 'd': zipview[0] = 0; CreateDirectoryA(dl_dir, NULL); page = P_PLAY; enter_dir(dl_dir); return 0;
    case K_CTRL | 'w': { char d[MAX_PATH]; autosave_dir(d, sizeof d); zipview[0] = 0; if (d[0]) { CreateDirectoryA(d, NULL); page = P_PLAY; enter_dir(d); } return 0; }
    case K_CTRL | 's': save_xm(); return 0;
    case K_CTRL | 'z': if (page == P_SAMPLE) sample_op('z'); else do_undo(); return 0;
    case K_CTRL | K_UP:   if (cur_ins > 0) cur_ins--; cur_sub = 0; return 0;
    case K_CTRL | K_DOWN: if (loaded && cur_ins < mi.mod->ins - 1) cur_ins++; cur_sub = 0; return 0;
    case '[': if (page != P_PLAY && octave > 0) octave--; if (page != P_PLAY) return 0; break;
    case ']': if (page != P_PLAY && octave < 8) octave++; if (page != P_PLAY) return 0; break;
    }
    if (k == K_ESC && page != P_PLAY) { page = P_PLAY; return 0; }
    if (page == P_EDIT) {
        if (edit_key(k)) return 0;
        if (k >= 32 && k < 127 && k != ' ' && k != '+' && k != '-') return 0;   /* letters are notes here, not shortcuts */
    } else if (page != P_PLAY) {
        if (page == P_SONG ? song_key(k) : page == P_CONFIG ? cfg_key(k) : page_key(k)) return 0;
        if (k >= 32 && k < 127 && k != ' ' && k != '+' && k != '-') return 0;
    }
    switch (k) {
    case K_F1:  overlay = 2; help_page = 0; break;
    case K_ESC: case K_F12: overlay = 1; osel = 0; break;
    case K_UP:   if (fsel > 0) fsel--; break;
    case K_DOWN: if (fsel < nents - 1) fsel++; break;
    case K_PGUP: fsel = fsel > 10 ? fsel - 10 : 0; break;
    case K_PGDN: fsel = fsel + 10 < nents ? fsel + 10 : nents - 1; break;
    case K_HOME: fsel = 0; break;
    case K_END:  fsel = nents - 1; break;
    case K_ENTER: play_entry(fsel); break;
    case K_BACK: if (nents && !strcmp(ents[0].name, "..")) enter_dir(".."); break;
    case K_F5: if (sel_is_playing() || (loaded && ents[fsel].dir)) restart(); else play_entry(fsel); break;
    case 's': case 'S': toggle_shuffle(); break;
    case 'r': case 'R': repeat = (repeat + 1) % 3; save_settings(); break;
    case 'f': fav_toggle(); break;
    case 'c': case 'C': page = P_CONFIG; break;
    case 'F': fav_open(); break;
    case '[': if (nseq > 1) play_seq(cur_seq > 0 ? cur_seq - 1 : 0); break;
    case ']': if (nseq > 1) play_seq(cur_seq + 1 < nseq ? cur_seq + 1 : cur_seq); break;
    case K_F6: case ' ': pause_toggle(); break;
    case K_F8: stop(); break;
    case K_LEFT:  seek(-1); break;
    case K_RIGHT: seek(1); break;
    case '+': case '=': set_volume(10); break;
    case '-': case '_': set_volume(-10); break;
    case 'n': case 'N': step(1); break;
    case 'p': case 'P': step(-1); break;
    case '<': case ',': if (chan_off > 0) chan_off--; break;
    case '>': case '.': if (loaded && chan_off < mi.mod->chn - 1) chan_off++; break;
    default:
        if (k >= '0' && k <= '9') toggle_mute(k == '0' ? 9 : k - '1');
    }
    if (fsel < 0) fsel = 0;
    return 0;
}

void core_toggle_mute(int ch) { toggle_mute(ch); }

void core_action(int id) { if (id == A_MENU) { overlay = overlay ? 0 : 1; osel = 0; } }

static int *scroll_of(int id) { return id == L_FILES ? &fscroll : &iscroll; }

void core_click_list(int id, int vis_row, int rows)
{
    int i = *scroll_of(id) + vis_row;
    if (id == L_FILES && i < nents) { if (fsel == i) play_entry(i); else fsel = i; }
    if (id == L_INSTR && loaded && i < mi.mod->ins) { cur_ins = i; cur_sub = 0; if (page == P_PLAY) page = P_INSTR; }
}

/* ---------------- queries ---------------- */
static void fmt_time(char *b, int ms) { sprintf(b, "%02d:%02d", ms / 60000, ms / 1000 % 60); }

const char *core_field(int id, char *b)
{
    b[0] = 0;
    switch (id) {
    case F_FILES_HDR: snprintf(b, 64, "%s (%d)", zipview[0] ? base(zipview) : base(dirname)[0] ? base(dirname) : dirname, nmods); return b;
    case F_MODE: snprintf(b, 32, "%s%s", shuffle ? "SHUF " : "", repeat == 1 ? "RPT 1" : repeat == 2 ? "RPT ALL" : ""); return b;
    case F_INSTR_HDR: return "Instruments";
    case F_STATUS:
        if (page == P_EDIT && loaded)
            { snprintf(b, 128, "%s oct %d ins %02d | %s", !rec ? "JAM" : state == 1 ? "REC" : "EDIT", octave, cur_ins + 1, status); return b; }
        return status;
    case F_VOL: sprintf(b, "%d%%", volume); return b;
    case F_HINTS:
        switch (page) {
        case P_EDIT:     return "F2 Back  Enter Play  Shift+Enter Loop  ^R Rec/Jam  \\ Solo  [ ] Oct  Shift+arrows Block  ^C/X/V  ^J Interp  ^Z Undo";
        case P_SAMPLE:   return "F3 Back  Up/Dn Field  Left/Right Change  Tab Sample  Enter Name  Shift+Enter Ins name  ^X ^T ^R ^L Tools  ^Z Undo";
        case P_INSTR:    return "F4 Back  Tab: vol env / pan env / keymap / vibrato  Left/Right Point  Up/Dn Value  Ins/Del  E S B N O  Enter Play";
        case P_MESSAGE:  return "F9 Back  Up/Down PgUp/PgDn Scroll  Esc Back";
        case P_SPECTRUM: return "F7 Back  F5 Play  F6 Pause  N/P Track  Esc Back";
        case P_CONFIG:   return "C or Esc Back  Up/Down Pick  Left/Right Change  Enter Edit folder";
        case P_SONG:     return "F10 Back  Up/Down Select  Left/Right Change  Enter Edit  Ins/Del  N New ptn  C Clone  ^S Save";
        }
        return loaded && nseq > 1 ? "F1 Help  F2 Edit  F3 Smp  F4 Ins  F9 Msg  F5 Play  F6 Pause  F8 Stop  F Fav  [ ] Subsong  ^F Search  Esc Menu"
                                  : "F1 Help  F2 Edit  F3 Smp  F4 Ins  F9 Msg  F5 Play  F6 Pause  F8 Stop  F Fav  C Settings  ^F Search  Esc Menu";
    }
    if (!loaded) return b;
    switch (id) {
    case F_TITLE: return mi.mod->name[0] ? mi.mod->name : base(playing);
    case F_TYPE:  snprintf(b, 64, "%s", mi.mod->type); return b;
    case F_CHANS: sprintf(b, "%d", mi.mod->chn); return b;
    case F_BPM:   sprintf(b, "%d", fi.bpm ? fi.bpm : mi.mod->bpm); return b;     /* before playback: the song's own */
    case F_SPEED: sprintf(b, "%d", fi.speed ? fi.speed : mi.mod->spd); return b;
    case F_ORD:   sprintf(b, "%02X/%02X", fi.pos, mi.mod->len); return b;
    case F_PTN:   sprintf(b, "%02X", view_pat()); return b;
    case F_ROW:   sprintf(b, "%02X", core_row()); return b;
    case F_TIME: { char t1[16], t2[16]; fmt_time(t1, fi.time); fmt_time(t2, fi.total_time); sprintf(b, "%s / %s", t1, t2); return b; }
    }
    return b;
}

const char *core_list(int id, int i, int rows, int *num, int *flags, float *g)
{
    int *scroll = scroll_of(id), n, sel;
    if (id == L_FILES) { n = nents; sel = fsel; }
    else { n = loaded ? mi.mod->ins : 0; sel = page != P_PLAY ? cur_ins : -1; }
    if (sel >= 0) {                                    /* keep selection in view */
        if (sel < *scroll) *scroll = sel;
        if (sel >= *scroll + rows) *scroll = sel - rows + 1;
    } else if (n - rows < *scroll) *scroll = n > rows ? n - rows : 0;
    int k = *scroll + i;
    *flags = 0; *g = 0; *num = k + 1;
    if (k >= n) return NULL;
    if (id == L_FILES) {
        static char nm[PATHLEN];
        if (ents[k].dir) {                                 /* [folder] cyan, [pack.zip] orange */
            snprintf(nm, sizeof nm, "[%s]", ents[k].name);
            *flags = (k == fsel) | 4 | (ents[k].dir == 2) << 3;
            return nm;
        }
        entry_path(k, nm, sizeof nm);
        *flags = (k == fsel) | (loaded && !_stricmp(nm, playing)) << 1;
        static char shown[PATHLEN];
        const char *b = zipview[0] && is_m3u(zipview) ? base(ents[k].name) : ents[k].name;
        if (is_fav(nm)) { snprintf(shown, sizeof shown, "*%s", b); return shown; }
        return b;
    }
    *g = k < 256 ? glow[k] : 0;
    *flags = k == sel;
    return mi.mod->xxi[k].name;
}

float core_progress(void) { return loaded && fi.total_time > 0 ? (float)fi.time / fi.total_time : 0; }

int core_nchan(void) { return loaded ? mi.mod->chn : 0; }
int core_rows(void)  { return pat_rows(view_pat()); }
int core_row(void)   { return page == P_EDIT ? erow : fi.row; }

/* libxmp internal effect -> the letter the format's own tracker shows. Returns 0 if unknown. */
static int fx_s3m(int t, int *p)
{
    switch (t) {
    case 0x0f: case 0xa3: return 'A';            /* FX_SPEED, FX_S3M_SPEED */
    case 0x0b: return 'B';  case 0x0d: case 0x8e: return 'C';
    case 0x0a: return 'D';  case 0x02: return 'E';  case 0x01: return 'F';
    case 0x03: return 'G';  case 0x04: return 'H';  case 0x1d: return 'I';
    case 0xb4: return 'J';  case 0x06: return 'K';  case 0x05: return 'L';
    case 0x80: return 'M';  case 0x81: return 'N';  case 0x09: return 'O';
    case 0x89: return 'P';  case 0x1b: return 'Q';  case 0x07: return 'R';
    case 0xab: case 0x87: return 'T';
    case 0xac: return 'U';  case 0x10: return 'V';  case 0x11: return 'W';
    case 0x08: return 'X';  case 0x8a: return 'Y';  case 0xbe: return 'Z';  case 0xbf: return '\\';
    case 0x0e: {                                  /* FX_EXTENDED: back to Sxy */
        static const char sub[16] = { 0, 0, 0, '1', '3', '2', 'B', '4', 0, 0, 0, 0, 'C', 'D', '6', 0 };
        int s = sub[*p >> 4];
        if (!s) return 0;
        *p = (s <= '9' ? s - '0' : s - 'A' + 10) << 4 | (*p & 15);
        return 'S';
    }
    case 0x8b: *p = 0x50 | *p; return 'S';        /* panbrello waveform  S5x */
    case 0x83: *p = 0x70 | *p; return 'S';        /* instrument function S7x */
    case 0x8d: *p = 0x90 | *p; return 'S';        /* surround            S9x */
    case 0x8f: *p = 0x9e + *p; return 'S';        /* reverse             S9E/F */
    case 0x8c: *p = 0xa0 | *p; return 'S';        /* high offset         SAx */
    case 0x88: *p = 0xe0 | *p; return 'S';        /* row delay           SEx */
    case 0xbd: *p = 0xf0 | *p; return 'S';        /* macro select        SFx */
    }
    return 0;
}
static int fx_xm(int t)
{
    if (t < 16) return "0123456789ABCDEF"[t];
    switch (t) {
    case 0x10: return 'G'; case 0x11: return 'H'; case 0x14: return 'K'; case 0x15: return 'L';
    case 0x19: return 'P'; case 0x1b: return 'R'; case 0x1d: return 'T'; case 0x21: case 0x8d: case 0x8f: return 'X';
    /* other trackers' effects, shown as their nearest XM letter (MED, 669, FAR, ULT, MDL, IMF, DSYM...) */
    case 0xa3: case 0xab: case 0x68: case 0x5f: case 0x7e: return 'F';   /* speed / tempo variants */
    case 0xac: return '4';                                 /* fine vibrato */
    case 0xb4: return '0';                                 /* arpeggio */
    case 0xa1: return 'A';                                 /* volume slide down */
    case 0x8e: return 'D';                                 /* pattern break, hex parameter */
    case 0xa5: case 0x7d: case 0xad: case 0xae: return 'A';   /* fine / period volume slides */
    case 0x61: case 0x78: return '2';                      /* 669 / period portamento down */
    case 0x7a: return '3';                                 /* period-based tone portamento */
    case 0x93: return 'R';                                 /* MED retrigger */
    }
    return 0;
}

/* volume-column command (libxmp keeps it in f2t/f2p) -> 2 chars: FT2 symbols for XM, IT letters for IT.
   \x18/\x19/\x1a/\x1b are up/down/right/left arrows, drawn by both renderers. Returns 0 if none. */
static int vol_fx(int t, int p, char out[3])
{
    static const char hex[] = "0123456789ABCDEF";
    char c = 0; int v = 0;
    if (!t) return 0;
    if (s3m_style) {
        switch (t) {
        case 0xc2: c = 'A'; v = p; break;           /* fine volume up */
        case 0xc3: c = 'B'; v = p; break;           /* fine volume down */
        case 0xc0: c = 'C'; v = p; break;           /* volume slide up */
        case 0xc1: c = 'D'; v = p; break;           /* volume slide down */
        case 0x02: c = 'E'; v = p >> 2; break;      /* pitch slide down */
        case 0x01: c = 'F'; v = p >> 2; break;      /* pitch slide up */
        case 0x08: c = 'P'; v = p >> 4; break;      /* panning (0-F) */
        case 0x04: c = 'H'; v = p; break;           /* vibrato depth */
        case 0x03: {                                /* portamento: undo IT's speed table */
            static const int g[10] = { 0x00, 0x01, 0x04, 0x08, 0x10, 0x20, 0x40, 0x60, 0x80, 0xff };
            c = 'G';
            for (int i = 0; i < 10; i++) if (g[i] == p) v = i;
            break;
        }
        }
    } else {
        switch (t) {
        case 0xa4: c = p & 0xf0 ? '+' : '-'; v = p & 0xf0 ? p >> 4 : p; break;   /* volume slide */
        case 0x0e: if (p >> 4 == 0xa) c = '\x18'; else if (p >> 4 == 0xb) c = '\x19'; v = p; break;  /* fine slide */
        case 0x04: c = p & 0xf0 ? 'S' : 'V'; v = p & 0xf0 ? p >> 4 : p; break;   /* vibrato speed / depth */
        case 0x08: c = 'P'; v = p >> 4; break;                                    /* panning */
        case 0xb5: c = p & 0xf0 ? '\x1b' : '\x1a'; v = p & 0xf0 ? p >> 4 : p; break; /* pan slide left/right */
        case 0x03: c = 'M'; v = p >> 4; break;                                    /* tone portamento */
        default:                                                                  /* a second effect column (669, FAR, IMF, GDM...): */
            if ((c = (char)fx_xm(t))) v = p > 15 ? p >> 4 : p;                    /* its letter and the leading digit */
        }
    }
    if (!c) return 0;
    out[0] = c; out[1] = hex[v & 15]; out[2] = 0;
    return 1;
}

void core_cell(int row, int ch, char out[14])
{
    static const char *nn[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    struct xmp_event *e = ev_at(view_pat(), row, ch);
    char note[4], ins[3], vol[3], fx[4];
    if (!e) { strcpy(out, "--- .. .. ..."); return; }
    if (e->note == XMP_KEY_OFF)       strcpy(note, "===");
    else if (e->note == XMP_KEY_CUT)  strcpy(note, "^^^");
    else if (e->note == XMP_KEY_FADE) strcpy(note, "~~~");
    else if (e->note && e->note < 128) sprintf(note, "%s%d", nn[(e->note - 1) % 12], (e->note - 1) / 12 % 10);
    else strcpy(note, "---");
    if (e->ins) sprintf(ins, "%02d", e->ins % 100); else strcpy(ins, "..");
    if (e->vol) sprintf(vol, "%02d", (e->vol - 1) % 100);
    else if (!vol_fx(e->f2t, e->f2p, vol)) strcpy(vol, "..");
    if (e->fxt || e->fxp) {
        int p = e->fxp, c = s3m_style ? fx_s3m(e->fxt, &p) : fx_xm(e->fxt);
        if (c) sprintf(fx, "%c%02X", c, p & 255); else sprintf(fx, "?%02X", e->fxp);
    } else strcpy(fx, "...");
    sprintf(out, "%s %s %s %s", note, ins, vol, fx);
}

void core_chan(int ch, int *pan, float *vol, int *m)
{
    struct xmp_channel_info *ci = &fi.channel_info[ch];
    *pan = loaded ? ci->pan : 128;
    *vol = loaded && state == 1 ? ci->volume / 64.0f : 0;
    if (*vol > 1) *vol = 1;
    *m = muted[ch];
}

/* real waveform: ~12 ms of the channel's sample from its current play position */
float core_scope(int ch, float t)
{
    if (!loaded || state != 1 || muted[ch]) return 0;
    struct xmp_channel_info *ci = &fi.channel_info[ch];
    if (!ci->volume || ci->sample >= mi.mod->smp || !ci->period) return 0;
    struct xmp_sample *s = &mi.mod->xxs[ci->sample];
    if (!s->data || s->len <= 0) return 0;
    double span = 428.0 * 8363.0 * 0.012 /(ci->period / 4096.0);   /* source samples across the scope */
    long p = (long)ci->position + (long)(t * span);
    if (p >= s->len) {
        if (s->flg & XMP_SAMPLE_LOOP && s->lpe > s->lps) p = s->lps + (p - s->lps) % (s->lpe - s->lps);
        else return 0;
    }
    float v = s->flg & XMP_SAMPLE_16BIT ? ((short *)s->data)[p] / 32768.0f : ((signed char *)s->data)[p] / 128.0f;
    return v * ci->volume / 64.0f;
}

int core_pick_width(int avail, int per_char, int pad, int *vis)
{
    int n = core_nchan() ? core_nchan() : 4;
    if (page == P_EDIT) { *vis = avail / (cell_widths[0] * per_char + pad); if (*vis > n) *vis = n; return 0; }  /* editor: full cells, scroll */
    for (int w = 0; w < 4; w++)
        if (n * (cell_widths[w] * per_char + pad) <= avail) { *vis = n; return w; }
    *vis = avail / (3 * per_char + pad);
    return 3;
}

int core_chan_off(int vis)
{
    int n = core_nchan();
    if (page == P_EDIT && vis > 0) {                   /* keep the cursor's channel on screen */
        if (ech < chan_off) chan_off = ech;
        if (ech >= chan_off + vis) chan_off = ech - vis + 1;
    }
    if (chan_off + vis > n) chan_off = n > vis ? n - vis : 0;
    return chan_off;
}

float core_level(int ch) { return ch >= 0 && ch < XMP_MAX_CHANNELS ? (level[ch] > 1 ? 1 : level[ch]) : 0; }

void core_title(char *b, int n)
{
    if (!loaded) { snprintf(b, n, "Kollapse"); return; }
    snprintf(b, n, "%s%s%s - Kollapse", modified ? "* " : "", state == 2 ? "[paused] " : state == 0 ? "[stopped] " : "",
             mi.mod->name[0] ? mi.mod->name : base(playing));
}

int core_cursor(int *row, int *ch, int *off, int *len)
{
    static const int o[8] = { 0, 4, 5, 7, 8, 10, 11, 12 };
    if (page != P_EDIT || !loaded) return 0;
    *row = erow; *ch = ech; *off = o[ecol]; *len = ecol ? 1 : 3;
    return 1;
}

/* text for the message / sample / instrument pages, built on demand */
static char tbuf[65536];
static const char *tl[2048];
static int tn, tpos;
static void tadd(const char *fmt, ...)
{
    va_list ap;
    if (tn >= 2048 || tpos >= (int)sizeof tbuf - 256) return;
    va_start(ap, fmt);
    int w = vsnprintf(tbuf + tpos, sizeof tbuf - tpos, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    if (tpos + w >= (int)sizeof tbuf) w = (int)sizeof tbuf - tpos - 1;
    tl[tn++] = tbuf + tpos;
    tpos += w + 1;
}
static const char *note_name(int n, char *b)
{
    static const char *nn[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    if (n < 0 || n > 119) return strcpy(b, "---");        /* b holds 8 */
    snprintf(b, 8, "%s%d", nn[n % 12], n / 12);
    return b;
}
static const struct xmp_sample *cur_sample(int *sid)
{
    if (!loaded || cur_ins >= mi.mod->ins) return NULL;
    struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    if (cur_sub >= xi->nsm) return NULL;
    int s = xi->sub[cur_sub].sid;
    if (s < 0 || s >= mi.mod->smp) return NULL;
    if (sid) *sid = s;
    return &mi.mod->xxs[s];
}

static int text_sel = -1;
int core_text_sel(void) { return text_sel; }

const char **core_text(int rows, int *n, int *first)
{
    tn = tpos = 0;
    *first = 0;
    text_sel = -1;
    if (page == P_CONFIG) { cfg_text(rows, first); *n = tn; return tl; }   /* settings need no song */
    if (!loaded) { tadd("pick a track, press Enter"); *n = tn; return tl; }
    struct xmp_module *m = mi.mod;
    if (page == P_MESSAGE) {
        tadd("%s", m->name[0] ? m->name : base(playing));
        tadd("%s - %d channels, %d patterns, %d orders, %d instruments, %d samples", m->type, m->chn, m->pat, m->len, m->ins, m->smp);
        if (mi.comment && mi.comment[0]) {
            tadd(""); tadd("-- song message --");
            char line[256]; int L = 0;
            for (const char *c = mi.comment; ; c++) {
                if (!*c || *c == '\r' || *c == '\n' || L >= 120) {
                    line[L] = 0; tadd("%s", line); L = 0;
                    if (!*c) break;
                    if (*c == '\r' && c[1] == '\n') c++;
                    if (*c != '\r' && *c != '\n') line[L++] = *c;
                } else line[L++] = *c;
            }
        }
        tadd(""); tadd("-- instruments --");
        for (int i = 0; i < m->ins; i++) tadd("%02d %s", i + 1, m->xxi[i].name);
        tadd(""); tadd("-- samples --");
        for (int i = 0; i < m->smp; i++)
            tadd("%02d %-24s %8d %s%s", i + 1, m->xxs[i].name, m->xxs[i].len,
                 m->xxs[i].flg & XMP_SAMPLE_16BIT ? "16-bit" : "8-bit", m->xxs[i].flg & XMP_SAMPLE_LOOP ? " loop" : "");
        if (text_scroll > tn - rows) text_scroll = tn - rows > 0 ? tn - rows : 0;
        *first = text_scroll;
    } else if (page == P_CONFIG) {
        cfg_text(rows, first);
    } else if (page == P_SONG) {
        song_text(rows, first);
        text_sel = song_sel < NSET ? song_sel : song_sel + 1;
    } else if (page == P_SAMPLE || page == P_INSTR) {
        struct xmp_instrument *xi = cur_ins < m->ins ? &m->xxi[cur_ins] : NULL;
        if (!xi) { tadd("no instruments"); *n = tn; return tl; }
        if (page == P_SAMPLE) {
            int sid = -1; const struct xmp_sample *s = cur_sample(&sid);
            const struct xmp_subinstrument *su = cur_sub < xi->nsm ? &xi->sub[cur_sub] : NULL;
            tadd("Instrument %02d %s%s  sample %d/%d  %s%s  (%d frames, %s)", cur_ins + 1, xi->name, name_edit == xi->name ? "_" : "",
                 xi->nsm ? cur_sub + 1 : 0, xi->nsm, s ? s->name : "(none)", s && name_edit == s->name ? "_" : "",
                 s ? s->len : 0, s && s->flg & XMP_SAMPLE_16BIT ? "16-bit" : "8-bit");
            if (s && su) {                                 /* the editable fields; ↑/↓ picks one */
                int v[7] = { 0, s->lps, s->lpe, su->vol, su->fin, su->xpo, su->pan };
                for (int f = 0; f < 7; f++)
                    if (f == 0) tadd("%-11s %s", sfield_name[0], s->flg & XMP_SAMPLE_LOOP ? (s->flg & XMP_SAMPLE_LOOP_BIDIR ? "pingpong" : "forward") : "off");
                    else tadd("%-11s %d", sfield_name[f], v[f]);
                if (srange_b > srange_a) { tadd("%-11s %d", sfield_name[7], srange_a); tadd("%-11s %d   (Ctrl+X cut  Ctrl+T trim  Ctrl+R reverse  Ctrl+L normalize  Ctrl+Z undo)", sfield_name[8], srange_b); }
                else { tadd("%-11s whole sample", sfield_name[7]); tadd("%-11s whole sample   (Ctrl+X cut  Ctrl+T trim  Ctrl+R reverse  Ctrl+L normalize  Ctrl+Z undo)", sfield_name[8]); }
                text_sel = 1 + sfield;
            } else tadd("no sample here (Ctrl+O loads a WAV)");
        } else {
            if (env_which == 2) { keymap_text(rows, first); *n = tn; return tl; }
            if (env_which == 3) { vibrato_text(); *n = tn; return tl; }
            tadd("Instrument %02d %-22s  %d sample%s  fadeout %d  (, . to change)", cur_ins + 1, xi->name, xi->nsm, xi->nsm == 1 ? "" : "s", xi->rls);
            const struct xmp_envelope *ev[2] = { &xi->aei, &xi->pei }, *e = ev[env_which];
            char line[256]; int L = 0;
            if (e->npt > 0 && e->npt <= 12 && env_pt < e->npt)
                tadd("%s envelope: %s  point %d/%d  time %d  value %d%s%s", env_which ? "Panning" : "Volume", e->flg & 1 ? "on" : "off",
                     env_pt + 1, e->npt, e->data[env_pt * 2], e->data[env_pt * 2 + 1],
                     e->flg & 2 ? (e->sus == env_pt ? "  [sustain]" : "  sustain set") : "",
                     e->flg & 4 ? (env_pt >= e->lps && env_pt <= e->lpe ? "  [in loop]" : "  loop set") : "");
            else tadd("%s envelope: none (Ins or E creates one)", env_which ? "Panning" : "Volume");
            tadd("Tab vol/pan  </> point  Up/Dn value  Shift+</> time  Ins/Del point  E on  S sustain  B/N loop  O no loop  Enter play");
            (void)L; (void)line;
            /* keymap: runs of notes that use the same sample */
            L = snprintf(line, sizeof line, "keymap:");
            for (int a = 0; a < 120 && xi->nsm > 1; ) {
                int sv = xi->map[a].ins, b2 = a;
                while (b2 + 1 < 120 && xi->map[b2 + 1].ins == sv) b2++;
                char n1[8], n2[8];
                if (sv < xi->nsm) {
                    if (L > 90) { tadd("%s", line); L = snprintf(line, sizeof line, "       "); }
                    L += snprintf(line + L, sizeof line - L, " %s..%s>%d", note_name(a, n1), note_name(b2, n2), sv + 1);
                }
                a = b2 + 1;
            }
            if (xi->nsm > 1) tadd("%s", line);
        }
    }
    *n = tn;
    return tl;
}

int core_wave(int col, int ncols, float *lo, float *hi)
{
    const struct xmp_sample *s = cur_sample(NULL);
    *lo = *hi = 0;
    if (!s || !s->data || s->len <= 0 || ncols <= 0) return 0;
    long a = (long)((double)col * s->len / ncols), b = (long)((double)(col + 1) * s->len / ncols);
    if (b <= a) b = a + 1;
    float mn = 1, mx = -1;
    for (long i = a; i < b && i < s->len; i++) {
        float v = s->flg & XMP_SAMPLE_16BIT ? ((const short *)s->data)[i] / 32768.0f : ((const signed char *)s->data)[i] / 128.0f;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    *lo = mn; *hi = mx;
    return 1;
}

void core_wave_marks(float *ls, float *le, float *pos)
{
    int sid = -1; const struct xmp_sample *s = cur_sample(&sid);
    *ls = *le = *pos = -1;
    if (!s || s->len <= 0) return;
    if (s->flg & XMP_SAMPLE_LOOP && s->lpe > s->lps) { *ls = (float)s->lps / s->len; *le = (float)s->lpe / s->len; }
    if (state == 1)
        for (int c = 0; c < mi.mod->chn; c++)
            if (fi.channel_info[c].sample == sid && fi.channel_info[c].volume) { *pos = (float)fi.channel_info[c].position / s->len; break; }
}

int core_env(int which, float *xs, float *ys, int max, int *sus, int *lps, int *lpe, int *on)
{
    *sus = *lps = *lpe = -1; *on = 0;
    if (!loaded || cur_ins >= mi.mod->ins) return 0;
    const struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    const struct xmp_envelope *e = which ? &xi->pei : &xi->aei;
    int n = e->npt < max ? e->npt : max;
    if (n <= 0) return 0;
    float xmax = e->data[(n - 1) * 2] > 0 ? e->data[(n - 1) * 2] : 1;
    for (int i = 0; i < n; i++) { xs[i] = e->data[i * 2] / xmax; ys[i] = e->data[i * 2 + 1] / 64.0f; }
    *on = e->flg & 1;
    if (e->flg & 2) *sus = e->sus;
    if (e->flg & 4) { *lps = e->lps; *lpe = e->lpe; }
    return n;
}

void core_spectrum(float *bars, float *peaks, int n)
{
    for (int i = 0; i < n; i++) { int k = i * 64 / (n > 0 ? n : 1); bars[i] = spec[k]; peaks[i] = peak[k]; }
}

/* Ctrl+S: write the song as XM next to the original as <name>-kollapse.xm (never over the original) */
static void save_xm(void)
{
    if (!loaded) return;
    char out[PATHLEN], nm[PATHLEN];
    snprintf(out, sizeof out, "%s", playing);
    char *bar = strchr(out, '|');
    if (bar) *bar = 0;                                     /* inside a zip: save next to the zip */
    char *slash = strrchr(out, SEP);
    if (slash) slash[1] = 0; else out[0] = 0;
    snprintf(nm, sizeof nm, "%s", base(playing));
    char *dot = strrchr(nm, '.');
    if (dot) *dot = 0;
    size_t l = strlen(nm);
    if (!(l >= 9 && !_stricmp(nm + l - 9, "-kollapse"))) strcat(nm, "-kollapse");
    strncat(out, nm, sizeof out - strlen(out) - 4);
    strcat(out, ".xm");
    int linear = !memcmp(orig_head, "Extended Module:", 16) ? orig_head[74] & 1 : is_it;
    EnterCriticalSection(&cs);
    int r = xm_save(mi.mod, out, linear, !memcmp(orig_head, "Extended Module:", 16));
    LeaveCriticalSection(&cs);
    snprintf(status, sizeof status, r ? "save failed: %s" : "saved %s", base(out));
    if (!r) { modified = 0; armed = 0; autosave_done(); }
    if (!r && !zipview[0]) {                               /* show it in the list if we're in that folder */
        char d[PATHLEN]; snprintf(d, sizeof d, "%s%s", dirname, is_root_path(dirname) ? "" : SEPS);
        if (!_strnicmp(d, out, strlen(d))) {
            char keep[128]; snprintf(keep, sizeof keep, "%s", status);
            scan();
            for (int i = 0; i < nents; i++) if (!_stricmp(ents[i].name, base(out))) fsel = i;
            snprintf(status, sizeof status, "%s", keep);
        }
    }
}

/* ---------------- structural edits ----------------
   libxmp can't grow a song in place: build a modified copy, write it as XM, reload */
static int rb_resize_pat = -1, rb_rows;                    /* rebuild: give this pattern a new length */
static void *owned[4096];
static int nowned;
static void *own(void *p) { if (p && nowned < 4096) owned[nowned++] = p; return p; }
static void free_owned(void) { while (nowned) free(owned[--nowned]); }

static struct xmp_track *new_track(int rows)
{
    struct xmp_track *t = own(calloc(1, sizeof *t + sizeof(struct xmp_event) * (rows > 1 ? rows - 1 : 0)));
    if (t) t->rows = rows;
    return t;
}
static struct xmp_pattern *new_pattern(int rows, int chn)
{
    struct xmp_pattern *p = own(calloc(1, sizeof *p + sizeof(int) * (chn > 1 ? chn - 1 : 0)));
    if (p) p->rows = rows;
    return p;
}

static int song_linear(void) { return !memcmp(orig_head, "Extended Module:", 16) ? orig_head[74] & 1 : (is_it || !loaded); }

/* write m as XM to a temp file, read it back, hand it to libxmp */
static int write_reload(const struct xmp_module *m, int linear, int from_xm, const char *path, int keep_view)
{
    char dir[MAX_PATH], tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    GetTempFileNameA(dir, "klp", 0, tmp);
    EnterCriticalSection(&cs);
    int r = xm_save(m, tmp, linear, from_xm);
    LeaveCriticalSection(&cs);
    size_t n = 0;
    void *buf = r ? NULL : read_file(tmp, &n);
    DeleteFileA(tmp);
    free_owned();
    if (!buf) { snprintf(status, sizeof status, "edit failed (couldn't write the song)"); return 0; }
    char keep[PATHLEN]; snprintf(keep, sizeof keep, "%s", path);
    int ok = load_mem(buf, n, keep, keep_view);
    free(buf);
    return ok;
}

/* rebuild: channel count (0 keep), add a pattern (1 empty, 2 clone of src) after ord_at,
   sample ns into instrument slot ins */
static int rebuild(int chn, int addpat, int src, int ord_at, int ins, const struct xmp_sample *ns, int xpo, int fin, int append)
{
    if (!loaded) return 0;
    struct xmp_module *m = mi.mod, m2 = *m;
    int from_xm = !memcmp(orig_head, "Extended Module:", 16), rp = m->pat - (from_xm && m->pat > 1);
    if (!chn) chn = m->chn;
    if (chn > 32) chn = 32;
    int np = rp + (addpat ? 1 : 0);
    if (np > 256) { snprintf(status, sizeof status, "XM allows 256 patterns"); return 0; }
    snapshot_push();                                       /* undo point: the whole song as it is now */
    m2.xxp = own(calloc(np, sizeof *m2.xxp));
    m2.xxt = own(calloc(m->trk + (size_t)np * chn + 1, sizeof *m2.xxt));
    if (!m2.xxp || !m2.xxt) { free_owned(); return 0; }
    for (int i = 0; i < m->trk; i++) m2.xxt[i] = m->xxt[i];
    int nt = m->trk;
    for (int p = 0; p < rp; p++) {
        int old = m->xxp[p]->rows, rows = p == rb_resize_pat ? rb_rows : old;
        if (chn == m->chn && rows == old) { m2.xxp[p] = m->xxp[p]; continue; }
        m2.xxp[p] = new_pattern(rows, chn);                /* channel count or length changed: new index table */
        for (int c = 0; c < chn; c++)
            if (c < m->chn && rows == old) m2.xxp[p]->index[c] = m->xxp[p]->index[c];
            else {                                         /* new track; a resized one keeps what still fits */
                struct xmp_track *t = new_track(rows);
                if (t && c < m->chn) memcpy(t->event, m->xxt[m->xxp[p]->index[c]]->event, sizeof(struct xmp_event) * (rows < old ? rows : old));
                m2.xxt[nt] = t; m2.xxp[p]->index[c] = nt++;
            }
    }
    if (addpat) {
        int rows = addpat == 2 && src < rp ? m->xxp[src]->rows : 64;
        m2.xxp[rp] = new_pattern(rows, chn);
        for (int c = 0; c < chn; c++) {
            struct xmp_track *t = new_track(rows);
            if (addpat == 2 && src < rp && c < m->chn) memcpy(t->event, m->xxt[m->xxp[src]->index[c]]->event, sizeof(struct xmp_event) * rows);
            m2.xxt[nt] = t; m2.xxp[rp]->index[c] = nt++;
        }
        int len = 0;                                       /* drop orders that pointed at libxmp's blank pattern */
        for (int i = 0; i < m2.len; i++) if (m2.xxo[i] < rp) m2.xxo[len++] = m2.xxo[i];
        m2.len = len;
        if (m2.len < 255) {
            int at = ord_at + 1 < 0 ? 0 : ord_at + 1 > m2.len ? m2.len : ord_at + 1;
            memmove(m2.xxo + at + 1, m2.xxo + at, m2.len - at);
            m2.xxo[at] = (unsigned char)rp; m2.len++;
        }
    }
    m2.pat = np; m2.trk = nt; m2.chn = chn;
    if (ins >= 0 && ns) {
        int nins = m->ins > ins + 1 ? m->ins : ins + 1;
        m2.xxi = own(calloc(nins, sizeof *m2.xxi));
        m2.xxs = own(calloc(m->smp + 1, sizeof *m2.xxs));
        if (!m2.xxi || !m2.xxs) { free_owned(); return 0; }
        for (int i = 0; i < m->ins; i++) m2.xxi[i] = m->xxi[i];
        for (int i = 0; i < m->smp; i++) m2.xxs[i] = m->xxs[i];
        m2.xxs[m->smp] = *ns;
        struct xmp_instrument *xi = &m2.xxi[ins];
        if (append && ins < m->ins && xi->nsm > 0) {       /* one more sample in this instrument; keymap unchanged */
            if (xi->nsm >= 16) { free_owned(); snprintf(status, sizeof status, "XM instruments hold 16 samples"); return 0; }
            struct xmp_subinstrument *sub = own(calloc(xi->nsm + 1, sizeof *sub));
            if (!sub) { free_owned(); return 0; }
            memcpy(sub, xi->sub, xi->nsm * sizeof *sub);
            sub[xi->nsm] = sub[0];                         /* vibrato etc. like the first sample */
            sub[xi->nsm].vol = 64; sub[xi->nsm].pan = 128; sub[xi->nsm].xpo = xpo; sub[xi->nsm].fin = fin; sub[xi->nsm].sid = m->smp;
            xi->sub = sub; xi->nsm++;
            m2.ins = nins; m2.smp = m->smp + 1;
            int ok = write_reload(&m2, song_linear(), 0, playing, 1);
            if (ok) modified = 1;
            return ok;
        }
        char keepname[32]; snprintf(keepname, sizeof keepname, "%s", ins < m->ins && xi->name[0] ? xi->name : ns->name);
        memset(xi, 0, sizeof *xi);
        snprintf(xi->name, sizeof xi->name, "%s", keepname);
        xi->vol = 64; xi->nsm = 1;
        xi->sub = own(calloc(1, sizeof *xi->sub));
        if (!xi->sub) { free_owned(); return 0; }
        xi->sub[0].vol = 64; xi->sub[0].gvl = 64; xi->sub[0].pan = 128;
        xi->sub[0].xpo = xpo; xi->sub[0].fin = fin; xi->sub[0].sid = m->smp;
        m2.ins = nins; m2.smp = m->smp + 1;
    }
    int ok = write_reload(&m2, song_linear(), 0, playing, 1);
    if (ok) modified = 1;
    return ok;
}

/* Ctrl+N: an empty XM - 8 channels, one 64-row pattern, 8 empty instrument slots */
static void new_song(void)
{
    if (!discard_ok()) return;
    struct xmp_module m;
    memset(&m, 0, sizeof m);
    snprintf(m.name, sizeof m.name, "untitled");
    m.chn = 8; m.pat = 1; m.trk = 8; m.ins = 8; m.smp = 0; m.spd = 6; m.bpm = 125; m.len = 1; m.gvl = 64;
    m.xxp = own(calloc(1, sizeof *m.xxp));
    m.xxt = own(calloc(8, sizeof *m.xxt));
    m.xxi = own(calloc(8, sizeof *m.xxi));
    if (!m.xxp || !m.xxt || !m.xxi) { free_owned(); return; }
    m.xxp[0] = new_pattern(64, 8);
    for (int c = 0; c < 8; c++) { m.xxt[c] = new_track(64); m.xxp[0]->index[c] = c; }
    char path[PATHLEN]; snprintf(path, sizeof path, "%s" SEPS "untitled.xm", dirname);
    if (write_reload(&m, 1, 0, path, 0)) {
        stop();
        page = P_EDIT; erow = ech = ecol = epat = cur_ins = 0;
        snprintf(status, sizeof status, "new song - Ctrl+O loads a sample into instrument 01");
    }
}

/* WAV -> 16-bit mono. 8/16/24/32-bit PCM and 32-bit float, any channel count, WAVE_FORMAT_EXTENSIBLE */
static short *load_wav(const char *path, int *frames, int *rate)
{
    size_t n; unsigned char *b = read_file(path, &n);
    short *out = NULL;
    if (!b) return NULL;
    if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); return NULL; }
    int fmt = 0, ch = 0, bits = 0; const unsigned char *data = NULL; size_t dlen = 0;
    for (size_t o = 12; o + 8 <= n; ) {
        size_t sz = b[o + 4] | b[o + 5] << 8 | b[o + 6] << 16 | (size_t)b[o + 7] << 24;
        const unsigned char *c = b + o + 8;
        if (!memcmp(b + o, "fmt ", 4) && sz >= 16 && o + 8 + 16 <= n) {
            fmt = c[0] | c[1] << 8; ch = c[2] | c[3] << 8; *rate = c[4] | c[5] << 8 | c[6] << 16 | c[7] << 24; bits = c[14] | c[15] << 8;
            if (fmt == 0xFFFE && sz >= 26 && o + 8 + 26 <= n) fmt = c[24] | c[25] << 8;   /* extensible: sub-format */
        } else if (!memcmp(b + o, "data", 4)) { data = c; dlen = sz > n - (o + 8) ? n - (o + 8) : sz; }
        o += 8 + sz + (sz & 1);
    }
    int bps = bits / 8;
    if (data && ch > 0 && bps > 0 && (fmt == 1 || (fmt == 3 && bits == 32)) && *rate > 0) {
        long fr = (long)(dlen / (size_t)(ch * bps));
        if (fr > (1 << 22)) fr = 1 << 22;                  /* ~95 s at 44.1 kHz */
        out = malloc(sizeof(short) * (fr ? fr : 1));
        for (long i = 0; out && i < fr; i++) {
            double acc = 0;
            for (int k = 0; k < ch; k++) {
                const unsigned char *s = data + ((size_t)i * ch + k) * bps;
                double v;
                if (fmt == 3) { float f; memcpy(&f, s, 4); v = f * 32767.0; }
                else if (bps == 1) v = (s[0] - 128) * 256.0;
                else if (bps == 2) v = (short)(s[0] | s[1] << 8);
                else if (bps == 3) v = (int)((unsigned)(s[0] << 8 | s[1] << 16 | s[2] << 24)) / 65536.0;
                else v = (int)((unsigned)(s[0] | s[1] << 8 | s[2] << 16 | (unsigned)s[3] << 24)) / 65536.0;
                acc += v;
            }
            acc /= ch;
            out[i] = (short)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
        }
        *frames = (int)fr;
    }
    free(b);
    return out;
}

/* put a WAV into instrument slot `ins` (pitch from its sample rate: C-5 plays it at its own speed) */
static int load_sample_into(const char *path, int ins, int append)
{
    int frames = 0, rate = 0;
    short *d = load_wav(path, &frames, &rate);
    if (!d || frames <= 0) { free(d); snprintf(status, sizeof status, "can't read %s (needs PCM or float WAV)", base(path)); return 0; }
    struct xmp_sample s;
    memset(&s, 0, sizeof s);
    snprintf(s.name, sizeof s.name, "%.22s", base(path));
    char *dot = strrchr(s.name, '.'); if (dot) *dot = 0;
    s.len = frames; s.flg = XMP_SAMPLE_16BIT; s.data = (unsigned char *)own(d);
    double semis = 12 * log2((double)rate / 8363.0);
    int xpo = (int)floor(semis + 0.5), fin = (int)floor((semis - xpo) * 128 + 0.5);
    if (rebuild(0, 0, 0, -1, ins, &s, xpo, fin, append)) {
        if (append) snprintf(status, sizeof status, "added %s to instrument %02d as sample %d - map it on F4 (keymap)", base(path), ins + 1, mi.mod->xxi[ins].nsm);
        else snprintf(status, sizeof status, "loaded %s into instrument %02d", base(path), ins + 1);
        return 1;
    }
    return 0;
}

/* ---------------- sample picker (Ctrl+O): folders and .wav files, its own folder ---------------- */
static char pick_dir[MAX_PATH];
static char *pick[1024];
static int npick, pick_sel;
static int pick_isdir(int i) { return pick[i][0] == '['; }
static int cmp_str(const void *a, const void *b)
{
    const char *x = *(char **)a, *y = *(char **)b;
    if ((x[0] == '[') != (y[0] == '[')) return x[0] == '[' ? -1 : 1;   /* folders first */
    return _stricmp(x, y);
}
static void pick_scan(void)
{
    for (int i = 0; i < npick; i++) free(pick[i]);
    npick = pick_sel = 0;
    if (strlen(pick_dir) > 3) pick[npick++] = _strdup("[..]");
    char pat[MAX_PATH + 4]; snprintf(pat, sizeof pat, "%s" SEPS "*", pick_dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int first = npick;
    do {
        if (fd.cFileName[0] == '.' || fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) || npick >= 1024) continue;
        char b[MAX_PATH + 2];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) snprintf(b, sizeof b, "[%s]", fd.cFileName);
        else if (has_ext(fd.cFileName, ".wav.")) snprintf(b, sizeof b, "%s", fd.cFileName);
        else continue;
        pick[npick++] = _strdup(b);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(pick + first, npick - first, sizeof *pick, cmp_str);
}
static void pick_open(void)
{
    if (!loaded) { snprintf(status, sizeof status, "load a song or Ctrl+N first"); return; }
    if (!pick_dir[0]) snprintf(pick_dir, sizeof pick_dir, "%s", dirname);
    pick_scan();
    overlay = 3;
}
static void pick_enter(int append)
{
    if (pick_sel >= npick) return;
    if (pick_isdir(pick_sel)) {
        char name[MAX_PATH]; snprintf(name, sizeof name, "%.*s", (int)strlen(pick[pick_sel]) - 2, pick[pick_sel] + 1);
        if (!strcmp(name, "..")) {                         /* up one level; "C:" -> "C:\\", "" -> "/" */
            char *s = strrchr(pick_dir, SEP);
            if (s) { *s = 0; if (strlen(pick_dir) == 2 || !pick_dir[0]) strcat(pick_dir, SEPS); }
        }
        else { size_t l = strlen(pick_dir); snprintf(pick_dir + l, sizeof pick_dir - l, "%s%s", pick_dir[l - 1] == SEP ? "" : SEPS, name); }
        pick_scan();
        return;
    }
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s%s%s", pick_dir, pick_dir[strlen(pick_dir) - 1] == SEP ? "" : SEPS, pick[pick_sel]);
    overlay = 0;
    load_sample_into(path, cur_ins, append);
}

/* ---------------- song page (F10): title, BPM, speed, channels, order list ---------------- */
static int song_sel;
static int real_pats(void) { return loaded ? mi.mod->pat - (!memcmp(orig_head, "Extended Module:", 16) && mi.mod->pat > 1) : 0; }


static char autosave_path[MAX_PATH];
static int song_key(int k)                                 /* 1 = handled */
{
    struct xmp_module *m = mi.mod;
    if (!loaded) return 0;
    int items = NSET + m->len, pos = song_sel - NSET, hex = -1, c = k < 128 ? tolower(k) : 0;
    if (c >= '0' && c <= '9') hex = c - '0'; else if (c >= 'a' && c <= 'f') hex = c - 'a' + 10;
    switch (k) {
    case K_UP:   if (song_sel > 0) song_sel--; return 1;
    case K_DOWN: if (song_sel < items - 1) song_sel++; return 1;
    case K_PGUP: song_sel = song_sel > 10 ? song_sel - 10 : 0; return 1;
    case K_PGDN: song_sel = song_sel + 10 < items ? song_sel + 10 : items - 1; return 1;
    case K_ENTER:
        if (song_sel == 0) {                               /* edit the title (default title starts empty) */
            snapshot_push(); snap_key = -1;
            name_edit = m->name; name_max = 20; if (!strcmp(m->name, "untitled")) m->name[0] = 0;
        } else if (pos >= 0 && m->xxo[pos] < m->pat) { epat = m->xxo[pos]; erow = 0; page = P_EDIT; }
        return 1;
    case K_LEFT: case K_RIGHT: case K_LEFT | K_SHIFT: case K_RIGHT | K_SHIFT: {
        int d = ((k & ~K_SHIFT) == K_RIGHT ? 1 : -1) * (k & K_SHIFT ? 16 : 1);
        if (song_sel == 1) { int v = m->bpm + d; if (v >= 32 && v <= 255) { snap_coalesce((6 << 24) | 1); m->bpm = v; modified = 1; } }
        else if (song_sel == 2) { int v = m->spd + d; if (v >= 1 && v <= 31) { snap_coalesce((6 << 24) | 2); m->spd = v; modified = 1; } }
        else if (song_sel == 3) { int v = m->chn + d; if (v >= 1 && v <= 32) rebuild(v, 0, 0, -1, -1, NULL, 0, 0, 0); }
        else if (song_sel == 4 && epat < real_pats()) {    /* length of the pattern open in the editor */
            int v = m->xxp[epat]->rows + d;
            if (v < 1) v = 1;
            if (v > 256) v = 256;
            if (v != m->xxp[epat]->rows) {
                rb_resize_pat = epat; rb_rows = v;
                if (rebuild(0, 0, 0, -1, -1, NULL, 0, 0, 0)) snprintf(status, sizeof status, "pattern %02X is %d rows", epat, v);
                rb_resize_pat = -1;
            }
        }
        return 1;
    }
    }
    if (pos < 0) return 0;
    if (k == K_INS && m->len < 255) {                      /* duplicate this entry */
        snapshot_push(); snap_key = -1;
        EnterCriticalSection(&cs);
        memmove(m->xxo + pos + 1, m->xxo + pos, m->len - pos); m->len++;
        LeaveCriticalSection(&cs);
        song_sel++; modified = 1; return 1;
    }
    if (k == K_DEL && m->len > 1) {
        snapshot_push(); snap_key = -1;
        EnterCriticalSection(&cs);
        memmove(m->xxo + pos, m->xxo + pos + 1, m->len - pos - 1); m->len--;
        LeaveCriticalSection(&cs);
        if (song_sel >= NSET + m->len) song_sel--;
        modified = 1; return 1;
    }
    if (c == 'n' || c == 'c') {                             /* new empty / cloned pattern after this position */
        int src = m->xxo[pos], np = real_pats();
        if (rebuild(0, c == 'n' ? 1 : 2, src, pos, -1, NULL, 0, 0, 0)) {
            song_sel = NSET + pos + 1;
            snprintf(status, sizeof status, c == 'n' ? "new pattern %02X" : "pattern %02X cloned to %02X", c == 'n' ? np : src, np);
        }
        return 1;
    }
    if (hex >= 0) {                                        /* type the pattern number (two hex digits) */
        snap_coalesce((7 << 24) | pos);
        int v = ((m->xxo[pos] & 15) << 4) | hex;
        if (v >= real_pats()) v = hex < real_pats() ? hex : m->xxo[pos];
        m->xxo[pos] = (unsigned char)v; modified = 1;
        return 1;
    }
    return 0;
}

static void song_text(int rows, int *first)
{
    struct xmp_module *m = mi.mod;
    tadd("Title     %s%s", m->name, name_edit == m->name ? "_" : "");
    tadd("BPM       %d", m->bpm);
    tadd("Speed     %d", m->spd);
    tadd("Channels  %d", m->chn);
    tadd("Rows      %d   (pattern %02X, the one open in the editor; Shift: 16 at a time)", epat < m->pat ? m->xxp[epat]->rows : 0, epat);
    tadd("-- order list (%d patterns): Ins dup  Del remove  N new  C clone  0-F number  Enter edit --", real_pats());
    for (int i = 0; i < m->len; i++)
        if (m->xxo[i] == 0xfe) tadd("%03d   +++", i);
        else if (m->xxo[i] == 0xff) tadd("%03d   ---", i);
        else tadd("%03d   %02X%s", i, m->xxo[i], loaded && fi.pos == i && state == 1 ? "  <" : "");
    if (autosave_path[0]) { tadd(""); tadd("autosave (every minute while there are unsaved edits): %s", autosave_path); }
    int sel = song_sel < NSET ? song_sel : song_sel + 1;
    *first = sel < rows / 2 ? 0 : sel - rows / 2;
    if (*first > tn - rows) *first = tn - rows > 0 ? tn - rows : 0;
}

int core_env_sel(int which) { return page == P_INSTR && which == env_which ? env_pt : -1; }

/* ---------------- waveform editing (F3) ----------------
   the length changes, so the edited sample goes through the same XM rebuild */
static int srange_a, srange_b;                             /* range on the sample page; b <= a means the whole sample */

static int replace_sample(int sid, const struct xmp_sample *ns)
{
    snapshot_push();                                       /* undo point */
    struct xmp_module *m = mi.mod, m2 = *m;
    m2.xxs = own(calloc(m->smp, sizeof *m2.xxs));
    if (!m2.xxs) { free_owned(); return 0; }
    for (int i = 0; i < m->smp; i++) m2.xxs[i] = m->xxs[i];
    m2.xxs[sid] = *ns;
    int ok = write_reload(&m2, song_linear(), !memcmp(orig_head, "Extended Module:", 16), playing, 1);
    if (ok) modified = 1;
    return ok;
}

/* op: 'x' cut the range, 't' trim to it, 'r' reverse it, 'l' normalize it, 'z' undo the last op */
static void sample_op(int op)
{
    int sid = -1;
    const struct xmp_sample *s = cur_sample(&sid);
    if (op == 'z') { do_undo(); return; }                 /* the shared history (snapshots) */
    if (!s || !s->data || s->len <= 0) { snprintf(status, sizeof status, "no sample here"); return; }
    int len = s->len, a = srange_a, b = srange_b;
    if (b <= a || b > len) { a = 0; b = len; }
    short *src = malloc(sizeof(short) * len);              /* work in 16-bit */
    if (!src) return;
    for (int i = 0; i < len; i++) src[i] = s->flg & XMP_SAMPLE_16BIT ? ((short *)s->data)[i] : (short)(((signed char *)s->data)[i] * 256);
    int nlen = len, lps = s->lps, lpe = s->lpe, loop = (s->flg & XMP_SAMPLE_LOOP) && s->lpe > s->lps;
    short *out = NULL;
    if (op == 'x' || op == 't') {
        nlen = op == 'x' ? len - (b - a) : b - a;
        if (nlen < 2) { free(src); snprintf(status, sizeof status, "that would leave no sample"); return; }
        out = malloc(sizeof(short) * nlen);
        if (!out) { free(src); return; }
        if (op == 'x') { memcpy(out, src, sizeof(short) * a); memcpy(out + a, src + b, sizeof(short) * (len - b)); }
        else memcpy(out, src + a, sizeof(short) * nlen);
        /* keep the loop where it was in the sound, as far as it survives */
        if (op == 'x') {
            lps = lps >= b ? lps - (b - a) : lps > a ? a : lps;
            lpe = lpe >= b ? lpe - (b - a) : lpe > a ? a : lpe;
        } else { lps -= a; lpe -= a; }
        if (lps < 0) lps = 0;
        if (lpe > nlen) lpe = nlen;
        if (lpe <= lps) loop = 0;
    } else {
        out = src; src = NULL;
        if (op == 'r') for (int i = a, j = b - 1; i < j; i++, j--) { short t = out[i]; out[i] = out[j]; out[j] = t; }
        else {
            int peak = 1;
            for (int i = a; i < b; i++) { int v = out[i] < 0 ? -out[i] : out[i]; if (v > peak) peak = v; }
            for (int i = a; i < b; i++) { long v = (long)out[i] * 32767 / peak; out[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
        }
    }
    free(src);
    struct xmp_sample ns = *s;
    ns.data = own(out); ns.len = nlen; ns.flg = (s->flg | XMP_SAMPLE_16BIT) & ~(loop ? 0 : XMP_SAMPLE_LOOP | XMP_SAMPLE_LOOP_BIDIR);
    ns.lps = loop ? lps : 0; ns.lpe = loop ? lpe : 0;
    static const char *what[] = { "cut", "trimmed", "reversed", "normalized" };
    int w = op == 'x' ? 0 : op == 't' ? 1 : op == 'r' ? 2 : 3;
    if (replace_sample(sid, &ns)) {
        srange_a = srange_b = 0;
        snprintf(status, sizeof status, "%s %d frames - Ctrl+Z undoes", what[w], op == 't' ? nlen : b - a);
    }
}

void core_wave_range(float *a, float *b)
{
    const struct xmp_sample *s = cur_sample(NULL);
    *a = *b = -1;
    if (!s || s->len <= 0 || srange_b <= srange_a) return;
    *a = (float)srange_a / s->len; *b = (float)srange_b / s->len;
}

/* ---------------- F4 keymap and vibrato views ---------------- */
static int km_note = 48, vib_field;                       /* keymap cursor (map index 12..107 = XM's 96 keys) */
static const char *vib_name[4] = { "Vibrato type", "Sweep", "Depth", "Rate" };
static const char *vib_type[4] = { "sine", "square", "ramp down", "ramp up" };

static int keymap_key(int k)
{
    struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    int c = k < 128 ? tolower(k) : 0;
    switch (k) {
    case K_UP:   if (km_note > 12) km_note--; return 1;
    case K_DOWN: if (km_note < 107) km_note++; return 1;
    case K_PGUP: km_note = km_note - 12 < 12 ? 12 : km_note - 12; return 1;
    case K_PGDN: km_note = km_note + 12 > 107 ? 107 : km_note + 12; return 1;
    case K_ENTER: preview(cur_ins, km_note + 1); return 1;
    case K_LEFT: case K_RIGHT:
        if (xi->nsm > 1) {
            snap_coalesce((4 << 24) | cur_ins << 12 | km_note);   /* undo point */
            int v = xi->map[km_note].ins >= xi->nsm ? 0 : xi->map[km_note].ins;
            v = (v + (k == K_RIGHT ? 1 : xi->nsm - 1)) % xi->nsm;
            xi->map[km_note].ins = (unsigned char)v; modified = 1;
        }
        return 1;
    }
    if (c == 'f') {                                        /* this note's sample for every key above it */
        snapshot_push(); snap_key = -1;
        for (int j = km_note + 1; j < 120; j++) xi->map[j].ins = xi->map[km_note].ins;
        modified = 1;
        snprintf(status, sizeof status, "keys above filled with sample %d", xi->map[km_note].ins + 1);
        return 1;
    }
    return k < 128;
}

static int vibrato_key(int k)
{
    struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    int d = k == K_RIGHT ? 1 : k == K_LEFT ? -1 : k == K_PGUP ? 8 : k == K_PGDN ? -8 : 0;
    if (k == K_UP) { if (vib_field > 0) vib_field--; return 1; }
    if (k == K_DOWN) { if (vib_field < 3) vib_field++; return 1; }
    if (k == K_ENTER) { preview(cur_ins, 61); return 1; }
    if (!d || !xi->nsm) return k < 128 || d;
    snap_coalesce((5 << 24) | cur_ins << 12 | vib_field);   /* undo point */
    struct xmp_subinstrument s0 = xi->sub[0];              /* XM keeps one vibrato per instrument: set all samples */
    int v;
    switch (vib_field) {
    case 0: v = (s0.vwf + (d > 0 ? 1 : 3)) % 4; for (int i = 0; i < xi->nsm; i++) xi->sub[i].vwf = v; break;
    case 1: v = s0.vsw + d; v = v < 0 ? 0 : v > 255 ? 255 : v; for (int i = 0; i < xi->nsm; i++) xi->sub[i].vsw = v; break;
    case 2: v = (s0.vde >> 2) + d; v = v < 0 ? 0 : v > 15 ? 15 : v; for (int i = 0; i < xi->nsm; i++) xi->sub[i].vde = v << 2; break;
    case 3: v = s0.vra + d; v = v < 0 ? 0 : v > 63 ? 63 : v; for (int i = 0; i < xi->nsm; i++) xi->sub[i].vra = v; break;
    }
    modified = 1;
    return 1;
}

static void keymap_text(int rows, int *first)
{
    struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    tadd("Keymap - which sample each key plays (%d sample%s; Ctrl+O then Ins adds one)", xi->nsm, xi->nsm == 1 ? "" : "s");
    tadd("Up/Dn key  PgUp/Dn octave  Left/Right sample  F fill keys above  Enter play  Tab next view");
    for (int j = 12; j <= 107; j++) {
        char nb[8]; int s = xi->map[j].ins;
        const char *sn = s < xi->nsm && xi->sub[s].sid >= 0 && xi->sub[s].sid < mi.mod->smp ? mi.mod->xxs[xi->sub[s].sid].name : "";
        if (s < xi->nsm) tadd("%-4s  sample %2d  %s", note_name(j, nb), s + 1, sn);
        else tadd("%-4s  (none)", note_name(j, nb));
    }
    text_sel = 2 + (km_note - 12);
    *first = text_sel - rows / 2 < 0 ? 0 : text_sel - rows / 2;
    if (*first > tn - rows) *first = tn - rows > 0 ? tn - rows : 0;
}

static void vibrato_text(void)
{
    struct xmp_instrument *xi = &mi.mod->xxi[cur_ins];
    tadd("Auto-vibrato (whole instrument)   Up/Dn field  Left/Right change  PgUp/Dn big  Enter play  Tab next view");
    if (!xi->nsm) { tadd("no samples in this instrument"); return; }
    const struct xmp_subinstrument *s = &xi->sub[0];
    tadd("%-13s %s", vib_name[0], vib_type[s->vwf & 3]);
    tadd("%-13s %d", vib_name[1], s->vsw);
    tadd("%-13s %d", vib_name[2], s->vde >> 2);
    tadd("%-13s %d", vib_name[3], s->vra);
    text_sel = 1 + vib_field;
}

int core_instr_view(void) { return env_which; }

/* ---------------- the song as XM bytes, autosave, WAV export ---------------- */
static void *song_bytes(size_t *n)
{
    char dir[MAX_PATH], tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    GetTempFileNameA(dir, "klp", 0, tmp);
    EnterCriticalSection(&cs);
    int r = xm_save(mi.mod, tmp, song_linear(), !memcmp(orig_head, "Extended Module:", 16));
    LeaveCriticalSection(&cs);
    void *buf = r ? NULL : read_file(tmp, n);
    DeleteFileA(tmp);
    return buf;
}

/* <folder of the song>\<name>-kollapse<ext> (inside a zip: next to the zip) - same rule as Ctrl+S */
static void out_path(char *out, size_t n, const char *ext)
{
    char nm[PATHLEN];
    snprintf(out, n, "%s", playing);
    char *bar = strchr(out, '|');
    if (bar) *bar = 0;
    char *slash = strrchr(out, SEP);
    if (slash) slash[1] = 0; else out[0] = 0;
    snprintf(nm, sizeof nm, "%s", base(playing));
    char *dot = strrchr(nm, '.');
    if (dot) *dot = 0;
    size_t l = strlen(nm);
    if (!(l >= 9 && !_stricmp(nm + l - 9, "-kollapse"))) strcat(nm, "-kollapse");
    size_t ol = strlen(out);
    snprintf(out + ol, n - ol, "%s%s", nm, ext);
}

/* autosave: %APPDATA%\kollapse\autosave\<name>.xm, written every minute while there are unsaved edits */
static DWORD last_autosave;
static void autosave_name(char *out, size_t n)
{
    char app[MAX_PATH], nm[PATHLEN];
    out[0] = 0;
    if (!GetEnvironmentVariableA("APPDATA", app, MAX_PATH) || !playing[0]) return;
    snprintf(nm, sizeof nm, "%s", base(playing));
    char *dot = strrchr(nm, '.');
    if (dot) *dot = 0;
    snprintf(out, n, "%s" SEPS "kollapse" SEPS "autosave" SEPS "%s.xm", app, nm);
}
static void autosave_dir(char *out, size_t n)
{
    char app[MAX_PATH];
    out[0] = 0;
    if (GetEnvironmentVariableA("APPDATA", app, MAX_PATH)) snprintf(out, n, "%s" SEPS "kollapse" SEPS "autosave", app);
}
static void autosave_now(void)
{
    char dir[MAX_PATH], app[MAX_PATH];
    if (!loaded || !GetEnvironmentVariableA("APPDATA", app, MAX_PATH)) return;
    snprintf(dir, sizeof dir, "%s" SEPS "kollapse", app); CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s" SEPS "kollapse" SEPS "autosave", app); CreateDirectoryA(dir, NULL);
    autosave_name(autosave_path, sizeof autosave_path);
    EnterCriticalSection(&cs);
    xm_save(mi.mod, autosave_path, song_linear(), !memcmp(orig_head, "Extended Module:", 16));
    LeaveCriticalSection(&cs);
}
static void autosave_tick(void)
{
    if (!modified) { last_autosave = GetTickCount(); return; }
    DWORD every = getenv("KOLLAPSE_AUTOSAVE_MS") ? (DWORD)atoi(getenv("KOLLAPSE_AUTOSAVE_MS")) : (DWORD)autosave_min * 60000;   /* test hook */
    if (!every) return;                                    /* autosave off */
    if (GetTickCount() - last_autosave >= every) { autosave_now(); last_autosave = GetTickCount(); }
}
static void autosave_done(void)                            /* saved for real: the autosave isn't needed */
{
    char p[MAX_PATH];
    autosave_name(p, sizeof p);
    if (p[0]) DeleteFileA(p);
    autosave_path[0] = 0;
}
/* when a song is opened: is there an autosave of it that is newer than the file? */
static void autosave_check(void)
{
    char p[MAX_PATH]; WIN32_FILE_ATTRIBUTE_DATA a, b;
    autosave_name(p, sizeof p);
    if (!p[0] || !GetFileAttributesExA(p, GetFileExInfoStandard, &a)) return;
    char f[PATHLEN]; snprintf(f, sizeof f, "%s", playing);
    char *bar = strchr(f, '|'); if (bar) *bar = 0;
    if (GetFileAttributesExA(f, GetFileExInfoStandard, &b) && CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) <= 0) return;
    snprintf(status, sizeof status, "newer autosave: Menu > Autosave folder");
}

/* Ctrl+E: render the song (with all edits) to <name>-kollapse.wav in the background */
static volatile int exporting;
static volatile float export_prog;
static volatile int export_done;                           /* 1 ok, -1 failed: core_update reports it */
static char export_path[PATHLEN];
static void *export_buf; static size_t export_len; static int export_secs;

static DWORD WINAPI export_thread(LPVOID arg)
{
    xmp_context c = xmp_create_context();
    int ok = 0; unsigned long bytes = 0;
    FILE *f = fopen(export_path, "wb");
    if (f && xmp_load_module_from_memory(c, export_buf, (long)export_len) == 0 && xmp_start_player(c, RATE, 0) == 0) {
        static short buf[4096 * 2];
        struct xmp_frame_info inf;
        unsigned char h[44] = { 0 };
        xmp_set_player(c, XMP_PLAYER_INTERP, XMP_INTERP_SPLINE);
        fwrite(h, 1, 44, f);                               /* header filled in at the end */
        float g0 = 1;                                      /* the same ending as playback: fade the last fade_secs */
        while (xmp_play_buffer(c, buf, sizeof buf, 1) == 0 && bytes < (unsigned long)RATE * 4 * 3600) {
            xmp_get_frame_info(c, &inf);
            int fade_ms = fade_secs * 1000;
            float g1 = 1;
            if (fade_secs > 0 && inf.total_time > 2 * fade_ms && inf.time >= inf.total_time - fade_ms) {
                g1 = (float)(inf.total_time - inf.time) / fade_ms;
                g1 = g1 < 0 ? 0 : g1 * g1;
            }
            if (fade_secs > 0 && inf.loop_count > 0) g1 = 0;  /* this buffer reached the loop point (the clock already wrapped) */
            if (g0 < 1 || g1 < 1)                          /* ramp within the buffer: no steps */
                for (int k = 0; k < 4096; k++) { float g = g0 + (g1 - g0) * k / 4096; buf[k * 2] = (short)(buf[k * 2] * g); buf[k * 2 + 1] = (short)(buf[k * 2 + 1] * g); }
            g0 = g1;
            fwrite(buf, 1, sizeof buf, f); bytes += sizeof buf;
            export_prog = inf.total_time > 0 ? (float)inf.time / inf.total_time : 0;
        }
        unsigned long riff = 36 + bytes, rate = RATE, brate = RATE * 4;
        memcpy(h, "RIFF", 4); memcpy(h + 4, &riff, 4); memcpy(h + 8, "WAVEfmt ", 8);
        h[16] = 16; h[20] = 1; h[22] = 2; memcpy(h + 24, &rate, 4); memcpy(h + 28, &brate, 4); h[32] = 4; h[34] = 16;
        memcpy(h + 36, "data", 4); memcpy(h + 40, &bytes, 4);
        fseek(f, 0, SEEK_SET); fwrite(h, 1, 44, f);
        ok = !ferror(f);
        xmp_end_player(c); xmp_release_module(c);
    }
    if (f && fclose(f)) ok = 0;
    xmp_free_context(c);
    free(export_buf); export_buf = NULL;
    export_secs = (int)(bytes / (RATE * 4));
    export_done = ok ? 1 : -1;
    exporting = 0;
    return 0;
}
static void export_wav(void)
{
    if (!loaded) return;
    if (exporting) { snprintf(status, sizeof status, "already exporting"); return; }
    export_buf = song_bytes(&export_len);                  /* a copy: you can keep editing meanwhile */
    if (!export_buf) { snprintf(status, sizeof status, "export failed (couldn't copy the song)"); return; }
    out_path(export_path, sizeof export_path, ".wav");
    export_prog = 0; export_done = 0; exporting = 1;
    CreateThread(NULL, 0, export_thread, NULL, 0, NULL);
}
static void export_tick(void)
{
    if (exporting) snprintf(status, sizeof status, "exporting %s ... %d%%", base(export_path), (int)(export_prog * 100));
    else if (export_done) {
        if (export_done > 0) snprintf(status, sizeof status, "exported %s (%d:%02d)", base(export_path), export_secs / 60, export_secs % 60);
        else snprintf(status, sizeof status, "export failed: %s", base(export_path));
        export_done = 0;
    }
}

/* ---------------- Modland search (Ctrl+F): type, pick, Enter downloads and plays ---------------- */
#define ML_MAX 1000
static char ml_q[64];
static int ml_res[ML_MAX], ml_nres, ml_sel, ml_last_state = -1, ml_busy;
static char ml_cache[MAX_PATH];

static void ml_run(void) { ml_nres = ml_search(ml_q, ml_res, ML_MAX); ml_sel = 0; }

static void ml_show(void)
{
    char app[MAX_PATH];
    if (!ml_cache[0] && GetEnvironmentVariableA("APPDATA", app, MAX_PATH)) {
        snprintf(ml_cache, sizeof ml_cache, "%s" SEPS "kollapse", app); CreateDirectoryA(ml_cache, NULL);
        snprintf(ml_cache, sizeof ml_cache, "%s" SEPS "kollapse" SEPS "modland.txt", app);
        DeleteFileA(ml_cache);                             /* v1 cache: only MOD/XM/S3M/IT */
        snprintf(ml_cache, sizeof ml_cache, "%s" SEPS "kollapse" SEPS "modland2.txt", app);   /* v2: every format libxmp plays */
    }
    overlay = 4;
    ml_open(ml_cache);
}

static int ml_key(int k)
{
    size_t l = strlen(ml_q);
    switch (k) {
    case K_ESC: case K_CTRL | 'f': case K_F12: overlay = 0; return 1;
    case K_UP:   if (ml_sel > 0) ml_sel--; return 1;
    case K_DOWN: if (ml_sel < ml_nres - 1) ml_sel++; return 1;
    case K_PGUP: ml_sel = ml_sel > 15 ? ml_sel - 15 : 0; return 1;
    case K_PGDN: ml_sel = ml_sel + 15 < ml_nres ? ml_sel + 15 : (ml_nres ? ml_nres - 1 : 0); return 1;
    case K_BACK: if (l) { ml_q[l - 1] = 0; ml_run(); } return 1;
    case K_CTRL | 'r': ml_refresh(ml_cache); return 1;
    case K_ENTER:
        if (ml_sel < ml_nres && ml_fetch(ml_res[ml_sel], dl_dir)) ml_busy = 1;
        else if (ml_busy) snprintf(status, sizeof status, "still downloading the last one");
        return 1;
    }
    if (k >= 32 && k < 127 && l < sizeof ml_q - 1) { ml_q[l] = (char)k; ml_q[l + 1] = 0; ml_run(); }
    return 1;
}

static void ml_tick(void)
{
    float pr;
    int st = ml_state(&pr);
    if (st != ml_last_state) {                             /* the index just arrived (or failed) */
        if (st == ML_READY && ml_q[0]) ml_run();
        if (st == ML_FAILED) snprintf(status, sizeof status, "Modland index: %s", ml_error());
        ml_last_state = st;
    }
    if (!ml_busy) return;
    char path[MAX_PATH];
    int fs = ml_fetch_state(&pr, path, sizeof path);
    if (fs == 1) { snprintf(status, sizeof status, "downloading ... %d%%", (int)(pr * 100)); return; }
    ml_busy = 0;
    if (fs == -1) { snprintf(status, sizeof status, "download failed: %s", ml_error()); return; }
    if (fs != 2) return;
    char dir[MAX_PATH], name[MAX_PATH];                    /* open its folder, play it there */
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, SEP);
    if (!sl) return;
    snprintf(name, sizeof name, "%s", sl + 1); *sl = 0;
    overlay = 0; zipview[0] = 0; page = P_PLAY;
    enter_dir(dir);
    for (int i = 0; i < nents; i++) if (!ents[i].dir && !_stricmp(ents[i].name, name)) { fsel = i; play_entry(i); return; }
    snprintf(status, sizeof status, "downloaded %s", name);
}

static const char **ml_overlay(int *n, int *sel, const char **title)
{
    static char ttl[96], lines[16][80];
    static const char *ptr[16];
    float pr;
    int st = ml_state(&pr);
    *sel = -1; *n = 0;
    for (int i = 0; i < 16; i++) ptr[i] = lines[i];
    if (st == ML_INDEX && !ml_count()) {
        snprintf(ttl, sizeof ttl, "Search Modland");
        snprintf(lines[0], 80, " fetching the Modland index (once, ~6 MB) ... %d%%", (int)(pr * 100));
        *n = 1; *title = ttl; return ptr;
    }
    if (st == ML_FAILED) {
        snprintf(ttl, sizeof ttl, "Search Modland");
        snprintf(lines[0], 80, " couldn't get the index: %.50s", ml_error());
        snprintf(lines[1], 80, " Ctrl+R tries again, Esc closes");
        *n = 2; *title = ttl; return ptr;
    }
    snprintf(ttl, sizeof ttl, "Modland: %s_   %s%d found%s", ml_q, ml_nres >= ML_MAX ? "first " : "", ml_nres, st == ML_INDEX ? "  (updating index)" : "");
    *title = ttl;
    if (!ml_q[0]) {
        snprintf(lines[0], 80, " type an artist or title (%d modules)", ml_count());
        snprintf(lines[1], 80, " Enter downloads + plays, Ctrl+R new index, Esc closes");
        *n = 2; return ptr;
    }
    if (!ml_nres) { snprintf(lines[0], 80, " nothing matches"); *n = 1; return ptr; }
    int first = ml_sel < 8 ? 0 : ml_sel - 8;
    if (first > ml_nres - 16) first = ml_nres - 16 > 0 ? ml_nres - 16 : 0;
    for (int i = 0; i < 16 && first + i < ml_nres; i++) {
        char b[256];
        ml_label(ml_res[first + i], b, sizeof b);
        snprintf(lines[i], 80, "%.76s", b);
        (*n)++;
    }
    *sel = ml_sel - first;
    return ptr;
}

/* ---------------- favorites: %APPDATA%\kollapse\favorites.m3u, one full path per line ---------------- */
static char *favs[4096];
static int nfav, fav_loaded;
static char fav_file[MAX_PATH];

static void fav_load(void)
{
    char app[MAX_PATH];
    if (fav_loaded) return;
    fav_loaded = 1;
    if (!GetEnvironmentVariableA("APPDATA", app, MAX_PATH)) return;
    snprintf(fav_file, sizeof fav_file, "%s" SEPS "kollapse", app); CreateDirectoryA(fav_file, NULL);
    snprintf(fav_file, sizeof fav_file, "%s" SEPS "kollapse" SEPS "favorites.m3u", app);
    size_t n; char *t = read_file(fav_file, &n);
    for (char *ln = t ? strtok(t, "\r\n") : NULL; ln && nfav < 4096; ln = strtok(NULL, "\r\n"))
        if (*ln && *ln != '#') favs[nfav++] = _strdup(ln);
    free(t);
}
static int is_fav(const char *path)
{
    fav_load();
    for (int i = 0; i < nfav; i++) if (!_stricmp(favs[i], path)) return 1;
    return 0;
}
static void fav_save(void)
{
    char tmp[MAX_PATH];
    if (!fav_file[0]) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", fav_file);
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(status, sizeof status, "can't write favorites"); return; }
    fprintf(f, "#EXTM3U\r\n");
    for (int i = 0; i < nfav; i++) fprintf(f, "%s\r\n", favs[i]);
    if (fclose(f) == 0) MoveFileExA(tmp, fav_file, MOVEFILE_REPLACE_EXISTING); else DeleteFileA(tmp);
}
/* F: the selected track (or the playing one, when a folder is selected) in or out of favorites */
static void fav_toggle(void)
{
    char p[PATHLEN];
    fav_load();
    if (fsel < nents && !ents[fsel].dir) entry_path(fsel, p, sizeof p);
    else if (loaded) snprintf(p, sizeof p, "%s", playing);
    else return;
    for (int i = 0; i < nfav; i++)
        if (!_stricmp(favs[i], p)) {
            free(favs[i]); memmove(favs + i, favs + i + 1, (nfav - i - 1) * sizeof *favs); nfav--;
            fav_save();
            snprintf(status, sizeof status, "removed from favorites: %s", base(p));
            if (zipview[0] && !_stricmp(zipview, fav_file)) { int keep = fsel; scan(); fsel = keep < nents ? keep : nents - 1; }
            return;
        }
    if (nfav >= 4096) return;
    favs[nfav++] = _strdup(p);
    fav_save();
    snprintf(status, sizeof status, "* added to favorites: %s (Shift+F opens them)", base(p));
}
static void fav_open(void)
{
    fav_load();
    if (!fav_file[0]) return;
    if (GetFileAttributesA(fav_file) == INVALID_FILE_ATTRIBUTES) fav_save();   /* an empty list to open */
    page = P_PLAY;
    snprintf(zipview, sizeof zipview, "%s", fav_file);
    scan();
    if (nfav) snprintf(status, sizeof status, "%d favorite%s - Enter plays, F removes", nfav, nfav == 1 ? "" : "s");
    else snprintf(status, sizeof status, "no favorites yet - F stars the selected track");
}

/* ---------------- Settings page (C): every option, saved to kollapse.ini as soon as it changes ---------------- */
enum { S_VOL, S_SHUF, S_REP, S_FADE, S_SKIP, S_SUBS, S_THEME, S_FIT, S_INTERP, S_STEREO, S_AUTOSAVE, S_DL, S_N };
static int cfg_sel;
static const char *cfg_help[S_N] = {
    "How loud Kollapse plays (also + and - on the player).",
    "Play the folder in random order, each track once per round (also S).",
    "Off stops at the end, Track loops this song, All starts over (also R).",
    "Fade out this long before a song loops. Off stops right at the loop.",
    "Skip to the next track after this much silence (not while muting).",
    "Files with several songs (game music, demos) play them all, one by one.",
    "Colours of the whole screen.",
    "How the picture fills the window, or the terminal in pixel mode.",
    "Sample smoothing: nearest is gritty like old hardware, spline cleanest.",
    "100% is the hard Amiga left/right split; lower is easier on headphones.",
    "Save a copy of unsaved edits this often (Menu > Autosave folder).",
    "Where Modland downloads go (Enter: type a folder, Enter again: keep it).",
};

static void cfg_apply_player(void)
{
    EnterCriticalSection(&cs);
    if (loaded) { xmp_set_player(ctx, XMP_PLAYER_INTERP, interp); xmp_set_player(ctx, XMP_PLAYER_MIX, stereo); }
    LeaveCriticalSection(&cs);
}

static int cfg_key(int k)
{
    int d = k == K_RIGHT ? 1 : k == K_LEFT ? -1 : 0;
    switch (k) {
    case K_UP:   if (cfg_sel > 0) cfg_sel--; return 1;
    case K_DOWN: if (cfg_sel < S_N - 1) cfg_sel++; return 1;
    case K_HOME: cfg_sel = 0; return 1;
    case K_END:  cfg_sel = S_N - 1; return 1;
    case K_ENTER:
        if (cfg_sel == S_DL) { name_edit = dl_dir; name_max = MAX_PATH - 1; return 1; }
        d = 1; break;                                      /* Enter steps the other settings forward */
    case 'c': case 'C': page = P_PLAY; return 1;
    }
    if (!d) return k < 128;
    switch (cfg_sel) {
    case S_VOL:      set_volume(d * 10); return 1;         /* saves itself */
    case S_SHUF:     toggle_shuffle(); return 1;
    case S_REP:      repeat = (repeat + 3 + d) % 3; break;
    case S_FADE:     fade_secs += d * (fade_secs + d > 10 ? 2 : 1); if (fade_secs < 0) fade_secs = 0; if (fade_secs > 30) fade_secs = 30; break;
    case S_SKIP:     skip_secs += d; if (skip_secs < 0) skip_secs = 0; if (skip_secs > 10) skip_secs = 10; break;
    case S_SUBS:     subsongs = !subsongs; break;
    case S_THEME:    theme = (theme + NTHEMES + d) % NTHEMES; apply_theme(); break;
    case S_FIT:      pixel_fit = (pixel_fit + 3 + d) % 3; break;
    case S_INTERP:   interp = (interp + 3 + d) % 3; cfg_apply_player(); break;
    case S_STEREO:   stereo += d * 10; if (stereo < 0) stereo = 0; if (stereo > 100) stereo = 100; cfg_apply_player(); break;
    case S_AUTOSAVE: { static const int v[] = { 0, 1, 2, 5, 10 }; int i = 0; while (i < 4 && v[i] < autosave_min) i++;
                       i = (i + 5 + d) % 5; autosave_min = v[i]; break; }
    case S_DL:       return 1;
    }
    save_settings();
    return 1;
}

static void cfg_text(int rows, int *first)
{
    static const char *rep_s[3] = { "off", "track", "all" }, *int_s[3] = { "nearest", "linear", "spline" };
    char v[S_N][MAX_PATH + 8];
    snprintf(v[S_VOL], sizeof v[0], "%d%%", volume);
    snprintf(v[S_SHUF], sizeof v[0], "%s", shuffle ? "on" : "off");
    snprintf(v[S_REP], sizeof v[0], "%s", rep_s[repeat % 3]);
    if (fade_secs) snprintf(v[S_FADE], sizeof v[0], "%d s", fade_secs); else snprintf(v[S_FADE], sizeof v[0], "off (stop at the loop)");
    if (skip_secs) snprintf(v[S_SKIP], sizeof v[0], "after %d s", skip_secs); else snprintf(v[S_SKIP], sizeof v[0], "off");
    snprintf(v[S_SUBS], sizeof v[0], "%s", subsongs ? "play all" : "first only");
    snprintf(v[S_THEME], sizeof v[0], "%s   (%d of %d)", themes[theme].name, theme + 1, NTHEMES);
    static const char *fit_s[3] = { "fill", "keep the shape", "crisp" };
    snprintf(v[S_FIT], sizeof v[0], "%s%s", fit_s[pixel_fit % 3], core_pixel_mode ? "" : "   (pixel graphics only)");
    snprintf(v[S_INTERP], sizeof v[0], "%s", int_s[interp % 3]);
    snprintf(v[S_STEREO], sizeof v[0], "%d%%", stereo);
    if (autosave_min) snprintf(v[S_AUTOSAVE], sizeof v[0], "every %d min", autosave_min); else snprintf(v[S_AUTOSAVE], sizeof v[0], "off");
    snprintf(v[S_DL], sizeof v[0], "%s%s", dl_dir, name_edit == dl_dir ? "_" : "");
    static const char *label[S_N] = { "Volume", "Shuffle", "Repeat", "Fade at loop", "Skip silence", "Subsongs",
                                      "Theme", "Pixel scaling", "Interpolation", "Stereo width", "Autosave", "Downloads" };
    static const int head[S_N] = { 1, 0, 0, 2, 0, 0, 3, 0, 4, 0, 5, 0 };   /* section titles before these rows */
    static const char *sect[6] = { "", "-- playback --", "-- song endings --", "-- look --", "-- sound --", "-- files --" };
    int sel_line = 0;
    for (int i = 0; i < S_N; i++) {
        if (head[i]) { if (i) tadd(""); tadd("%s", sect[head[i]]); }
        if (i == cfg_sel) sel_line = tn;
        tadd("  %-15s %s %s %s", label[i], i == cfg_sel && i != S_DL ? "<" : " ", v[i], i == cfg_sel && i != S_DL ? ">" : "");
    }
    tadd("");
    tadd("  %s", cfg_help[cfg_sel]);                      /* keep these to 72 characters: one line in the window */
    tadd("  saved in %s", ini[0] ? ini : "(no settings file)");
    text_sel = sel_line;
    *first = sel_line < rows / 2 ? 0 : sel_line - rows / 2;
    if (*first > tn - rows) *first = tn - rows > 0 ? tn - rows : 0;
}
