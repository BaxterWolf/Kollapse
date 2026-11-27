/* Modland search and download. See modland.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "compat.h"
#ifdef _WIN32
#include <wininet.h>
#endif
#include "miniz.h"
#include "modland.h"

#define BASE "https://ftp.modland.com/pub/modules/"
#define INDEX_URL "https://ftp.modland.com/allmods.zip"

/* the index: only formats Kollapse plays. text holds "path\0" strings, lc the same lowercased */
static char *text, *lc;
static int *off, *sz, n_ent;
static volatile int state;
static volatile float iprog;
static char err[160];

static volatile int fstate;                 /* download: 0 idle, 1 busy, 2 done, -1 failed */
static volatile float fprog;
static char fpath[MAX_PATH], furl[2048], fdir[MAX_PATH], fcache[MAX_PATH];

#ifdef _WIN32
static void *http_get(const char *url, size_t *n, volatile float *prog)
{
    void *buf = NULL;
    *n = 0;
    HINTERNET net = InternetOpenA("Kollapse", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!net) { snprintf(err, sizeof err, "no internet (%lu)", GetLastError()); return NULL; }
    HINTERNET h = InternetOpenUrlA(net, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
    if (!h) { snprintf(err, sizeof err, "can't reach ftp.modland.com (%lu)", GetLastError()); InternetCloseHandle(net); return NULL; }
    DWORD code = 0, total = 0, len = sizeof code;
    HttpQueryInfoA(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &len, NULL);
    len = sizeof total;
    if (!HttpQueryInfoA(h, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &total, &len, NULL)) total = 0;
    if (code != 200) snprintf(err, sizeof err, "server said %lu", code);
    else {
        size_t cap = total ? total : 1 << 16, have = 0;
        buf = malloc(cap);
        DWORD got;
        char chunk[16384];
        while (buf && InternetReadFile(h, chunk, sizeof chunk, &got) && got) {
            if (have + got > cap) { void *nb = realloc(buf, cap = (have + got) * 2); if (!nb) { free(buf); buf = NULL; break; } buf = nb; }
            memcpy((char *)buf + have, chunk, got); have += got;
            if (prog && total) *prog = (float)have / total;
        }
        if (buf && total && have != total) { free(buf); buf = NULL; snprintf(err, sizeof err, "download cut off"); }
        else if (buf) *n = have;
        else if (!err[0]) snprintf(err, sizeof err, "download failed");
    }
    InternetCloseHandle(h); InternetCloseHandle(net);
    return buf;
}

#else
/* Linux: curl. ml_url percent-encodes, so the URL is safe in single quotes */
static void *http_get(const char *url, size_t *n, volatile float *prog)
{
    char cmd[2300];
    *n = 0;
    if (strchr(url, '\'')) { snprintf(err, sizeof err, "bad url"); return NULL; }
    snprintf(cmd, sizeof cmd, "curl -sSL --fail --max-time 120 '%s' 2>/dev/null", url);
    FILE *f = popen(cmd, "r");
    if (!f) { snprintf(err, sizeof err, "can't run curl"); return NULL; }
    size_t cap = 1 << 16, have = 0;
    char *buf = malloc(cap);
    for (size_t k; buf && (k = fread(buf + have, 1, cap - have, f)) > 0; ) {
        have += k;
        if (have == cap) { char *nb = realloc(buf, cap *= 2); if (!nb) { free(buf); buf = NULL; } else buf = nb; }
    }
    int rc = pclose(f);
    if (rc != 0 || !buf || !have) { free(buf); snprintf(err, sizeof err, rc == 127 << 8 ? "curl isn't installed" : "download failed"); return NULL; }
    if (prog) *prog = 1;
    *n = have;
    return buf;
}
#endif

static int wanted(const char *path)            /* Modland folders libxmp plays (each checked with a real file) */
{
    static const char *fmt[] = {
        "Protracker/", "Fasttracker/", "Soundtracker/", "Startrekker AM/", "Chiptracker/", "His Master's Noise/",
        "Octalyser/", "Fasttracker 2/", "Screamtracker 3/", "Impulsetracker/", "Screamtracker 2/", "Composer 669/",
        "Farandole Composer/", "Multitracker/", "Polytracker/", "Ultratracker/", "Digitrakker/", "Oktalyzer/",
        "OctaMED MMD0/", "OctaMED MMD1/", "OctaMED MMD2/", "OctaMED MMD3/", "Music Editor/", "Digibooster/",
        "Digibooster Pro/", "Digital Symphony/", "Digital Tracker DTM/", "Epic Megagames MASI/", "General DigiMusic/",
        "Digital Sound And Music Interface/", "Asylum/", "Archimedes Tracker/", "Game Music Creator/", "Coconizer/",
        "SoundFX/", "Funktracker/", "Imago Orpheus/", "Liquid Tracker/", "Real Tracker/" };
    const char *dot = strrchr(path, '.');
    if (dot && !_stricmp(dot, ".nt")) return 0;         /* Startrekker's separate instrument files */
    for (size_t i = 0; i < sizeof fmt / sizeof *fmt; i++) if (!strncmp(path, fmt[i], strlen(fmt[i]))) return 1;
    return 0;
}

/* "size\tpath\n" lines -> the arrays; text is taken over (filtered in place) */
int ml_load_text(char *t, size_t len)
{
    int cap = 1024, n = 0, *o = malloc(cap * sizeof *o), *s = malloc(cap * sizeof *s);
    char *w = t, *end = t + len;
    if (!o || !s) { free(o); free(s); free(t); return 0; }
    for (char *p = t; p < end; ) {
        char *nl = memchr(p, '\n', end - p), *tab;
        if (!nl) nl = end;
        if (nl > p && nl[-1] == '\r') nl[-1] = 0;
        *nl = 0;
        if ((tab = strchr(p, '\t')) && wanted(tab + 1)) {
            if (n == cap) { cap *= 2; int *a = realloc(o, cap * sizeof *o), *b = realloc(s, cap * sizeof *s); if (!a || !b) break; o = a; s = b; }
            size_t l = strlen(tab + 1);
            s[n] = atoi(p);
            memmove(w, tab + 1, l + 1);                    /* compact: keep just the path */
            o[n++] = (int)(w - t); w += l + 1;
        }
        p = nl + 1;
    }
    char *l2 = malloc(w - t + 1);
    if (!l2) { free(o); free(s); free(t); return 0; }
    for (char *a = t, *b = l2; a < w; a++, b++) *b = (char)tolower((unsigned char)*a);
    free(text); free(lc); free(off); free(sz);
    text = t; lc = l2; off = o; sz = s; n_ent = n;
    return n;
}

static void save_cache(const char *cache)       /* the filtered index, same "size\tpath" format */
{
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof tmp, "%s.tmp", cache);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    for (int i = 0; i < n_ent; i++) fprintf(f, "%d\t%s\n", sz[i], text + off[i]);
    if (fclose(f) == 0) MoveFileExA(tmp, cache, MOVEFILE_REPLACE_EXISTING);
    else DeleteFileA(tmp);
}

