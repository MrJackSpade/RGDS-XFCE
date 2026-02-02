#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>

// Direct3D test mimicking Diablo 2's D3D initialization

typedef HRESULT (WINAPI *DIRECTDRAWCREATE)(GUID*, LPDIRECTDRAW*, IUnknown*);

HRESULT CALLBACK EnumDevicesCallback(
    LPGUID lpGUID, LPSTR lpDeviceDescription, LPSTR lpDeviceName,
    LPD3DDEVICEDESC lpD3DHWDeviceDesc, LPD3DDEVICEDESC lpD3DHELDeviceDesc,
    LPVOID lpContext)
{
    printf("  Device: %s\n", lpDeviceName);
    printf("    Desc: %s\n", lpDeviceDescription);
    if (lpD3DHWDeviceDesc && lpD3DHWDeviceDesc->dwSize > 0) {
        printf("    HW caps: color model %lu, flags 0x%lx\n",
               lpD3DHWDeviceDesc->dcmColorModel, lpD3DHWDeviceDesc->dwFlags);
    }
    if (lpD3DHELDeviceDesc && lpD3DHELDeviceDesc->dwSize > 0) {
        printf("    HEL caps: color model %lu, flags 0x%lx\n",
               lpD3DHELDeviceDesc->dcmColorModel, lpD3DHELDeviceDesc->dwFlags);
    }
    return D3DENUMRET_OK;
}

