/* term.h - the terminal: term_win32.c or term_posix.c. UTF-8 output with VT escapes. */
#ifndef TERM_H
#define TERM_H
#include <stddef.h>

int  term_open(void);                         /* raw input, VT output, alternate screen; 0 = not a terminal */
void term_close(void);                        /* back to how it was (also on exit and on signals) */
void term_write(const char *s, size_t n);
void term_size(int *w, int *h);               /* visible columns and rows */
void term_title(const char *t);
/* send a query and collect the terminal's reply until `end` has been seen `count` times (or ~300 ms) */
int  term_query(const char *q, char end, char *buf, int max, int count);

enum { TE_NONE, TE_KEY, TE_CLICK, TE_RCLICK };   /* F11 is left to the terminal (fullscreen) */
typedef struct { int type, key, x, y; } term_event;   /* key: a core key code; x, y: cell of a click */
int  term_poll(int ms, term_event *ev);       /* waits up to ms for one event; TE_NONE on timeout */
void term_start(void);                        /* the UI begins: mouse on, alternate screen, cursor hidden */
#endif