static void *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = l > 0 ? malloc(l + 1) : NULL;
    if (b && fread(b, 1, l, f) != (size_t)l) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[l] = 0; *n = l; }
    return b;
}

static DWORD WINAPI index_thread(LPVOID arg)
{
    size_t zn, tn;
    err[0] = 0;
    void *z = http_get(INDEX_URL, &zn, &iprog);
    char *t = NULL;
    if (z) {
        mz_zip_archive za; memset(&za, 0, sizeof za);
        if (mz_zip_reader_init_mem(&za, z, zn, 0)) {
            t = mz_zip_reader_extract_file_to_heap(&za, "allmods.txt", &tn, 0);
            mz_zip_reader_end(&za);
        }
        free(z);
        if (!t) snprintf(err, sizeof err, "the index zip didn't unpack");
    }
    if (t) { char *t2 = realloc(t, tn + 1); if (t2) { t = t2; t[tn] = 0; } else { mz_free(t); t = NULL; } }   /* the parser needs t[len] */
    if (t && ml_load_text(t, tn)) { save_cache(fcache); state = ML_READY; }
    else state = n_ent ? ML_READY : ML_FAILED;            /* a refresh that fails keeps the old index */
    return 0;
}

int ml_state(float *prog) { if (prog) *prog = iprog; return state; }
const char *ml_error(void) { return err; }
int ml_count(void) { return n_ent; }

void ml_refresh(const char *cache)
{
    if (state == ML_INDEX) return;
    snprintf(fcache, sizeof fcache, "%s", cache);
    iprog = 0; state = ML_INDEX;
    HANDLE th = CreateThread(NULL, 0, index_thread, NULL, 0, NULL);
    if (th) CloseHandle(th); else state = n_ent ? ML_READY : ML_FAILED;
}

void ml_open(const char *cache)
{
    size_t n;
    char *t;
    if (state == ML_INDEX || state == ML_READY) return;
    if ((t = read_all(cache, &n)) && ml_load_text(t, n)) { state = ML_READY; return; }
    ml_refresh(cache);
}

int ml_search(const char *q, int *out, int max)
{
    char w[8][64]; int nw = 0, found = 0;
    for (const char *p = q; *p && nw < 8; ) {               /* split into lowercase words */
        while (*p == ' ') p++;
        int l = 0;
        while (*p && *p != ' ') { if (l < 63) w[nw][l++] = (char)tolower((unsigned char)*p); p++; }
        if (l) w[nw++][l] = 0;
    }
    if (!nw) return 0;
    for (int i = 0; i < n_ent && found < max; i++) {
        const char *s = lc + off[i];
        int ok = 1;
        for (int k = 0; ok && k < nw; k++) ok = strstr(s, w[k]) != NULL;
        if (ok) out[found++] = i;
    }
    return found;
}

