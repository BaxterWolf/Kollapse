/* Modland: local search of its public index, and downloads. Slow work runs on one thread;
   poll ml_state(). */
#ifndef MODLAND_H
#define MODLAND_H
#include <stddef.h>

enum { ML_IDLE, ML_INDEX, ML_READY, ML_FAILED };
int  ml_state(float *prog);                 /* ML_* for the index; prog 0..1 while it downloads */
const char *ml_error(void);                 /* why the last index fetch or download failed */
void ml_open(const char *cache);            /* load the cached index, or fetch it if there is none */
void ml_refresh(const char *cache);         /* fetch a fresh index (keeps the old one until it's in) */
int  ml_count(void);
int  ml_search(const char *query, int *out, int max);   /* all words must match; returns how many found (<= max) */
const char *ml_path(int i);                 /* "Fasttracker 2/Ghidorah/aftershock!.xm" */
int  ml_size(int i);
void ml_label(int i, char *out, size_t n);  /* "Ghidorah - aftershock!.xm  [XM, 55k]" */

/* download entry i into <dir>\<artist>\<file>; 0 if a download is already running */
int  ml_fetch(int i, const char *dir);
int  ml_fetch_state(float *prog, char *path, size_t n);   /* 0 idle, 1 busy, 2 done (path set), -1 failed; 2/-1 reported once */

/* helpers, exposed for tests */
void ml_url(const char *path, char *out, size_t n);       /* https://ftp.modland.com/pub/modules/<percent-encoded path> */
void ml_local_name(const char *s, char *out, size_t n);  /* safe file/folder name */
int  ml_load_text(char *text, size_t len);                /* take ownership of an allmods.txt-style buffer */
#endif
