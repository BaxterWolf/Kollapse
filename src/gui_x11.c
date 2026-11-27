/* gui_x11.c - kollapsew on Linux: the 632x400 framebuffer in an X11 window (also XWayland), scaled per Settings > Pixel scaling. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "core.h"
#include "fb.h"

static Display *dpy;
static Window   win;
static GC       gc;
static XImage  *img;                                  /* window-sized; the scaled framebuffer goes in the middle */
static int      ww = SCREEN_W * 2, wh = SCREEN_H * 2;

static void to_fb(int px, int py, int *x, int *y)
{
    int ox, oy, w, h; fb_fit(ww, wh, &ox, &oy, &w, &h);
    *x = (px - ox) * SCREEN_W / w;
    *y = (py - oy) * SCREEN_H / h;
}

static void make_img(void)
{
    if (img) XDestroyImage(img);                      /* frees the pixels too */
    char *px = malloc((size_t)ww * wh * 4);
    img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), 24, ZPixmap, 0, px, (unsigned)ww, (unsigned)wh, 32, 0);
}

/* assumes 24-bit TrueColor laid out as 0xRRGGBB, which X servers and XWayland give */
static void draw(void)
{
    int ox, oy, w, h; fb_fit(ww, wh, &ox, &oy, &w, &h);
    uint32_t *d = (uint32_t *)img->data;
    memset(d, 0, (size_t)ww * wh * 4);
    for (int y = 0; y < h && oy + y < wh; y++) {          /* nearest neighbour */
        const uint32_t *src = fb + (y * SCREEN_H / h) * SCREEN_W;
        uint32_t *row = d + (size_t)(oy + y) * ww + ox;
        for (int x = 0; x < w && ox + x < ww; x++) row[x] = src[x * SCREEN_W / w];
    }
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)ww, (unsigned)wh);
}

static int key(XKeyEvent *e)                          /* X key -> K_ code or character; 0 if not ours */
{
    char ch[8]; KeySym ks;
    int n = XLookupString(e, ch, sizeof ch, &ks, NULL);
    int ctrl = (e->state & ControlMask) != 0, shift = (e->state & ShiftMask) != 0, k = 0;
    switch (ks) {
    case XK_Up: k = K_UP; break;          case XK_Down: k = K_DOWN; break;
    case XK_Left: k = K_LEFT; break;      case XK_Right: k = K_RIGHT; break;
    case XK_Page_Up: k = K_PGUP; break;   case XK_Page_Down: k = K_PGDN; break;
    case XK_Home: k = K_HOME; break;      case XK_End: k = K_END; break;
    case XK_Return: case XK_KP_Enter: k = K_ENTER; break;
    case XK_Escape: k = K_ESC; break;     case XK_BackSpace: k = K_BACK; break;
    case XK_Delete: k = K_DEL; break;     case XK_Insert: k = K_INS; break;
    case XK_Tab: k = shift ? K_STAB : K_TAB; break;   case XK_ISO_Left_Tab: k = K_STAB; break;
    case XK_F1: k = K_F1; break; case XK_F2: k = K_F2; break; case XK_F3: k = K_F3; break; case XK_F4: k = K_F4; break;
    case XK_F5: k = K_F5; break; case XK_F6: k = K_F6; break; case XK_F7: k = K_F7; break; case XK_F8: k = K_F8; break;
    case XK_F9: k = K_F9; break; case XK_F10: k = K_F10; break; case XK_F12: k = K_F12; break;
    }
    if (k) {
        if (shift && ((k >= K_UP && k <= K_END) || k == K_ENTER)) k |= K_SHIFT;   /* same rules as core_vk */
        return ctrl ? k | K_CTRL : k;
    }
    int c = n == 1 ? (unsigned char)ch[0] : 0;        /* XLookupString already turns Ctrl+Q into 17 etc. */
    return (c >= 32 && c < 127) || core_ctrl_char(c) ? c : 0;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--dump")) {     /* kollapsew --dump out.bmp [track] : render ~2 s in, no X needed */
        char *args[2] = { argv[0], argc >= 4 ? argv[3] : NULL };
        core_init(argc >= 4 ? 2 : 1, args, 1);
        const char *keys = getenv("KOLLAPSE_KEYS");
        while (keys && *keys) {
            int cxp, cyp, used;
            if (*keys == '@' && sscanf(keys + 1, "%d,%d.%n", &cxp, &cyp, &used) == 2) { fb_render(); fb_click(cxp, cyp); keys += 1 + used; }
            else core_key(core_hook_key(&keys));
            for (int i = 0; i < 5; i++) { usleep(16000); core_update(0.016); }
        }
        for (int i = 0; i < 120; i++) { usleep(16000); core_update(0.016); }
        fb_render();
        return fb_dump_bmp(argv[2]);
    }
    if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "kollapsew: no X display (try kollapse in the terminal)\n"); return 1; }
    core_pixel_mode = 1;
    core_init(argc, argv, 1);

    int scr = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, (unsigned)ww, (unsigned)wh, 0, 0, BlackPixel(dpy, scr));
    XSelectInput(dpy, win, KeyPressMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask | ExposureMask);
    XClassHint ch = { "kollapse", "Kollapse" }; XSetClassHint(dpy, win, &ch);
    Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &del, 1);
    XStoreName(dpy, win, "Kollapse");
    gc = XCreateGC(dpy, win, 0, NULL);
    make_img();
    XMapWindow(dpy, win);

    double last = now();
    for (;;) {
        while (XPending(dpy)) {
            XEvent e; XNextEvent(dpy, &e);
            int x, y, k;
            switch (e.type) {
            case ClientMessage: if ((Atom)e.xclient.data.l[0] == del) goto out; break;
            case ConfigureNotify:
                if (e.xconfigure.width != ww || e.xconfigure.height != wh) { ww = e.xconfigure.width; wh = e.xconfigure.height; make_img(); }
                break;
            case KeyPress: if ((k = key(&e.xkey)) && core_key(k)) goto out; break;
            case ButtonPress:
                to_fb(e.xbutton.x, e.xbutton.y, &x, &y);
                if (e.xbutton.button == 3) { fb_click_solo(x, y); break; }
                if (e.xbutton.button != 1 || fb_click_mute(x, y)) break;
                int i = fb_hit(x, y);
                if (i >= 0 && layout[i].type == W_LIST) core_click_list(layout[i].id, (y - layout[i].y - 1) / 11, (layout[i].h - 2) / 11);
                else fb_pressed = i;
                break;
            case ButtonRelease:
                if (e.xbutton.button != 1) break;
                to_fb(e.xbutton.x, e.xbutton.y, &x, &y);
                if (fb_pressed >= 0 && fb_hit(x, y) == fb_pressed) core_action(layout[fb_pressed].id);
                fb_pressed = -1;
                break;
            }
        }
        double t = now();
        core_update(t - last);
        last = t;
        fb_render();
        char title[160];                               /* now playing in the title bar */
        static char shown[160];
        core_title(title, sizeof title);
        if (strcmp(title, shown)) { XStoreName(dpy, win, title); strcpy(shown, title); }
        draw();
        XFlush(dpy);
        double spent = now() - t;
        if (spent < 0.016) usleep((useconds_t)((0.016 - spent) * 1e6));
    }
out:
    XCloseDisplay(dpy);
    return 0;
}
