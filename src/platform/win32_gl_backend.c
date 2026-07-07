#include "platform/render_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <gl/GL.h>

#define MAX_GL_TEXTURE_SETS 32
#define MAX_GL_TEXTURES_PER_SET 16

typedef struct GlTexture {
    GLuint id;
    int width;
    int height;
} GlTexture;

typedef struct GlTextureSet {
    int count;
    GlTexture textures[MAX_GL_TEXTURES_PER_SET];
} GlTextureSet;

static HWND gWindow;
static HDC gDeviceContext;
static HGLRC gGlContext;
static int gShouldQuit;
static int gWindowWidth;
static int gWindowHeight;
static GlTextureSet gTextureSets[MAX_GL_TEXTURE_SETS];

static unsigned int ReadBe32(const unsigned char *data, unsigned int offset) {
    return ((unsigned int)data[offset] << 24) |
           ((unsigned int)data[offset + 1] << 16) |
           ((unsigned int)data[offset + 2] << 8) |
           (unsigned int)data[offset + 3];
}

static unsigned short ReadBe16(const unsigned char *data, unsigned int offset) {
    return (unsigned short)(((unsigned int)data[offset] << 8) | (unsigned int)data[offset + 1]);
}

static void DecodeRgba32(const unsigned char *source, unsigned char *dest, int width, int height) {
    int blockX;
    int blockY;
    const unsigned char *block = source;

    for (blockY = 0; blockY < height; blockY += 4) {
        for (blockX = 0; blockX < width; blockX += 4) {
            int pixelY;
            int pixelX;

            for (pixelY = 0; pixelY < 4; pixelY++) {
                for (pixelX = 0; pixelX < 4; pixelX++) {
                    int x = blockX + pixelX;
                    int y = blockY + pixelY;
                    int index = pixelY * 4 + pixelX;

                    if (x < width && y < height) {
                        unsigned char alpha = block[index * 2 + 0];
                        unsigned char red = block[index * 2 + 1];
                        unsigned char green = block[32 + index * 2 + 0];
                        unsigned char blue = block[32 + index * 2 + 1];
                        unsigned char *pixel = dest + (y * width + x) * 4;

                        pixel[0] = red;
                        pixel[1] = green;
                        pixel[2] = blue;
                        pixel[3] = alpha;
                    }
                }
            }

            block += 64;
        }
    }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (message) {
        case WM_CLOSE:
        case WM_DESTROY:
            gShouldQuit = 1;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                gShouldQuit = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static void PumpMessages(void) {
    MSG message;

    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

int Platform_InitOpenGLWindow(const char *title, int width, int height) {
    WNDCLASSA windowClass;
    PIXELFORMATDESCRIPTOR pfd;
    int pixelFormat;
    RECT rect = { 0, 0, width, height };
    HINSTANCE instance = GetModuleHandleA(0);

    gWindowWidth = width;
    gWindowHeight = height;

    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "DDRIIStaticRecompOpenGL";
    windowClass.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&windowClass);

    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    gWindow = CreateWindowExA(0,
                              windowClass.lpszClassName,
                              title,
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              rect.right - rect.left,
                              rect.bottom - rect.top,
                              0,
                              0,
                              instance,
                              0);
    if (gWindow == 0) {
        printf("OpenGL backend: CreateWindowExA failed %lu\n", GetLastError());
        return 0;
    }

    gDeviceContext = GetDC(gWindow);

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    pixelFormat = ChoosePixelFormat(gDeviceContext, &pfd);
    if (pixelFormat == 0 || !SetPixelFormat(gDeviceContext, pixelFormat, &pfd)) {
        puts("OpenGL backend: pixel format setup failed");
        return 0;
    }

    gGlContext = wglCreateContext(gDeviceContext);
    if (gGlContext == 0 || !wglMakeCurrent(gDeviceContext, gGlContext)) {
        puts("OpenGL backend: context setup failed");
        return 0;
    }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return 1;
}

void Platform_ShutdownOpenGLWindow(void) {
    int i;
    int j;

    for (i = 0; i < MAX_GL_TEXTURE_SETS; i++) {
        for (j = 0; j < gTextureSets[i].count; j++) {
            if (gTextureSets[i].textures[j].id != 0) {
                glDeleteTextures(1, &gTextureSets[i].textures[j].id);
            }
        }
    }

    if (gGlContext != 0) {
        wglMakeCurrent(0, 0);
        wglDeleteContext(gGlContext);
        gGlContext = 0;
    }
    if (gWindow != 0 && gDeviceContext != 0) {
        ReleaseDC(gWindow, gDeviceContext);
        gDeviceContext = 0;
    }
    if (gWindow != 0) {
        DestroyWindow(gWindow);
        gWindow = 0;
    }
}

int Platform_ShouldQuit(void) {
    PumpMessages();
    return gShouldQuit;
}

void Platform_ApplyRenderConfig(unsigned int renderConfigColor) {
    (void)renderConfigColor;
}

int Platform_GetTextureDimensions(void *textureHandle, int textureIndex, int *width, int *height) {
    int slot = (int)(long)textureHandle;

    if (slot < 0 || slot >= MAX_GL_TEXTURE_SETS ||
        textureIndex < 0 || textureIndex >= gTextureSets[slot].count) {
        return 0;
    }

    if (width != 0) {
        *width = gTextureSets[slot].textures[textureIndex].width;
    }
    if (height != 0) {
        *height = gTextureSets[slot].textures[textureIndex].height;
    }
    return 1;
}

int Platform_BindTextureFromTextureSet(void *textureHandle, void *outTextureObject, int textureIndex) {
    int slot = (int)(long)textureHandle;
    (void)outTextureObject;

    if (slot < 0 || slot >= MAX_GL_TEXTURE_SETS ||
        textureIndex < 0 || textureIndex >= gTextureSets[slot].count) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, gTextureSets[slot].textures[textureIndex].id);
    return 1;
}

void Platform_DrawTexturedQuad(
    const RenderQuad *quad,
    const unsigned char *color,
    void *textureHandle,
    int textureIndex
) {
    float alpha = color[3] / 255.0f;
    (void)textureHandle;
    (void)textureIndex;

    PumpMessages();
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(quad->x, quad->y, quad->z);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(quad->x + quad->width, quad->y, quad->z);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(quad->x + quad->width, quad->y + quad->height, quad->z);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(quad->x, quad->y + quad->height, quad->z);
    glEnd();
}

void Platform_DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags) {
    unsigned int c = color != 0 ? *color : 0;
    (void)z;
    (void)flags;

    glDisable(GL_TEXTURE_2D);
    glColor4ub((GLubyte)((c >> 24) & 0xFF),
               (GLubyte)((c >> 16) & 0xFF),
               (GLubyte)((c >> 8) & 0xFF),
               (GLubyte)(c & 0xFF));
    glBegin(GL_QUADS);
    glVertex2i(x, y);
    glVertex2i(x + width, y);
    glVertex2i(x + width, y + height);
    glVertex2i(x, y + height);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    SwapBuffers(gDeviceContext);
}

