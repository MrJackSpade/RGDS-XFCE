#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>

// Minimal WGL OpenGL context creation and rendering test
// Reproduces Wine-Hangover GL context issues on Panfrost

static bool running = true;
static bool fullscreen = false;

void printPixelFormat(HDC hdc, int format) {
    PIXELFORMATDESCRIPTOR pfd;
    DescribePixelFormat(hdc, format, sizeof(pfd), &pfd);
    printf("  Format %d: color=%d depth=%d stencil=%d flags=0x%x\n",
           format, pfd.cColorBits, pfd.cDepthBits, pfd.cStencilBits, pfd.dwFlags);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            running = false;
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) running = false;
            if (wParam == 'F') fullscreen = !fullscreen;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void renderFrame(int frame) {
    // Animate color
    float r = (float)(frame % 256) / 255.0f;
    float g = (float)((frame + 85) % 256) / 255.0f;
    float b = (float)((frame + 170) % 256) / 255.0f;

    glClearColor(r * 0.3f, g * 0.3f, b * 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw a rotating triangle
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef((float)frame, 0.0f, 0.0f, 1.0f);

    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex2f(0.0f, 0.6f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex2f(-0.5f, -0.4f);
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex2f(0.5f, -0.4f);
    glEnd();
}

int main(int argc, char* argv[]) {
    printf("=== Wine OpenGL Render Test ===\n\n");

    // Check for fullscreen arg
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fullscreen") == 0) {
            fullscreen = true;
        }
    }

    // Get screen dimensions
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    printf("Screen: %dx%d\n", screenW, screenH);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "GLTest";
    wc.style = CS_OWNDC;
    RegisterClass(&wc);

    DWORD style = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    int winW = fullscreen ? screenW : 640;
    int winH = fullscreen ? screenH : 480;
    int winX = fullscreen ? 0 : (screenW - winW) / 2;
    int winY = fullscreen ? 0 : (screenH - winH) / 2;

    printf("Creating %s window %dx%d at %d,%d\n",
           fullscreen ? "FULLSCREEN" : "windowed", winW, winH, winX, winY);

    HWND hwnd = CreateWindow("GLTest", "GL Render Test",
                             style, winX, winY, winW, winH,
                             NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        printf("ERROR: CreateWindow failed (0x%lx)\n", GetLastError());
        return 1;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        printf("ERROR: GetDC failed\n");
        return 1;
    }

    // List first few pixel formats
    int numFormats = DescribePixelFormat(hdc, 1, 0, NULL);
    printf("Available pixel formats: %d\n", numFormats);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (!pixelFormat) {
        printf("ERROR: ChoosePixelFormat failed (0x%lx)\n", GetLastError());
        return 1;
    }
    printf("Using pixel format: %d\n", pixelFormat);
    printPixelFormat(hdc, pixelFormat);

    if (!SetPixelFormat(hdc, pixelFormat, &pfd)) {
        printf("ERROR: SetPixelFormat failed (0x%lx)\n", GetLastError());
        return 1;
    }

    HGLRC hglrc = wglCreateContext(hdc);
    if (!hglrc) {
        printf("ERROR: wglCreateContext failed (0x%lx)\n", GetLastError());
        return 1;
    }

    if (!wglMakeCurrent(hdc, hglrc)) {
        printf("ERROR: wglMakeCurrent failed (0x%lx)\n", GetLastError());
        return 1;
    }

    printf("\n=== OpenGL Info ===\n");
    printf("Vendor:   %s\n", glGetString(GL_VENDOR));
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    printf("Version:  %s\n", glGetString(GL_VERSION));

    glViewport(0, 0, winW, winH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

    printf("\n=== Rendering (ESC to quit, F for fullscreen toggle) ===\n");
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    int frame = 0;
    MSG msg;
    DWORD lastFpsTime = GetTickCount();
    int fps = 0;

    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) running = false;
        }

        renderFrame(frame++);
        SwapBuffers(hdc);
        fps++;

        DWORD now = GetTickCount();
        if (now - lastFpsTime >= 1000) {
            printf("FPS: %d\n", fps);
            fps = 0;
            lastFpsTime = now;
        }
    }

    printf("\n=== Cleanup ===\n");
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hglrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    printf("=== SUCCESS ===\n");
    return 0;
}
