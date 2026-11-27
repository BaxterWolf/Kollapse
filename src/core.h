/* core.h - player state, layout table and input for Kollapse. No drawing in here. */
#ifndef CORE_H
#define CORE_H
#include <stdint.h>

#define SCREEN_W 632
#define SCREEN_H 400
#define TOP_H    173   /* top panel height in pixels */
#define PAT_Y    187   /* pattern rows start (pan strip sits above) */
#define HINT_Y   388   /* key-hint bar */

/* palette (our own colours) */
extern unsigned core_pal[18];
void core_banner(unsigned bars[8], unsigned ink[5]);   /* the KOLLAPSE banner's colours for the theme */          /* the current theme (Settings page) */
#define C_BLACK   (core_pal[16])   /* panel background: black in dark themes */
#define C_LABEL   (core_pal[17])   /* row / channel / instrument numbers on the panels */
#define C_DESK     (core_pal[0])
#define C_LIGHT    (core_pal[1])
#define C_DARK     (core_pal[2])
#define C_TEXT     (core_pal[3])
#define C_SHADOW   (core_pal[4])
#define C_BEATBG   (core_pal[5])
#define C_BARBG    (core_pal[6])
#define C_NOTE     (core_pal[7])
#define C_INS      (core_pal[8])
#define C_VOL      (core_pal[9])
#define C_FX       (core_pal[10])
#define C_DIM      (core_pal[11])
#define C_SCOPE    (core_pal[12])
#define C_SEL      (core_pal[13])
#define C_CURSOR   (core_pal[14])
#define C_MUTE     (core_pal[15])

/* widget types */
enum { W_PANEL, W_SUNK, W_BUTTON, W_LABEL, W_FIELD, W_LIST, W_LOGO, W_SCOPES, W_BG };

/* dynamic text ids */
enum { F_NONE, F_TITLE, F_TYPE, F_CHANS, F_VOL, F_BPM, F_SPEED, F_ORD, F_PTN, F_ROW,
       F_TIME, F_STATUS, F_FILES_HDR, F_INSTR_HDR, F_HINTS, F_MODE };
enum { L_FILES = 1, L_INSTR };
enum { A_NONE, A_MENU = 100 };

typedef struct { uint8_t type; int16_t x, y, w, h; const char *text; int id; } widget_t;
extern const widget_t layout[];
extern const int layout_count;

/* keys: printable chars are passed as themselves, specials start at 256; K_CTRL is or'ed in */
enum { K_UP = 256, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN, K_HOME, K_END, K_ENTER, K_ESC, K_BACK,
       K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F12, K_TAB, K_STAB, K_DEL, K_INS };
#define K_CTRL  0x10000
#define K_SHIFT 0x20000                  /* or'ed into arrows / PgUp / PgDn / Home / End */
/* Windows virtual key -> K_ code (0 if it's not one of ours; printable keys come as characters) */
#ifdef _WIN32
int core_vk(int vk, int ctrl, int shift);
#endif
int core_hook_key(const char **s);       /* test hook: parse one KOLLAPSE_KEYS token, advance *s */
/* control characters the core understands (Ctrl+C L N O Q R S T V X Z); renderers pass these through */
static inline int core_ctrl_char(int c) { return c == 3 || c == 5 || c == 6 || c == 10 || c == 12 || c == 14 || c == 15 || c == 16 || c == 17 || c == 18 || c == 19 || c == 20 || c == 22 || c == 24 || c == 26; }

/* pages shown in the lower half; the top half (info, scopes, lists) stays */
enum { P_PLAY, P_EDIT, P_SAMPLE, P_INSTR, P_MESSAGE, P_SPECTRUM, P_SONG, P_CONFIG };
int  core_page(void);
void core_solo(int ch);
#ifndef KOLLAPSE_VERSION
#define KOLLAPSE_VERSION "dev"
#endif
int  core_pixel_fit(void);                 /* Sixel in the terminal: 0 fill, 1 keep the shape, 2 crisp (Settings) */
extern int core_pixel_mode;                /* set by the terminal version when it draws Sixel; Settings notes the rest */

/* pattern cell text widths: full "C-5 01 64 F06", medium "C-5 01 64", short "C-5 01", tiny "C-5" */
static const int cell_widths[4] = { 13, 9, 6, 3 };

/* load settings, pick the folder, start audio. prefer_last: no arguments opens the last folder */
void core_init(int argc, char **argv, int prefer_last);
void core_update(double dt);             /* call once per frame */
void core_stall_guard(void);             /* Windows terminal: pause the sound while frames stop (window closed) */
int  core_key(int k);                    /* returns 1 to quit */
void core_action(int id);
void core_toggle_mute(int ch);           /* 0-based channel; clicks on its pan bar or scope */
void core_click_list(int id, int vis_row, int rows); /* mouse on a list row: select (and play files) */
const char *core_field(int id, char *buf);
/* list row i of `rows` visible; returns NULL past the end. flags: 1 selected, 2 now playing, 4 folder, 8 zip. glow 0..1 */
const char *core_list(int id, int i, int rows, int *num, int *flags, float *glow);

/* playback view */
int   core_nchan(void);
int   core_rows(void);                   /* rows in current pattern (0 = nothing loaded) */
int   core_row(void);
void  core_cell(int row, int ch, char out[14]);   /* "C-5 01 64 F06", '.'/'-' for empty */
void  core_chan(int ch, int *pan, float *vol, int *muted);  /* pan 0..255 */
float core_scope(int ch, float t);       /* -1..1, t in 0..1 across the scope */
int   core_chan_off(int vis);            /* first visible channel when only vis fit */
float core_progress(void);               /* 0..1 through the song */
int   core_pick_width(int avail, int per_char, int pad, int *vis); /* index into cell_widths */

float core_level(int ch);                /* 0..1 VU level from the channel's actual signal */
void  core_title(char *buf, int n);      /* window / console tab title */

/* pattern editor: cursor in the view (row, channel, character offset+length inside the 13-char cell) */
int   core_cursor(int *row, int *ch, int *off, int *len);   /* 0 when not on the editor page */

/* text pages (message, sample/instrument info): `rows` visible; returns lines from *first */
const char **core_text(int rows, int *n, int *first);
int   core_text_sel(void);               /* highlighted line of the last core_text (-1 none) */
/* sample page: min/max of column col of ncols; loop and play position as 0..1 fractions (-1 = none) */
int   core_wave(int col, int ncols, float *lo, float *hi);  /* 0 = no sample */
void  core_wave_marks(float *loop_start, float *loop_end, float *playpos);
/* instrument page: envelope which=0 volume, 1 panning; points as 0..1; returns point count */
int   core_env(int which, float *xs, float *ys, int max, int *sus, int *lps, int *lpe, int *on);
/* spectrum page: n bars 0..1 and their falling peaks */
void  core_spectrum(float *bars, float *peaks, int n);
int   core_env_sel(int which);          /* selected point of that envelope on the instrument page, -1 none */
int   core_instr_view(void);            /* instrument page view: 0 vol env, 1 pan env, 2 keymap, 3 vibrato */
void  core_wave_range(float *a, float *b); /* sample page selection as 0..1 (-1 = whole sample) */

/* overlay (menu / help); NULL when closed */
const char **core_overlay(int *n, int *sel, const char **title);
#endif
