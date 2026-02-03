/*
 * test_ddraw.c - Minimal DirectDraw test
 * Tests if DirectDraw can blit RGB565 to screen under Wine
 */

#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdint.h>

static LPDIRECTDRAW g_dd = NULL;
static LPDIRECTDRAW7 g_dd7 = NULL;
static LPDIRECTDRAWSURFACE7 g_primary = NULL;
static LPDIRECTDRAWSURFACE7 g_backbuf = NULL;
static LPDIRECTDRAWCLIPPER g_clipper = NULL;
static HWND g_hwnd = NULL;

#define WIDTH 640
#define HEIGHT 480

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main(void)
{
    HRESULT hr;
    DDSURFACEDESC2 ddsd;
    WNDCLASS wc = {0};
    RECT rect;
    MSG msg;
    int frame;

    LOG("=== DirectDraw Test ===");

    /* Create window */
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "DDTest";
    RegisterClass(&wc);

    rect.left = 0; rect.top = 0;
    rect.right = WIDTH; rect.bottom = HEIGHT;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindow("DDTest", "DirectDraw Test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!g_hwnd) {
        LOG("CreateWindow failed: %lu", GetLastError());
        return 1;
    }
    LOG("Window created: %p", g_hwnd);

    /* Create DirectDraw */
    hr = DirectDrawCreate(NULL, &g_dd, NULL);
    if (FAILED(hr)) {
        LOG("DirectDrawCreate failed: 0x%08lX", hr);
        return 1;
    }
    LOG("DirectDraw created");

    hr = IDirectDraw_QueryInterface(g_dd, &IID_IDirectDraw7, (void**)&g_dd7);
    if (FAILED(hr)) {
        LOG("QueryInterface DD7 failed: 0x%08lX", hr);
        return 1;
    }
    LOG("Got IDirectDraw7");

    hr = IDirectDraw7_SetCooperativeLevel(g_dd7, g_hwnd, DDSCL_NORMAL);
    if (FAILED(hr)) {
        LOG("SetCooperativeLevel failed: 0x%08lX", hr);
        return 1;
    }
    LOG("Cooperative level set");

    /* Create primary surface */
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    hr = IDirectDraw7_CreateSurface(g_dd7, &ddsd, &g_primary, NULL);
    if (FAILED(hr)) {
        LOG("CreateSurface (primary) failed: 0x%08lX", hr);
        return 1;
    }

    /* Query primary surface format */
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    hr = IDirectDrawSurface7_GetSurfaceDesc(g_primary, &ddsd);
    if (SUCCEEDED(hr)) {
        LOG("Primary: %lux%lu, %lu bpp, R=0x%lX G=0x%lX B=0x%lX",
            ddsd.dwWidth, ddsd.dwHeight,
            ddsd.ddpfPixelFormat.dwRGBBitCount,
            ddsd.ddpfPixelFormat.dwRBitMask,
            ddsd.ddpfPixelFormat.dwGBitMask,
            ddsd.ddpfPixelFormat.dwBBitMask);
    }
    LOG("Primary surface created");

    /* Create clipper for windowed mode - REQUIRED */
    hr = IDirectDraw7_CreateClipper(g_dd7, 0, &g_clipper, NULL);
    if (FAILED(hr)) {
        LOG("CreateClipper failed: 0x%08lX", hr);
        return 1;
    }
    hr = IDirectDrawClipper_SetHWnd(g_clipper, 0, g_hwnd);
    if (FAILED(hr)) {
        LOG("Clipper SetHWnd failed: 0x%08lX", hr);
        return 1;
    }
    hr = IDirectDrawSurface7_SetClipper(g_primary, g_clipper);
    if (FAILED(hr)) {
        LOG("SetClipper failed: 0x%08lX", hr);
        return 1;
    }
    LOG("Clipper attached");

    /* Create offscreen surface - 32-bit ARGB to match primary */
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    ddsd.dwWidth = WIDTH;
    ddsd.dwHeight = HEIGHT;
    ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
    ddsd.ddpfPixelFormat.dwRGBBitCount = 32;
    ddsd.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
    ddsd.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
    ddsd.ddpfPixelFormat.dwBBitMask = 0x000000FF;

    hr = IDirectDraw7_CreateSurface(g_dd7, &ddsd, &g_backbuf, NULL);
    if (FAILED(hr)) {
        LOG("CreateSurface (backbuf) failed: 0x%08lX", hr);
        return 1;
    }
    LOG("Back buffer created");

    /* Draw test frames - run for 30 seconds */
    for (frame = 0; frame < 900; frame++) {

        /* Process messages */
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        /* Lock and draw test pattern */
        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        hr = IDirectDrawSurface7_Lock(g_backbuf, NULL, &ddsd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
        if (FAILED(hr)) {
            LOG("Lock failed: 0x%08lX", hr);
            continue;
        }

        {
            uint32_t *pixels = (uint32_t*)ddsd.lpSurface;
            int pitch = ddsd.lPitch / 4;  /* 32-bit pixels */
            int x, y;
            uint32_t red = 0x00FF0000;
            uint32_t green = 0x0000FF00;

            /* Fill with blue, cycling intensity */
            uint32_t bg = 0x00000040 + (frame % 64);
            for (y = 0; y < HEIGHT; y++) {
                for (x = 0; x < WIDTH; x++) {
                    pixels[y * pitch + x] = bg;
                }
            }

            /* Red border */
            for (x = 0; x < WIDTH; x++) {
                pixels[x] = red;
                pixels[(HEIGHT-1) * pitch + x] = red;
            }
            for (y = 0; y < HEIGHT; y++) {
                pixels[y * pitch] = red;
                pixels[y * pitch + WIDTH - 1] = red;
            }

            /* Green square in center */
            for (y = HEIGHT/2 - 50; y < HEIGHT/2 + 50; y++) {
                for (x = WIDTH/2 - 50; x < WIDTH/2 + 50; x++) {
                    pixels[y * pitch + x] = green;
                }
            }

            if (frame == 0) {
                LOG("Drew to surface: lpSurface=%p pitch=%d", ddsd.lpSurface, ddsd.lPitch);
            }
        }

        IDirectDrawSurface7_Unlock(g_backbuf, NULL);

        /* Use GDI to blit - more compatible with Wine */
        {
            HDC hdcSurf, hdcWnd;
            hr = IDirectDrawSurface7_GetDC(g_backbuf, &hdcSurf);
            if (SUCCEEDED(hr)) {
                hdcWnd = GetDC(g_hwnd);
                if (hdcWnd) {
                    BitBlt(hdcWnd, 0, 0, WIDTH, HEIGHT, hdcSurf, 0, 0, SRCCOPY);
                    ReleaseDC(g_hwnd, hdcWnd);
                }
                IDirectDrawSurface7_ReleaseDC(g_backbuf, hdcSurf);
                if (frame == 0) {
                    LOG("GDI BitBlt done");
                }
            } else if (frame == 0) {
                LOG("GetDC failed: 0x%08lX", hr);
            }
        }

        Sleep(16);
    }

done:
    LOG("Cleaning up...");
    if (g_backbuf) IDirectDrawSurface7_Release(g_backbuf);
    if (g_clipper) IDirectDrawClipper_Release(g_clipper);
    if (g_primary) IDirectDrawSurface7_Release(g_primary);
    if (g_dd7) IDirectDraw7_Release(g_dd7);
    if (g_dd) IDirectDraw_Release(g_dd);
    if (g_hwnd) DestroyWindow(g_hwnd);

    LOG("=== Test complete ===");
    return 0;
}
