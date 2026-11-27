/* gui_win32.c - kollapsew.exe: 632x400 framebuffer in a Win32 window, scaled per Settings > Pixel scaling. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"

#include "fb.h"


/* ---------- window ---------- */
static BITMAPINFO bmi;


static void to_fb(HWND hw, LPARAM lp, int *x, int *y)
{
    RECT r; GetClientRect(hw, &r);
    int ox, oy, w, h; fb_fit(r.right, r.bottom, &ox, &oy, &w, &h);
    *x = ((short)LOWORD(lp) - ox) * SCREEN_W / w;
    *y = ((short)HIWORD(lp) - oy) * SCREEN_H / h;
}

static LRESULT CALLBACK wndproc(HWND hw, UINT m, WPARAM wp, LPARAM lp)
{
    int x, y, k = 0;
    switch (m) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hw, &ps);
        RECT r; GetClientRect(hw, &r);
        int ox, oy, w, h; fb_fit(r.right, r.bottom, &ox, &oy, &w, &h);
        PatBlt(dc, 0, 0, r.right, oy, BLACKNESS); PatBlt(dc, 0, oy + h, r.right, r.bottom, BLACKNESS);
        PatBlt(dc, 0, 0, ox, r.bottom, BLACKNESS);  PatBlt(dc, ox + w, 0, r.right, r.bottom, BLACKNESS);
        SetStretchBltMode(dc, COLORONCOLOR);               /* nearest neighbour: sharp pixels */
        StretchDIBits(dc, ox, oy, w, h, 0, 0, SCREEN_W, SCREEN_H, fb, &bmi, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        to_fb(hw, lp, &x, &y);
        if (fb_click_mute(x, y)) return 0;
        int i = fb_hit(x, y);
        if (i >= 0 && layout[i].type == W_LIST) core_click_list(layout[i].id, (y - layout[i].y - 1) / 11, (layout[i].h - 2) / 11);
        else { fb_pressed = i; SetCapture(hw); }
        return 0;
    }
    case WM_RBUTTONDOWN: to_fb(hw, lp, &x, &y); fb_click_solo(x, y); return 0;
    case WM_LBUTTONUP:
        to_fb(hw, lp, &x, &y);
        if (fb_pressed >= 0 && fb_hit(x, y) == fb_pressed) core_action(layout[fb_pressed].id);
        fb_pressed = -1; ReleaseCapture(); return 0;
    case WM_CHAR:                                       /* printable keys, plus the Ctrl+letter shortcuts */
        if (((wp >= 32 && wp < 127) || core_ctrl_char((int)wp)) && core_key((int)wp)) DestroyWindow(hw);
        return 0;
    case WM_SYSKEYDOWN: if (wp != VK_F10) break;          /* fallthrough: keep F10 out of the menu bar */
    case WM_KEYDOWN:
        k = core_vk((int)wp, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0);
        if (k && core_key(k)) DestroyWindow(hw);
        return 0;
    }
    return DefWindowProc(hw, m, wp, lp);
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--dump")) {     /* kollapsew --dump out.bmp [track] : render ~2 s in */
        char *args[2] = { argv[0], argc >= 4 ? argv[3] : NULL };
        core_init(argc >= 4 ? 2 : 1, args, 1);
        const char *keys = getenv("KOLLAPSE_KEYS");          /* test hook: see core_hook_key; @x,y. = click */
        while (keys && *keys) {
            int cxp, cyp, used;
            if (*keys == '@' && sscanf(keys + 1, "%d,%d.%n", &cxp, &cyp, &used) == 2) { fb_render(); fb_click(cxp, cyp); keys += 1 + used; }
            else core_key(core_hook_key(&keys));
            for (int i = 0; i < 5; i++) { Sleep(16); core_update(0.016); }
        }
        for (int i = 0; i < 120; i++) { Sleep(16); core_update(0.016); }
        fb_render();
        return fb_dump_bmp(argv[2]);
    }
    core_pixel_mode = 1;
    core_init(argc, argv, 1);

    FARPROC dpi = GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDPIAware");
    if (dpi) ((BOOL (WINAPI *)(void))dpi)();

    bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
    bmi.bmiHeader.biWidth = SCREEN_W; bmi.bmiHeader.biHeight = -SCREEN_H;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;

    HINSTANCE inst = GetModuleHandle(NULL);
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "kollapse";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wc);
    RECT r = { 0, 0, SCREEN_W * 2, SCREEN_H * 2 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hw = CreateWindow("kollapse", "Kollapse", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    timeBeginPeriod(1);
    DWORD last = timeGetTime();
    for (;;) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { timeEndPeriod(1); return 0; }
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        DWORD now = timeGetTime();
        core_update((now - last) / 1000.0);
        last = now;
        fb_render();
        char title[160];                               /* now playing in the title bar */
        static char shown[160];
        core_title(title, sizeof title);
        if (strcmp(title, shown)) { SetWindowTextA(hw, title); strcpy(shown, title); }
        InvalidateRect(hw, NULL, FALSE);
        UpdateWindow(hw);
        DWORD spent = timeGetTime() - now;
        if (spent < 16) Sleep(16 - spent);
    }
}
