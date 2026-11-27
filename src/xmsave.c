/* xmsave.c - write a libxmp module as FastTracker 2 XM, the inverse of libxmp's XM loader.
   From MOD/S3M/IT: effects without an XM equivalent are dropped; over 32 channels or 16 samples
   per instrument are cut. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xmp.h"
#include "xmsave.h"

static void w8(FILE *f, int v)  { fputc(v & 255, f); }
static void w16(FILE *f, int v) { w8(f, v); w8(f, v >> 8); }
static void w32(FILE *f, long v) { w16(f, (int)(v & 0xffff)); w16(f, (int)(v >> 16)); }
static void wstr(FILE *f, const char *s, int n)
{
    int i = 0;
    for (; i < n && s && s[i]; i++) w8(f, s[i]);
    for (; i < n; i++) w8(f, 0);
}

/* libxmp effect -> XM effect. libxmp's codes for XM's own effects are XM's numbers (G=0x10 ... X=0x21) */
int xm_fx(int t, int p, int *xt, int *xp)
{
    *xp = p;
    if (t < 16) { *xt = t; return 1; }
    switch (t) {
    case 0x10: case 0x11: case 0x14: case 0x15: case 0x19: case 0x1b: case 0x1d: case 0x21: *xt = t; return 1;
    case 0xa3: *xt = 0x0f; *xp = p > 0x1f ? 0x1f : p; return 1;            /* S3M/IT speed -> Fxx < 20 */
    case 0xab: case 0x87: *xt = 0x0f; *xp = p < 0x20 ? 0x20 : p; return 1; /* tempo -> Fxx >= 20 */
    case 0x8e: *xt = 0x0d; *xp = ((p / 10) << 4) | (p % 10); return 1;     /* IT break (hex) -> FT2 Dxx */
    case 0xb4: *xt = 0x00; return 1;                                        /* S3M arpeggio */
    case 0xac: *xt = 0x04; return 1;                                        /* fine vibrato ~ vibrato */
    case 0x89: *xt = 0x19; return 1;                                        /* IT pan slide */
    case 0x8d: *xt = 0x21; *xp = 0x90 | (p & 15); return 1;                 /* surround: X9x */
    }
    return 0;
}

/* volume column (libxmp: vol+1, or a command in f2t/f2p) -> XM volume byte */
int xm_vol(int vol, int t, int p)
{
    if (vol) return 0x10 + (vol - 1 > 64 ? 64 : vol - 1);
    switch (t) {
    case 0xa4: return p & 0xf0 ? 0x70 | (p >> 4) : 0x60 | (p & 15);        /* volume slide up/down */
    case 0x0e: return (p >> 4) == 0xb ? 0x80 | (p & 15) : (p >> 4) == 0xa ? 0x90 | (p & 15) : 0;
    case 0x04: return p & 0xf0 ? 0xa0 | (p >> 4) : 0xb0 | (p & 15);        /* vibrato speed / depth */
    case 0x08: return 0xc0 | (p >> 4);                                      /* panning */
    case 0xb5: return p & 0xf0 ? 0xd0 | (p >> 4) : 0xe0 | (p & 15);        /* pan slide */
    case 0x03: return 0xf0 | (p >> 4);                                      /* tone portamento */
    case 0xc0: return 0x70 | (p & 15);  case 0xc1: return 0x60 | (p & 15); /* IT volume column slides */
    case 0xc2: return 0x90 | (p & 15);  case 0xc3: return 0x80 | (p & 15);
    }
    return 0;
}

static int xm_note(int n)
{
    if (n == XMP_KEY_OFF || n == XMP_KEY_CUT || n == XMP_KEY_FADE) return 97;
    return n >= 13 && n <= 108 ? n - 12 : 0;
}

