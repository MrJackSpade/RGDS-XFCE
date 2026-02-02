#include <windows.h>
#include <ddraw.h>
#include <stdio.h>

// Minimal DirectDraw test mimicking Diablo 2's initialization

typedef HRESULT (WINAPI *DIRECTDRAWCREATE)(GUID*, LPDIRECTDRAW*, IUnknown*);

int main() {
    printf("=== DirectDraw Test ===\n\n");

    HMODULE hDDraw = LoadLibrary("ddraw.dll");
    if (!hDDraw) {
        printf("ERROR: Failed to load ddraw.dll\n");
        return 1;
    }
    printf("Loaded ddraw.dll\n");

    DIRECTDRAWCREATE pDirectDrawCreate =
        (DIRECTDRAWCREATE)GetProcAddress(hDDraw, "DirectDrawCreate");
    if (!pDirectDrawCreate) {
        printf("ERROR: DirectDrawCreate not found\n");
        return 1;
    }

    LPDIRECTDRAW pDD = NULL;
    HRESULT hr = pDirectDrawCreate(NULL, &pDD, NULL);
    if (FAILED(hr)) {
        printf("ERROR: DirectDrawCreate failed (0x%08lx)\n", hr);
        return 1;
    }
    printf("DirectDraw created\n");

    // Query for IDirectDraw7
    LPDIRECTDRAW7 pDD7 = NULL;
    hr = pDD->QueryInterface(IID_IDirectDraw7, (void**)&pDD7);
    if (FAILED(hr)) {
        printf("ERROR: QueryInterface IDirectDraw7 failed (0x%08lx)\n", hr);
        pDD->Release();
        return 1;
    }
    pDD->Release();
    printf("Got IDirectDraw7\n");

    // Get display mode info
    DDSURFACEDESC2 ddsd = {};
    ddsd.dwSize = sizeof(ddsd);
    hr = pDD7->GetDisplayMode(&ddsd);
    if (SUCCEEDED(hr)) {
        printf("Current display: %lux%lu @ %lu bpp\n",
               ddsd.dwWidth, ddsd.dwHeight, ddsd.ddpfPixelFormat.dwRGBBitCount);
    }

    // Create a window for fullscreen
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "DDTest";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("DDTest", "DDraw Test", WS_POPUP,
                             0, 0, 640, 480, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);
    printf("Created window\n");

    // Set cooperative level - fullscreen exclusive (like D2)
    printf("Setting cooperative level FULLSCREEN|EXCLUSIVE...\n");
    hr = pDD7->SetCooperativeLevel(hwnd, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE);
    if (FAILED(hr)) {
        printf("ERROR: SetCooperativeLevel failed (0x%08lx)\n", hr);
        pDD7->Release();
        return 1;
    }
    printf("SetCooperativeLevel succeeded\n");

    // Set display mode to 640x480x32 (like D2)
    printf("Setting display mode 640x480x32...\n");
    hr = pDD7->SetDisplayMode(640, 480, 32, 0, 0);
    if (FAILED(hr)) {
        printf("ERROR: SetDisplayMode failed (0x%08lx)\n", hr);
        pDD7->RestoreDisplayMode();
        pDD7->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
        pDD7->Release();
        return 1;
    }
    printf("SetDisplayMode succeeded\n");

    // Create primary surface with backbuffer (like D2)
    printf("Creating primary surface with backbuffer...\n");
    DDSURFACEDESC2 ddsdPrimary = {};
    ddsdPrimary.dwSize = sizeof(ddsdPrimary);
    ddsdPrimary.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    ddsdPrimary.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    ddsdPrimary.dwBackBufferCount = 1;

    LPDIRECTDRAWSURFACE7 pPrimary = NULL;
    hr = pDD7->CreateSurface(&ddsdPrimary, &pPrimary, NULL);
    if (FAILED(hr)) {
        printf("ERROR: CreateSurface (primary) failed (0x%08lx)\n", hr);
        pDD7->RestoreDisplayMode();
        pDD7->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
        pDD7->Release();
        return 1;
    }
    printf("Primary surface created\n");

    // Get back buffer
    DDSCAPS2 caps = {};
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    LPDIRECTDRAWSURFACE7 pBack = NULL;
    hr = pPrimary->GetAttachedSurface(&caps, &pBack);
    if (FAILED(hr)) {
        printf("ERROR: GetAttachedSurface (backbuffer) failed (0x%08lx)\n", hr);
    } else {
        printf("Got backbuffer\n");
    }

    // Try to render something
    printf("\nRendering test frames...\n");
    for (int frame = 0; frame < 60; frame++) {
        DDBLTFX fx = {};
        fx.dwSize = sizeof(fx);
        fx.dwFillColor = ((frame * 4) & 0xFF) << 16; // Red gradient

        hr = pBack ? pBack->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &fx) :
                     pPrimary->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &fx);
        if (FAILED(hr)) {
            printf("ERROR: Blt failed (0x%08lx)\n", hr);
            break;
        }

        if (pBack) {
            hr = pPrimary->Flip(NULL, DDFLIP_WAIT);
            if (FAILED(hr)) {
                printf("ERROR: Flip failed (0x%08lx)\n", hr);
                break;
            }
        }

        Sleep(16);
    }
    printf("Rendering complete\n");

    // Cleanup
    printf("\nCleanup...\n");
    if (pBack) pBack->Release();
    if (pPrimary) pPrimary->Release();
    pDD7->RestoreDisplayMode();
    pDD7->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    pDD7->Release();
    DestroyWindow(hwnd);
    FreeLibrary(hDDraw);

    printf("\n=== SUCCESS ===\n");
    return 0;
}