const char *ml_path(int i) { return i >= 0 && i < n_ent ? text + off[i] : ""; }
int ml_size(int i) { return i >= 0 && i < n_ent ? sz[i] : 0; }

void ml_label(int i, char *out, size_t n)
{
    const char *p = ml_path(i), *a = strchr(p, '/'), *file = strrchr(p, '/'), *dot = strrchr(p, '.');
    char artist[96] = "?", fmt[8] = "";
    if (a && file && file > a) {
        const char *ae = strchr(a + 1, '/');
        int l = (int)(ae - a - 1);
        snprintf(artist, sizeof artist, "%.*s", l < 95 ? l : 95, a + 1);
        if (ae != file) {                                  /* a deeper folder, e.g. coop-Ghidorah */
            size_t al = strlen(artist);
            snprintf(artist + al, sizeof artist - al, " (%.*s)", (int)(file - ae - 1), ae + 1);
        }
    }
    for (int k = 0; dot && dot[k + 1] && k < 7; k++) fmt[k] = (char)toupper((unsigned char)dot[k + 1]), fmt[k + 1] = 0;
    snprintf(out, n, "%s - %s  [%s, %dk]", artist, file ? file + 1 : p, fmt, (ml_size(i) + 1023) / 1024);
}

void ml_url(const char *path, char *out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = snprintf(out, n, "%s", BASE);
    for (const unsigned char *p = (const unsigned char *)path; *p && o + 4 < n; p++)
        if (isalnum(*p) || strchr("-_.~/", *p)) out[o++] = (char)*p;
        else { out[o++] = '%'; out[o++] = hex[*p >> 4]; out[o++] = hex[*p & 15]; }
    out[o < n ? o : n - 1] = 0;
}

void ml_local_name(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && o + 1 < n; p++)
        out[o++] = *p < 32 || *p >= 127 || strchr("<>:\"/\\|?*", *p) ? '_' : (char)*p;
    while (o && (out[o - 1] == '.' || out[o - 1] == ' ')) o--;   /* Windows drops trailing dots/spaces */
    out[o] = 0;
    if (!o) snprintf(out, n, "_");
}

static void make_dirs(const char *path)          /* CreateDirectory for every level */
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++) if (*c == SEP) { *c = 0; CreateDirectoryA(p, NULL); *c = SEP; }
    CreateDirectoryA(p, NULL);
}

static DWORD WINAPI fetch_thread(LPVOID arg)
{
    size_t n;
    err[0] = 0;
    void *b = http_get(furl, &n, &fprog);
    int ok = 0;
    if (b) {
        make_dirs(fdir);
        FILE *f = fopen(fpath, "wb");
        ok = f && fwrite(b, 1, n, f) == n;
        if (f && fclose(f)) ok = 0;
        if (!ok) { DeleteFileA(fpath); snprintf(err, sizeof err, "can't write %s", fpath); }
        free(b);
    }
    fstate = ok ? 2 : -1;
    return 0;
}

int ml_fetch(int i, const char *dir)
{
    if (fstate == 1 || i < 0 || i >= n_ent) return 0;
    const char *p = ml_path(i), *a = strchr(p, '/'), *file = strrchr(p, '/');
    char artist[96], name[MAX_PATH], raw[96];
    const char *ae = a ? strchr(a + 1, '/') : NULL;
    snprintf(raw, sizeof raw, "%.*s", ae ? (int)(ae - a - 1) : 1, ae ? a + 1 : "_");
    ml_local_name(raw, artist, sizeof artist);
    ml_local_name(file ? file + 1 : p, name, sizeof name);
    snprintf(fdir, sizeof fdir, "%s" SEPS "%s", dir, artist);
    snprintf(fpath, sizeof fpath, "%s" SEPS "%s", fdir, name);
    if (GetFileAttributesA(fpath) != INVALID_FILE_ATTRIBUTES) { fstate = 2; return 1; }   /* already here */
    ml_url(p, furl, sizeof furl);
    fprog = 0; fstate = 1;
    HANDLE th = CreateThread(NULL, 0, fetch_thread, NULL, 0, NULL);
    if (th) CloseHandle(th); else fstate = -1;
    return 1;
}

int ml_fetch_state(float *prog, char *path, size_t n)
{
    int s = fstate;
    if (prog) *prog = fprog;
    if (s == 2 || s == -1) { if (path) snprintf(path, n, "%s", fpath); fstate = 0; }
    return s;
}