int xm_save(const struct xmp_module *m, const char *path, int linear, int from_xm)
{
    int nch = m->chn > 32 ? 32 : m->chn, npat = m->pat - (from_xm && m->pat > 1), nins = m->ins > 128 ? 128 : m->ins;
    if (npat > 256) npat = 256;                          /* from_xm: drop the blank pattern libxmp's XM loader appends */
    unsigned char ord[256]; int len = 0;
    for (int i = 0; i < m->len && len < 256; i++) if (m->xxo[i] < npat) ord[len++] = m->xxo[i];   /* drop S3M/IT +++ / --- markers */
    if (!len) ord[len++] = 0;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fwrite("Extended Module: ", 1, 17, f);
    wstr(f, m->name, 20);
    w8(f, 0x1a);
    wstr(f, "Kollapse", 20);
    w16(f, 0x0104);
    w32(f, 276);
    w16(f, len); w16(f, m->rst < len ? m->rst : 0); w16(f, nch); w16(f, npat); w16(f, nins);
    w16(f, linear ? 1 : 0); w16(f, m->spd); w16(f, m->bpm);
    for (int i = 0; i < 256; i++) w8(f, i < len ? ord[i] : 0);

    /* patterns: packed cells (flag byte 0x80|present bits, then the present fields) */
    unsigned char *buf = malloc(256 * 32 * 6);
    for (int p = 0; p < npat; p++) {
        const struct xmp_pattern *xp = m->xxp[p];
        int rows = xp->rows > 256 ? 256 : xp->rows, n = 0;
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < nch; c++) {
                const struct xmp_event *e = &m->xxt[xp->index[c]]->event[r];
                int note = xm_note(e->note), vol = xm_vol(e->vol, e->f2t, e->f2p), ft = 0, fp = 0;
                if ((e->fxt || e->fxp) && !xm_fx(e->fxt, e->fxp, &ft, &fp)) ft = fp = 0;
                int flag = 0x80 | (note ? 1 : 0) | (e->ins ? 2 : 0) | (vol ? 4 : 0) | (ft ? 8 : 0) | (fp ? 16 : 0);
                buf[n++] = flag;
                if (note) buf[n++] = note;
                if (e->ins) buf[n++] = e->ins;
                if (vol) buf[n++] = vol;
                if (ft) buf[n++] = ft;
                if (fp) buf[n++] = fp;
            }
        w32(f, 9); w8(f, 0); w16(f, rows); w16(f, n);
        fwrite(buf, 1, n, f);
    }
    free(buf);

    /* instruments + samples */
    for (int i = 0; i < nins; i++) {
        const struct xmp_instrument *xi = &m->xxi[i];
        int nsm = xi->nsm > 16 ? 16 : xi->nsm;
        if (!nsm) { w32(f, 29); wstr(f, xi->name, 22); w8(f, 0); w16(f, 0); continue; }
        w32(f, 263); wstr(f, xi->name, 22); w8(f, 0); w16(f, nsm); w32(f, 40);
        for (int k = 0; k < 96; k++) { int s = xi->map[k + 12].ins; w8(f, s < nsm ? s : 0); }
        const struct xmp_envelope *env[2] = { &xi->aei, &xi->pei };
        for (int e = 0; e < 2; e++)
            for (int pt = 0; pt < 12; pt++) {
                int on = pt < env[e]->npt && env[e]->npt <= 12;
                w16(f, on ? env[e]->data[pt * 2] : 0); w16(f, on ? env[e]->data[pt * 2 + 1] : 0);
            }
        for (int e = 0; e < 2; e++) w8(f, env[e]->npt > 12 ? 12 : env[e]->npt);
        for (int e = 0; e < 2; e++) { w8(f, env[e]->sus); w8(f, env[e]->lps); w8(f, env[e]->lpe); }
        w8(f, xi->aei.flg & 7); w8(f, xi->pei.flg & 7);
        const struct xmp_subinstrument *s0 = &xi->sub[0];
        w8(f, s0->vwf); w8(f, s0->vsw); w8(f, s0->vde >> 2); w8(f, s0->vra);
        w16(f, xi->rls >> 1);
        for (int k = 0; k < 22; k++) w8(f, 0);
        for (int k = 0; k < nsm; k++) {                  /* sample headers */
            const struct xmp_subinstrument *su = &xi->sub[k];
            const struct xmp_sample *s = su->sid < m->smp ? &m->xxs[su->sid] : NULL;
            int b = s && s->flg & XMP_SAMPLE_16BIT ? 2 : 1, loop = s && s->flg & XMP_SAMPLE_LOOP && s->lpe > s->lps;
            long slen = s && s->data ? (long)s->len * b : 0;
            w32(f, slen); w32(f, loop ? (long)s->lps * b : 0); w32(f, loop ? (long)(s->lpe - s->lps) * b : 0);
            w8(f, su->vol > 64 ? 64 : su->vol); w8(f, su->fin);
            w8(f, (loop ? (s->flg & XMP_SAMPLE_LOOP_BIDIR ? 2 : 1) : 0) | (b == 2 ? 0x10 : 0));
            w8(f, su->pan < 0 ? 128 : su->pan > 255 ? 255 : su->pan); w8(f, su->xpo); w8(f, 0);
            wstr(f, s ? s->name : "", 22);
        }
        for (int k = 0; k < nsm; k++) {                  /* sample data, delta-encoded */
            const struct xmp_subinstrument *su = &xi->sub[k];
            const struct xmp_sample *s = su->sid < m->smp ? &m->xxs[su->sid] : NULL;
            if (!s || !s->data) continue;
            int old = 0;
            if (s->flg & XMP_SAMPLE_16BIT)
                for (int j = 0; j < s->len; j++) { int v = ((const short *)s->data)[j]; w16(f, v - old); old = v; }
            else
                for (int j = 0; j < s->len; j++) { int v = ((const signed char *)s->data)[j]; w8(f, v - old); old = v; }
        }
    }
    int ok = !ferror(f);
    return fclose(f) == 0 && ok ? 0 : -1;
}
