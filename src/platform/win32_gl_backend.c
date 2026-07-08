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
static int gConfirmPressed;
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

static void WritePixel(unsigned char *dest, int width, int x, int y,
                       unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    unsigned char *pixel = dest + (y * width + x) * 4;

    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = a;
}

static unsigned char Expand4(unsigned int value) {
    value &= 0xF;
    return (unsigned char)((value << 4) | value);
}

static unsigned char Expand5(unsigned int value) {
    value &= 0x1F;
    return (unsigned char)((value << 3) | (value >> 2));
}

static unsigned char Expand6(unsigned int value) {
    value &= 0x3F;
    return (unsigned char)((value << 2) | (value >> 4));
}

static void DecodeIa4(const unsigned char *source, unsigned char *dest, int width, int height) {
    int blockX;
    int blockY;
    const unsigned char *block = source;

    for (blockY = 0; blockY < height; blockY += 4) {
        for (blockX = 0; blockX < width; blockX += 8) {
            int y;
            int x;

            for (y = 0; y < 4; y++) {
                for (x = 0; x < 8; x++) {
                    int px = blockX + x;
                    int py = blockY + y;
                    unsigned char value = block[y * 8 + x];
                    unsigned char alpha = Expand4(value >> 4);
                    unsigned char intensity = Expand4(value);

                    if (px < width && py < height) {
                        WritePixel(dest, width, px, py, intensity, intensity, intensity, alpha);
                    }
                }
            }
            block += 32;
        }
    }
}

static void DecodeIa8(const unsigned char *source, unsigned char *dest, int width, int height) {
    int blockX;
    int blockY;
    const unsigned char *block = source;

    for (blockY = 0; blockY < height; blockY += 4) {
        for (blockX = 0; blockX < width; blockX += 4) {
            int y;
            int x;

            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    int px = blockX + x;
                    int py = blockY + y;
                    unsigned char alpha = block[(y * 4 + x) * 2 + 0];
                    unsigned char intensity = block[(y * 4 + x) * 2 + 1];

                    if (px < width && py < height) {
                        WritePixel(dest, width, px, py, intensity, intensity, intensity, alpha);
                    }
                }
            }
            block += 32;
        }
    }
}

static void DecodeRgb5a3(const unsigned char *source, unsigned char *dest, int width, int height) {
    int blockX;
    int blockY;
    const unsigned char *block = source;

    for (blockY = 0; blockY < height; blockY += 4) {
        for (blockX = 0; blockX < width; blockX += 4) {
            int y;
            int x;

            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    int px = blockX + x;
                    int py = blockY + y;
                    unsigned short value = ReadBe16(block, (unsigned int)((y * 4 + x) * 2));
                    unsigned char r;
                    unsigned char g;
                    unsigned char b;
                    unsigned char a;

                    if ((value & 0x8000) != 0) {
                        a = 0xFF;
                        r = Expand5(value >> 10);
                        g = Expand5(value >> 5);
                        b = Expand5(value);
                    }
                    else {
                        a = (unsigned char)(((value >> 12) & 7) * 255 / 7);
                        r = Expand4(value >> 8);
                        g = Expand4(value >> 4);
                        b = Expand4(value);
                    }

                    if (px < width && py < height) {
                        WritePixel(dest, width, px, py, r, g, b, a);
                    }
                }
            }
            block += 32;
        }
    }
}

