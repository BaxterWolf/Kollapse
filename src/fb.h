/* fb.h - the 632x400 pixel renderer (fb.c), shared by the window version and the console's --sixel mode */
#ifndef FB_H
#define FB_H
#include <stdint.h>
#include "core.h"

extern uint32_t fb[SCREEN_W * SCREEN_H];   /* 0xRRGGBB per pixel */
extern int fb_pressed;                     /* layout index of the button held down, -1 none */

void fb_render(void);
int  fb_click_mute(int x, int y);          /* 1 if (x,y) hit a scope or pan bar (toggles that channel) */
int  fb_hit(int x, int y);                 /* layout index of the button / list under (x,y), -1 none */
void fb_click_solo(int x, int y);          /* right click: solo the channel under (x,y) */
void fb_click(int x, int y);               /* a whole click: mute, list row or button */
int  fb_dump_bmp(const char *path);
void fb_fit(int cw, int ch, int *ox, int *oy, int *w, int *h);   /* where the picture goes in a cw x ch window (Pixel scaling) */
#endif
