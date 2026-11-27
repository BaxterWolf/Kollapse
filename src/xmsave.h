/* xmsave.h - write a loaded libxmp module as .XM */
#ifndef XMSAVE_H
#define XMSAVE_H
struct xmp_module;
int xm_save(const struct xmp_module *m, const char *path, int linear, int from_xm);  /* 0 ok, -1 error */
int xm_fx(int t, int p, int *xt, int *xp);   /* libxmp effect -> XM effect; 0 if XM has none */
int xm_vol(int vol, int t, int p);           /* libxmp volume column -> XM volume byte */
#endif
