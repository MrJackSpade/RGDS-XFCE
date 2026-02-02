/*
 * display_ddraw.c - DirectDraw display output for software Glide3x
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Uses DirectDraw to blit RGB565 framebuffer to screen.
 * This is a single syscall per frame, minimizing Wine overhead.
 */

#include <windows.h>
#include <ddraw.h>
#include <stdint.h>
#include <stdio.h>

/* DirectDraw interfaces */
static LPDIRECTDRAW g_dd = NULL;
static LPDIRECTDRAW7 g_dd7 = NULL;
static LPDIRECTDRAWSURFACE7 g_primary = NULL;
static LPDIRECTDRAWSURFACE7 g_backbuf = NULL;
static HWND g_hwnd = NULL;
static int g_width = 0;
static int g_height = 0;
static int g_window_owned = 0;

/* Debug logging - write to same log file as glide3x.c */
/* Shared debug logging from glide3x.c */
extern void debug_log(const char *msg);

static void display_log(const char *msg)
{
    fprintf(stderr, "%s", msg); /* Force stderr output */
    debug_log(msg);
}

/* Forward declaration */
void display_shutdown(void);

/* Window procedure */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE:
        display_log("display_ddraw: WndProc received WM_CLOSE\n");
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        display_log("display_ddraw: WndProc received WM_DESTROY\n");
        if (hwnd == g_hwnd) {
            display_log("display_ddraw: wndproc clearing g_hwnd\n");
            g_hwnd = NULL;
        }
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* Create output window */
static HWND create_window(int width, int height, HWND hWindow)
{
    char dbg[128];

    /* If external window provided, use it */
    if (hWindow) {
        snprintf(dbg, sizeof(dbg), "display_ddraw: Using external window %p\n", (void*)hWindow);
        display_log(dbg);
        g_window_owned = 0;
        
        /* Ensure it's visible and sized correctly? 
           Diablo II might manage this itself, but let's safe-guard. */
        /* SetWindowPos(hWindow, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW); */
        /* Better not to resize external window forcibly unless necessary, 
           as it might mess up the app's layout. But for fullscreen games usually we want to.
           Let's just update our tracking variable. */
        return hWindow;
    }

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "Glide3xWindow";

    if (!RegisterClass(&wc)) {
        /* Class may already be registered */
    }

    /* Compute window size for client area */
    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    
    int win_width = rect.right - rect.left;
    int win_height = rect.bottom - rect.top;
    
    snprintf(dbg, sizeof(dbg), "display_ddraw: create_window requesting %dx%d (client %dx%d)\n", 
             win_width, win_height, width, height);
    display_log(dbg);

    /* Reuse existing window if available */
    if (g_hwnd) {
        display_log("display_ddraw: Reusing existing window\n");
        /* Simply resize and show */
        SetWindowPos(g_hwnd, NULL, 0, 0, win_width, win_height, SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        return g_hwnd;
    }

    /* Use 0,0 explicitly instead of CW_USEDEFAULT to avoid weird placement/sizing logic on some Wine setups */
    HWND hwnd = CreateWindow(
        "Glide3xWindow",
        "Glide3x Software Renderer",
        WS_OVERLAPPEDWINDOW,
        0, 0,
        win_width, win_height,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (hwnd) {
        g_window_owned = 1;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        
        /* Verify actual size */
        RECT client_rect;
        GetClientRect(hwnd, &client_rect);
        snprintf(dbg, sizeof(dbg), "display_ddraw: Created window client rect %ldx%ld\n", 
                 client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
        display_log(dbg);
        
        /* Force resize if mismatch */
        if ((client_rect.right - client_rect.left) != width || (client_rect.bottom - client_rect.top) != height) {
            display_log("display_ddraw: Window size mismatch, attempting to force resize...\n");
            if (!SetWindowPos(hwnd, NULL, 0, 0, win_width, win_height, SWP_NOMOVE | SWP_NOZORDER)) {
                snprintf(dbg, sizeof(dbg), "display_ddraw: SetWindowPos failed (Error %ld)\n", GetLastError());
                display_log(dbg);
            } else {
                display_log("display_ddraw: SetWindowPos succeeded. Re-verifying...\n");
                GetClientRect(hwnd, &client_rect);
                snprintf(dbg, sizeof(dbg), "display_ddraw: New client rect %ldx%ld\n", 
                         client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
                display_log(dbg);
            }
        }
    }

    return hwnd;
}

/* Initialize DirectDraw */
int display_init(int width, int height, HWND hWindow)
{
    HRESULT hr;
    DDSURFACEDESC2 ddsd;

    g_width = width;
    g_height = height;

    /* Create window */
    g_hwnd = create_window(width, height, hWindow);
    if (!g_hwnd) {
        OutputDebugStringA("display_ddraw: Failed to create window\n");
        return 0;
    }

    /* Create DirectDraw object */
    hr = DirectDrawCreate(NULL, &g_dd, NULL);
    if (FAILED(hr)) {
        OutputDebugStringA("display_ddraw: DirectDrawCreate failed\n");
        return 0;
    }

    /* Get IDirectDraw7 interface */
    hr = IDirectDraw_QueryInterface(g_dd, &IID_IDirectDraw7, (void**)&g_dd7);
    if (FAILED(hr)) {
        OutputDebugStringA("display_ddraw: QueryInterface for DD7 failed\n");
        IDirectDraw_Release(g_dd);
        g_dd = NULL;
        return 0;
    }

    /* Set cooperative level - windowed mode */
    hr = IDirectDraw7_SetCooperativeLevel(g_dd7, g_hwnd, DDSCL_NORMAL);
    if (FAILED(hr)) {
        OutputDebugStringA("display_ddraw: SetCooperativeLevel failed\n");
        goto fail;
    }

    /* Create primary surface */
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    hr = IDirectDraw7_CreateSurface(g_dd7, &ddsd, &g_primary, NULL);
    if (FAILED(hr)) {
        OutputDebugStringA("display_ddraw: CreateSurface (primary) failed\n");
        goto fail;
    }

    /* Create offscreen back buffer for rendering */
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    ddsd.dwWidth = width;
    ddsd.dwHeight = height;

    /* Request RGB565 format */
    ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
    ddsd.ddpfPixelFormat.dwRGBBitCount = 16;
    ddsd.ddpfPixelFormat.dwRBitMask = 0xF800;
    ddsd.ddpfPixelFormat.dwGBitMask = 0x07E0;
    ddsd.ddpfPixelFormat.dwBBitMask = 0x001F;

    hr = IDirectDraw7_CreateSurface(g_dd7, &ddsd, &g_backbuf, NULL);
    if (FAILED(hr)) {
        OutputDebugStringA("display_ddraw: CreateSurface (backbuf) failed\n");
        goto fail;
    }

    return 1;

fail:
    display_shutdown();
    return 0;
}

/* Shutdown DirectDraw */
void display_shutdown(void)
{
    if (g_backbuf) {
        IDirectDrawSurface7_Release(g_backbuf);
        g_backbuf = NULL;
    }
    if (g_primary) {
        IDirectDrawSurface7_Release(g_primary);
        g_primary = NULL;
    }
    if (g_dd7) {
        IDirectDraw7_Release(g_dd7);
        g_dd7 = NULL;
    }
    if (g_dd) {
        IDirectDraw_Release(g_dd);
        g_dd = NULL;
    }
    display_log("display_ddraw: display_shutdown complete (window preserved)\n");
}

/* Explicitly destroy window (called on DLL detach) */
void display_destroy_window(void)
{
    display_log("display_ddraw: display_destroy_window called\n");
    if (g_hwnd) {
        if (g_window_owned) {
            display_log("display_ddraw: Destroying owned window\n");
            DestroyWindow(g_hwnd);
        } else {
            display_log("display_ddraw: Detaching from external window (not destroying)\n");
        }
        g_hwnd = NULL;
    }
}

static int g_present_count = 0;

/* Present framebuffer to screen */
void display_present(uint16_t *framebuffer, int width, int height)
{
    HRESULT hr;
    DDSURFACEDESC2 ddsd;
    MSG msg;
    char dbg[256];

    g_present_count++;
    if (g_present_count <= 20) {
        snprintf(dbg, sizeof(dbg), "display_present #%d: %dx%d fb=%p\n",
                 g_present_count, width, height, (void*)framebuffer);
        display_log(dbg);
    }

    if (!g_backbuf || !g_primary) return;

    /* Process window messages */
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    /* Lock the back buffer and copy framebuffer data */
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    hr = IDirectDrawSurface7_Lock(g_backbuf, NULL, &ddsd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (FAILED(hr)) {
        return;
    }

    /* Copy RGB565 data */
    uint16_t *dst = (uint16_t*)ddsd.lpSurface;
    int dst_pitch_pixels = ddsd.lPitch / 2;  /* pitch in 16-bit words */

    /* Debug pitch mismatch once */
    if (g_present_count == 1 || dst_pitch_pixels < width) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "display_present: width=%d, pitch_pixels=%d (bytes=%ld)\n", 
                 width, dst_pitch_pixels, ddsd.lPitch);
        display_log(dbg);
    }

    /* Clamp copy to prevent wrapping ("ZARDBLIZ" fix) */
    int copy_width = (width < dst_pitch_pixels) ? width : dst_pitch_pixels;

    for (int y = 0; y < height; y++) {
        memcpy(&dst[y * dst_pitch_pixels], &framebuffer[y * width], copy_width * 2);
    }

    IDirectDrawSurface7_Unlock(g_backbuf, NULL);

    /* Use GDI StretchBlt to handle window resizing/mismatches */
    {
        HDC hdcSurf, hdcWnd;
        hr = IDirectDrawSurface7_GetDC(g_backbuf, &hdcSurf);
        if (SUCCEEDED(hr)) {
            hdcWnd = GetDC(g_hwnd);
            if (hdcWnd) {
                RECT client_rect;
                GetClientRect(g_hwnd, &client_rect);
                int client_w = client_rect.right - client_rect.left;
                int client_h = client_rect.bottom - client_rect.top;

                /* Log only on size change or periodically to avoid spam */
                if (width != client_w || height != client_h) {
                     if (g_present_count % 60 == 0) { /* Log once every 60 frames if scaling */
                         char dbg[256];
                         snprintf(dbg, sizeof(dbg), "display_present: Scaling %dx%d -> %dx%d\n", width, height, client_w, client_h);
                         display_log(dbg);
                     }
                     SetStretchBltMode(hdcWnd, COLORONCOLOR); /* Better quality for shrinking? or HALFTONE? COLORONCOLOR is faster */
                }

                StretchBlt(hdcWnd, 0, 0, client_w, client_h, hdcSurf, 0, 0, width, height, SRCCOPY);
                ReleaseDC(g_hwnd, hdcWnd);
            }
            IDirectDrawSurface7_ReleaseDC(g_backbuf, hdcSurf);
        }
    }
}
