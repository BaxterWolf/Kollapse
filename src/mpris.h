/* mpris.h - Linux media keys over D-Bus (MPRIS). Does nothing when built without libdbus. */
#ifndef MPRIS_H
#define MPRIS_H
int mpris_start(void);                          /* 1 if Kollapse is on the session bus */
int mpris_poll(int state, const char *title);   /* call each frame; returns 0 or 1 play/pause 2 next 3 prev 4 stop */
#endif