int main() {
    printf("=== Direct3D Test ===\n\n");

    HMODULE hDDraw = LoadLibrary("ddraw.dll");
    if (!hDDraw) {
        printf("ERROR: Failed to load ddraw.dll\n");
        return 1;
    }

    DIRECTDRAWCREATE pDirectDrawCreate =
        (DIRECTDRAWCREATE)GetProcAddress(hDDraw, "DirectDrawCreate");

    LPDIRECTDRAW pDD = NULL;
    HRESULT hr = pDirectDrawCreate(NULL, &pDD, NULL);
    if (FAILED(hr)) {
        printf("ERROR: DirectDrawCreate failed\n");
        return 1;
    }
    printf("DirectDraw created\n");

    // Get IDirectDraw4 (what D2 uses)
    LPDIRECTDRAW4 pDD4 = NULL;
    hr = pDD->QueryInterface(IID_IDirectDraw4, (void**)&pDD4);
    if (FAILED(hr)) {
        printf("ERROR: QueryInterface IDirectDraw4 failed\n");
        pDD->Release();
        return 1;
    }
    pDD->Release();
    printf("Got IDirectDraw4\n");

    // Query for Direct3D3 (what D2 uses)
    LPDIRECT3D3 pD3D3 = NULL;
    hr = pDD4->QueryInterface(IID_IDirect3D3, (void**)&pD3D3);
    if (FAILED(hr)) {
        printf("ERROR: QueryInterface IDirect3D3 failed (0x%08lx)\n", hr);
        pDD4->Release();
        return 1;
    }
    printf("Got IDirect3D3\n");

    // Enumerate D3D devices
    printf("\nEnumerating D3D devices:\n");
    hr = pD3D3->EnumDevices(EnumDevicesCallback, NULL);
    if (FAILED(hr)) {
        printf("ERROR: EnumDevices failed (0x%08lx)\n", hr);
    }

    // Create window
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "D3DTest";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("D3DTest", "D3D Test", WS_POPUP,
                             0, 0, 640, 480, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);

    // Set cooperative level
    hr = pDD4->SetCooperativeLevel(hwnd, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE);
    if (FAILED(hr)) {
        printf("ERROR: SetCooperativeLevel failed\n");
        pD3D3->Release();
        pDD4->Release();
        return 1;
    }
    printf("\nCooperative level set\n");

    // Set display mode
    hr = pDD4->SetDisplayMode(640, 480, 16, 0, 0);
    if (FAILED(hr)) {
        printf("ERROR: SetDisplayMode failed\n");
        pD3D3->Release();
        pDD4->Release();
        return 1;
    }
    printf("Display mode set to 640x480x16\n");

    // Create primary surface with zbuffer
    DDSURFACEDESC2 ddsd = {};
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP |
                          DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
    ddsd.dwBackBufferCount = 1;

    LPDIRECTDRAWSURFACE4 pPrimary = NULL;
    hr = pDD4->CreateSurface(&ddsd, &pPrimary, NULL);
    if (FAILED(hr)) {
        printf("ERROR: CreateSurface (primary) failed (0x%08lx)\n", hr);
        pDD4->RestoreDisplayMode();
        pD3D3->Release();
        pDD4->Release();
        return 1;
    }
    printf("Primary surface created\n");

    // Get backbuffer
    DDSCAPS2 caps = {};
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    LPDIRECTDRAWSURFACE4 pBack = NULL;
    hr = pPrimary->GetAttachedSurface(&caps, &pBack);
    if (FAILED(hr)) {
        printf("ERROR: GetAttachedSurface failed (0x%08lx)\n", hr);
    } else {
        printf("Got backbuffer\n");
    }

    // Create Z-buffer
    printf("\nCreating Z-buffer...\n");
    DDSURFACEDESC2 zDesc = {};
    zDesc.dwSize = sizeof(zDesc);
    zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    zDesc.dwWidth = 640;
    zDesc.dwHeight = 480;
    zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
    zDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    zDesc.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
    zDesc.ddpfPixelFormat.dwZBufferBitDepth = 16;

    LPDIRECTDRAWSURFACE4 pZBuffer = NULL;
    hr = pDD4->CreateSurface(&zDesc, &pZBuffer, NULL);
    if (FAILED(hr)) {
        printf("Z-buffer in video memory failed, trying system memory...\n");
        zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
        hr = pDD4->CreateSurface(&zDesc, &pZBuffer, NULL);
    }
    if (FAILED(hr)) {
        printf("ERROR: CreateSurface (zbuffer) failed (0x%08lx)\n", hr);
    } else {
        printf("Z-buffer created\n");
        // Attach to backbuffer
        hr = pBack->AddAttachedSurface(pZBuffer);
        if (FAILED(hr)) {
            printf("ERROR: AddAttachedSurface (zbuffer) failed (0x%08lx)\n", hr);
        }
    }

    // Try to create D3D device
    printf("\nCreating D3D device (HAL)...\n");
    LPDIRECT3DDEVICE3 pDevice = NULL;
    hr = pD3D3->CreateDevice(IID_IDirect3DHALDevice, pBack, &pDevice, NULL);
    if (FAILED(hr)) {
        printf("HAL device failed (0x%08lx), trying RGB...\n", hr);
        hr = pD3D3->CreateDevice(IID_IDirect3DRGBDevice, pBack, &pDevice, NULL);
    }
    if (FAILED(hr)) {
        printf("ERROR: CreateDevice failed (0x%08lx)\n", hr);
    } else {
        printf("D3D device created!\n");

        // Render a frame
        LPDIRECT3DVIEWPORT3 pViewport = NULL;
        pD3D3->CreateViewport(&pViewport, NULL);
        if (pViewport) {
            D3DVIEWPORT2 vp = {};
            vp.dwSize = sizeof(vp);
            vp.dwX = 0;
            vp.dwY = 0;
            vp.dwWidth = 640;
            vp.dwHeight = 480;
            vp.dvClipX = -1.0f;
            vp.dvClipY = 1.0f;
            vp.dvClipWidth = 2.0f;
            vp.dvClipHeight = 2.0f;
            vp.dvMinZ = 0.0f;
            vp.dvMaxZ = 1.0f;
            pViewport->SetViewport2(&vp);
            pDevice->AddViewport(pViewport);
            pDevice->SetCurrentViewport(pViewport);

            // Clear and render
            D3DRECT rect = {0, 0, 640, 480};
            pViewport->Clear2(1, &rect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF0000FF, 1.0f, 0);

            pDevice->BeginScene();
            // ... would draw here ...
            pDevice->EndScene();

            printf("Rendered a frame!\n");

            pViewport->Release();
        }

        pDevice->Release();
    }

    // Cleanup
    printf("\nCleanup...\n");
    if (pZBuffer) pZBuffer->Release();
    if (pBack) pBack->Release();
    if (pPrimary) pPrimary->Release();
    pDD4->RestoreDisplayMode();
    pDD4->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    pD3D3->Release();
    pDD4->Release();
    DestroyWindow(hwnd);

    printf("\n=== DONE ===\n");
    return 0;
}