unsigned int Platform_CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
) {
    unsigned char *data = (unsigned char *)resourceData;
    int size = resourceSizeOrTplBase;
    unsigned int textureCount;
    unsigned int tableOffset;
    unsigned int i;
    GlTextureSet *set;

    (void)textureManager;

    if (textureSlot >= MAX_GL_TEXTURE_SETS) {
        textureSlot = 0;
    }
    if (data == 0 || size < 0x20 || ReadBe32(data, 0) != 0x0020AF30) {
        printf("OpenGL backend: invalid TPL resource\n");
        return textureSlot;
    }

    set = &gTextureSets[textureSlot];
    memset(set, 0, sizeof(*set));

    textureCount = ReadBe32(data, 4);
    tableOffset = ReadBe32(data, 8);
    if (textureCount > MAX_GL_TEXTURES_PER_SET) {
        textureCount = MAX_GL_TEXTURES_PER_SET;
    }

    for (i = 0; i < textureCount; i++) {
        unsigned int descriptorOffset = tableOffset + i * 8;
        unsigned int textureHeaderOffset;
        unsigned int imageOffset;
        unsigned int format;
        unsigned short height;
        unsigned short width;
        unsigned char *pixels;
        GLuint id;

        if (descriptorOffset + 8 > (unsigned int)size) {
            break;
        }
        textureHeaderOffset = ReadBe32(data, descriptorOffset);
        if (textureHeaderOffset + 0x20 > (unsigned int)size) {
            break;
        }

        height = ReadBe16(data, textureHeaderOffset + 0);
        width = ReadBe16(data, textureHeaderOffset + 2);
        format = ReadBe32(data, textureHeaderOffset + 4);
        imageOffset = ReadBe32(data, textureHeaderOffset + 8);
        if (format != 6 || imageOffset >= (unsigned int)size) {
            printf("OpenGL backend: unsupported texture %u format=%u\n", i, format);
            continue;
        }

        pixels = (unsigned char *)malloc((size_t)width * (size_t)height * 4);
        if (pixels == 0) {
            continue;
        }
        DecodeRgba32(data + imageOffset, pixels, width, height);

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        free(pixels);

        set->textures[set->count].id = id;
        set->textures[set->count].width = width;
        set->textures[set->count].height = height;
        set->count++;
        printf("OpenGL backend: loaded TPL texture %u -> %dx%d\n", i, width, height);
    }

    return textureSlot;
}

int Platform_TextureSlotInitFromTpl(TextureSlotKnownFields *textureSlot) {
    (void)textureSlot;
    return 1;
}