static void DecodeCmprSubBlock(const unsigned char *block, unsigned char *dest, int width, int height,
                               int blockX, int blockY) {
    unsigned short c0 = ReadBe16(block, 0);
    unsigned short c1 = ReadBe16(block, 2);
    unsigned int bits = ReadBe32(block, 4);
    unsigned char color[4][4];
    int i;

    color[0][0] = Expand5(c0 >> 11);
    color[0][1] = Expand6(c0 >> 5);
    color[0][2] = Expand5(c0);
    color[0][3] = 0xFF;
    color[1][0] = Expand5(c1 >> 11);
    color[1][1] = Expand6(c1 >> 5);
    color[1][2] = Expand5(c1);
    color[1][3] = 0xFF;

    if (c0 > c1) {
        for (i = 0; i < 3; i++) {
            color[2][i] = (unsigned char)((2 * color[0][i] + color[1][i]) / 3);
            color[3][i] = (unsigned char)((color[0][i] + 2 * color[1][i]) / 3);
        }
        color[2][3] = 0xFF;
        color[3][3] = 0xFF;
    }
    else {
        for (i = 0; i < 3; i++) {
            color[2][i] = (unsigned char)((color[0][i] + color[1][i]) / 2);
            color[3][i] = 0;
        }
        color[2][3] = 0xFF;
        color[3][3] = 0;
    }

    for (i = 0; i < 16; i++) {
        int px = blockX + (i & 3);
        int py = blockY + (i >> 2);
        unsigned int index = (bits >> (30 - i * 2)) & 3;

        if (px < width && py < height) {
            WritePixel(dest, width, px, py,
                       color[index][0],
                       color[index][1],
                       color[index][2],
                       color[index][3]);
        }
    }
}

static void DecodeCmpr(const unsigned char *source, unsigned char *dest, int width, int height) {
    int blockX;
    int blockY;
    const unsigned char *block = source;

    for (blockY = 0; blockY < height; blockY += 8) {
        for (blockX = 0; blockX < width; blockX += 8) {
            DecodeCmprSubBlock(block + 0, dest, width, height, blockX, blockY);
            DecodeCmprSubBlock(block + 8, dest, width, height, blockX + 4, blockY);
            DecodeCmprSubBlock(block + 16, dest, width, height, blockX, blockY + 4);
            DecodeCmprSubBlock(block + 24, dest, width, height, blockX + 4, blockY + 4);
            block += 32;
        }
    }
}

static int DecodeTplTexture(const unsigned char *source, unsigned char *dest,
                            int width, int height, unsigned int format) {
    switch (format) {
        case 2:
            DecodeIa4(source, dest, width, height);
            return 1;
        case 3:
            DecodeIa8(source, dest, width, height);
            return 1;
        case 5:
            DecodeRgb5a3(source, dest, width, height);
            return 1;
        case 6:
            DecodeRgba32(source, dest, width, height);
            return 1;
        case 14:
            DecodeCmpr(source, dest, width, height);
            return 1;
        default:
            return 0;
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
            if (wParam == VK_RETURN ||
                wParam == VK_SPACE ||
                wParam == 'A' ||
                wParam == 'B') {
                gConfirmPressed = 1;
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

int Platform_ConsumeConfirmPressed(void) {
    int pressed;

    PumpMessages();
    pressed = gConfirmPressed;
    gConfirmPressed = 0;
    return pressed;
}

void Platform_ApplyRenderConfig(unsigned int renderConfigColor) {
    float r = (float)((renderConfigColor >> 24) & 0xFF) / 255.0f;
    float g = (float)((renderConfigColor >> 16) & 0xFF) / 255.0f;
    float b = (float)((renderConfigColor >> 8) & 0xFF) / 255.0f;
    float a = (float)(renderConfigColor & 0xFF) / 255.0f;

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Platform_BeginFrame(void) {
    PumpMessages();
}

void Platform_EndFrame(void) {
    SwapBuffers(gDeviceContext);
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
        if (imageOffset >= (unsigned int)size) {
            continue;
        }
        if (format != 2 && format != 3 && format != 5 && format != 6 && format != 14) {
            printf("OpenGL backend: unsupported texture %u format=%u\n", i, format);
            continue;
        }

        pixels = (unsigned char *)malloc((size_t)width * (size_t)height * 4);
        if (pixels == 0) {
            continue;
        }
        if (!DecodeTplTexture(data + imageOffset, pixels, width, height, format)) {
            free(pixels);
            continue;
        }

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
