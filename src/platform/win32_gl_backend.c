#include "platform/render_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include <gl/GL.h>

#define MAX_GL_TEXTURE_SETS 512
#define MAX_GL_TEXTURES_PER_SET 128
#define MAX_MOVIE_AUDIO_BUFFERS 16

/* Wii Remote (WPAD/KPAD, held upright) button bits, as the DOL tests them. Confirmed
   by the CSelMode neighbour table at 0x80272348 (1/2 move horizontally, 4/8 vertically)
   and the prompt controller 0x80100DCC (2|4 = next, 1|8 = previous). */
#define MENU_INPUT_LEFT 0x00000001U
#define MENU_INPUT_RIGHT 0x00000002U
#define MENU_INPUT_DOWN 0x00000004U
#define MENU_INPUT_UP 0x00000008U
#define MENU_INPUT_BACK 0x00000400U
#define MENU_INPUT_CONFIRM 0x00000800U

#define HOST_XINPUT_GAMEPAD_DPAD_UP 0x0001
#define HOST_XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define HOST_XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define HOST_XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define HOST_XINPUT_GAMEPAD_START 0x0010
#define HOST_XINPUT_GAMEPAD_BACK 0x0020
#define HOST_XINPUT_GAMEPAD_A 0x1000
#define HOST_XINPUT_GAMEPAD_B 0x2000

typedef struct HostXInputGamepad {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} HostXInputGamepad;

typedef struct HostXInputState {
    DWORD dwPacketNumber;
    HostXInputGamepad Gamepad;
} HostXInputState;

typedef DWORD (WINAPI *HostXInputGetStateProc)(DWORD userIndex, HostXInputState *state);

typedef struct GlTexture {
    GLuint id;
    int width;
    int height;
    float uMax;
    float vMax;
} GlTexture;

typedef struct GlTextureSet {
    int count;
    GlTexture textures[MAX_GL_TEXTURES_PER_SET];
} GlTextureSet;

typedef struct GlCapturedTexture {
    GLuint id;
    int width;
    int height;
} GlCapturedTexture;

static HWND gWindow;
static HDC gDeviceContext;
static HGLRC gGlContext;
static int gShouldQuit;
static int gConfirmPressed;
static int gTriedXInputLoad;
static int gWindowWidth;
static int gWindowHeight;
static int gLogicalProjectionWidth;
static int gLogicalProjectionHeight;
static int gLogicalProjectionDirty;
static int gViewportX;
static int gViewportY;
static int gViewportWidth;
static int gViewportHeight;
static HMODULE gXInputModule;
static HostXInputGetStateProc gXInputGetState;
static unsigned int gPreviousMenuInputMask;
static GLuint gBoundTextureId;
static GLuint gMovieYuvTextureId;
static unsigned char *gMovieYuvRgba;
static int gMovieYuvWidth;
static int gMovieYuvHeight;
static int gMovieYuvFrameToken = -1;
static HWAVEOUT gMovieWaveOut;
static int gMovieWaveChannels;
static int gMovieWaveRate;
static WAVEHDR gMovieWaveHeaders[MAX_MOVIE_AUDIO_BUFFERS];
static short *gMovieWaveBuffers[MAX_MOVIE_AUDIO_BUFFERS];
static GlCapturedTexture gFrameCaptureTexture;
static unsigned char *gFrameCaptureReadPixels;
static size_t gFrameCaptureReadPixelsSize;
static unsigned char *gFrameCaptureScaledPixels;
static size_t gFrameCaptureScaledPixelsSize;
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

static void WritePixel(unsigned char *dest, int width, int x, int y,
                       unsigned char r, unsigned char g, unsigned char b, unsigned char a);
static unsigned char Expand4(unsigned int value);
static unsigned char Expand5(unsigned int value);
static unsigned char Expand6(unsigned int value);

static unsigned char *EnsureScratchBuffer(unsigned char **buffer, size_t *currentSize, size_t requiredSize) {
    unsigned char *newBuffer;

    if (requiredSize == 0) {
        return 0;
    }
    if (*currentSize >= requiredSize && *buffer != 0) {
        return *buffer;
    }

    newBuffer = (unsigned char *)realloc(*buffer, requiredSize);
    if (newBuffer == 0) {
        return 0;
    }

    *buffer = newBuffer;
    *currentSize = requiredSize;
    return *buffer;
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

static void DecodeI4(const unsigned char *source, unsigned char *dest, int width, int height) {
    int blockX;
    int blockY;
    const unsigned char *block = source;

    for (blockY = 0; blockY < height; blockY += 8) {
        for (blockX = 0; blockX < width; blockX += 8) {
            int y;
            int x;

            for (y = 0; y < 8; y++) {
                for (x = 0; x < 8; x += 2) {
                    unsigned char packed = block[y * 4 + x / 2];
                    unsigned char high = Expand4(packed >> 4);
                    unsigned char low = Expand4(packed);
                    int px = blockX + x;
                    int py = blockY + y;

                    if (px < width && py < height) {
                        WritePixel(dest, width, px, py, high, high, high, 0xff);
                    }
                    if (px + 1 < width && py < height) {
                        WritePixel(dest, width, px + 1, py, low, low, low, 0xff);
                    }
                }
            }
            block += 32;
        }
    }
}

static void DecodeI8(const unsigned char *source, unsigned char *dest, int width, int height) {
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
                    unsigned char intensity = block[y * 8 + x];

                    if (px < width && py < height) {
                        WritePixel(dest, width, px, py, intensity, intensity, intensity, 0xff);
                    }
                }
            }
            block += 32;
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

static unsigned char ClampByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (unsigned char)value;
}

static void CleanupCompletedMovieAudioBuffers(void) {
    int i;

    for (i = 0; i < MAX_MOVIE_AUDIO_BUFFERS; i++) {
        if (gMovieWaveBuffers[i] != 0 &&
            (gMovieWaveHeaders[i].dwFlags & WHDR_DONE) != 0) {
            if (gMovieWaveOut != 0 &&
                (gMovieWaveHeaders[i].dwFlags & WHDR_PREPARED) != 0) {
                waveOutUnprepareHeader(gMovieWaveOut, &gMovieWaveHeaders[i], sizeof(WAVEHDR));
            }
            free(gMovieWaveBuffers[i]);
            gMovieWaveBuffers[i] = 0;
            memset(&gMovieWaveHeaders[i], 0, sizeof(gMovieWaveHeaders[i]));
        }
    }
}

static void ResetMovieAudioOutput(void) {
    int i;

    if (gMovieWaveOut != 0) {
        waveOutReset(gMovieWaveOut);
        for (i = 0; i < MAX_MOVIE_AUDIO_BUFFERS; i++) {
            if (gMovieWaveBuffers[i] != 0 &&
                (gMovieWaveHeaders[i].dwFlags & WHDR_PREPARED) != 0) {
                waveOutUnprepareHeader(gMovieWaveOut, &gMovieWaveHeaders[i], sizeof(WAVEHDR));
            }
        }
        waveOutClose(gMovieWaveOut);
        gMovieWaveOut = 0;
    }
    for (i = 0; i < MAX_MOVIE_AUDIO_BUFFERS; i++) {
        free(gMovieWaveBuffers[i]);
        gMovieWaveBuffers[i] = 0;
        memset(&gMovieWaveHeaders[i], 0, sizeof(gMovieWaveHeaders[i]));
    }
    gMovieWaveChannels = 0;
    gMovieWaveRate = 0;
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

static void DecodeRgb565(const unsigned char *source, unsigned char *dest, int width, int height) {
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

                    if (px < width && py < height) {
                        WritePixel(dest, width, px, py,
                                   Expand5(value >> 11),
                                   Expand6(value >> 5),
                                   Expand5(value),
                                   0xff);
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
        case 0:
            DecodeI4(source, dest, width, height);
            return 1;
        case 1:
            DecodeI8(source, dest, width, height);
            return 1;
        case 2:
            DecodeIa4(source, dest, width, height);
            return 1;
        case 3:
            DecodeIa8(source, dest, width, height);
            return 1;
        case 4:
            DecodeRgb565(source, dest, width, height);
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

static int Platform_IsKeyHeld(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

static void Platform_LoadXInput(void) {
    static const char *moduleNames[] = {
        "xinput1_4.dll",
        "xinput9_1_0.dll",
        "xinput1_3.dll",
    };
    int i;

    if (gTriedXInputLoad) {
        return;
    }
    gTriedXInputLoad = 1;

    for (i = 0; i < (int)(sizeof(moduleNames) / sizeof(moduleNames[0])); i++) {
        gXInputModule = LoadLibraryA(moduleNames[i]);
        if (gXInputModule != 0) {
            gXInputGetState = (HostXInputGetStateProc)GetProcAddress(gXInputModule, "XInputGetState");
            if (gXInputGetState != 0) {
                return;
            }
            FreeLibrary(gXInputModule);
            gXInputModule = 0;
        }
    }
}

static unsigned int gPlatformFrameCounter;

static unsigned int Platform_ReadScriptedInputMask(void) {
    /* Debug aid: DDRII_INPUT_SCRIPT="frame:keys,frame:keys,..." holds the given
       buttons for 4 frames from each frame number (keys: A B U D L R), so headless
       runs (DDRII_FRAME_DUMP) can drive menus without a focused window. */
    static int parsed;
    static unsigned int frames[64];
    static unsigned int masks[64];
    static int count;
    unsigned int mask = 0;
    int i;

    if (!parsed) {
        const char *script = getenv("DDRII_INPUT_SCRIPT");
        parsed = 1;
        while (script != 0 && *script != '\0' && count < 64) {
            char *end;
            unsigned int frame = (unsigned int)strtoul(script, &end, 10);
            unsigned int keys = 0;
            if (end == script || *end != ':') {
                break;
            }
            for (script = end + 1; *script != '\0' && *script != ','; script++) {
                switch (*script) {
                    case 'A': keys |= MENU_INPUT_CONFIRM; break;
                    case 'B': keys |= MENU_INPUT_BACK; break;
                    case 'U': keys |= MENU_INPUT_UP; break;
                    case 'D': keys |= MENU_INPUT_DOWN; break;
                    case 'L': keys |= MENU_INPUT_LEFT; break;
                    case 'R': keys |= MENU_INPUT_RIGHT; break;
                    default: break;
                }
            }
            frames[count] = frame;
            masks[count++] = keys;
            if (*script == ',') {
                script++;
            }
        }
    }
    for (i = 0; i < count; i++) {
        if (gPlatformFrameCounter >= frames[i] && gPlatformFrameCounter < frames[i] + 4) {
            mask |= masks[i];
        }
    }
    return mask;
}

static int Platform_ReadScriptedPointer(float *x, float *y, int *active) {
    /* Debug aid: DDRII_POINTER_SCRIPT="frame:x,y;frame:off;..." places the Wii
       pointer (logical 640x480 coordinates) from each frame on, for headless runs. */
    static int parsed;
    static unsigned int frames[64];
    static float xs[64];
    static float ys[64];
    static int onScreen[64];
    static int count;
    int i;
    int found = -1;

    if (!parsed) {
        const char *script = getenv("DDRII_POINTER_SCRIPT");
        parsed = 1;
        while (script != 0 && *script != '\0' && count < 64) {
            char *end;
            frames[count] = (unsigned int)strtoul(script, &end, 10);
            if (end == script || *end != ':') {
                break;
            }
            script = end + 1;
            if (strncmp(script, "off", 3) == 0) {
                onScreen[count] = 0;
                script += 3;
            }
            else {
                xs[count] = (float)strtod(script, &end);
                script = end;
                if (*script == ',') {
                    script++;
                }
                ys[count] = (float)strtod(script, &end);
                script = end;
                onScreen[count] = 1;
            }
            count++;
            while (*script != '\0' && *script != ';') {
                script++;
            }
            if (*script == ';') {
                script++;
            }
        }
    }
    if (count == 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (gPlatformFrameCounter >= frames[i]) {
            found = i;
        }
    }
    *active = found >= 0 && onScreen[found];
    if (*active) {
        *x = xs[found];
        *y = ys[found];
    }
    return 1;
}

void Platform_SetLogicalScissor(int enable, float x, float y, float width, float height) {
    /* Clip rectangle in logical (640x480, y down) coordinates; used as the host
       stand-in for Czan alpha-mask sprites. */
    int logicalWidth = gLogicalProjectionWidth > 0 ? gLogicalProjectionWidth : 640;
    int logicalHeight = gLogicalProjectionHeight > 0 ? gLogicalProjectionHeight : 480;
    float sx = (float)gViewportWidth / (float)logicalWidth;
    float sy = (float)gViewportHeight / (float)logicalHeight;

    if (!enable) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(gViewportX + (int)(x * sx),
              gViewportY + (int)(((float)logicalHeight - (y + height)) * sy),
              (int)(width * sx + 0.5f),
              (int)(height * sy + 0.5f));
}

int Platform_GetPointerPosition(float *x, float *y) {
    /* Wii Remote IR pointer stand-in: the mouse over the game viewport, mapped to
       the logical (640x480) framebuffer space the Czan UI uses. */
    POINT cursor;
    int logicalWidth;
    int logicalHeight;
    int active = 0;

    if (Platform_ReadScriptedPointer(x, y, &active)) {
        return active;
    }
    if (gWindow == 0 || GetForegroundWindow() != gWindow ||
        gViewportWidth <= 0 || gViewportHeight <= 0 ||
        !GetCursorPos(&cursor) || !ScreenToClient(gWindow, &cursor)) {
        return 0;
    }
    cursor.y -= gWindowHeight - (gViewportY + gViewportHeight);
    cursor.x -= gViewportX;
    if (cursor.x < 0 || cursor.y < 0 || cursor.x >= gViewportWidth || cursor.y >= gViewportHeight) {
        return 0;
    }
    logicalWidth = gLogicalProjectionWidth > 0 ? gLogicalProjectionWidth : 640;
    logicalHeight = gLogicalProjectionHeight > 0 ? gLogicalProjectionHeight : 480;
    *x = (float)cursor.x * (float)logicalWidth / (float)gViewportWidth;
    *y = (float)cursor.y * (float)logicalHeight / (float)gViewportHeight;
    return 1;
}

static unsigned int Platform_ReadMouseButtonMask(void) {
    /* Pointer buttons: left click = A, right click = B, only while the pointer is
       over the game viewport. */
    float x;
    float y;
    unsigned int mask = 0;

    if (getenv("DDRII_POINTER_SCRIPT") != 0 || !Platform_GetPointerPosition(&x, &y)) {
        return 0;
    }
    if (Platform_IsKeyHeld(VK_LBUTTON)) {
        mask |= MENU_INPUT_CONFIRM;
    }
    if (Platform_IsKeyHeld(VK_RBUTTON)) {
        mask |= MENU_INPUT_BACK;
    }
    return mask;
}

static unsigned int Platform_ReadMenuInputMask(void) {
    unsigned int mask = 0;

    mask |= Platform_ReadScriptedInputMask();
    mask |= Platform_ReadMouseButtonMask();

    if (Platform_IsKeyHeld(VK_UP) || Platform_IsKeyHeld('W')) {
        mask |= MENU_INPUT_UP;
    }
    if (Platform_IsKeyHeld(VK_DOWN) || Platform_IsKeyHeld('S')) {
        mask |= MENU_INPUT_DOWN;
    }
    if (Platform_IsKeyHeld(VK_LEFT) || Platform_IsKeyHeld('A')) {
        mask |= MENU_INPUT_LEFT;
    }
    if (Platform_IsKeyHeld(VK_RIGHT) || Platform_IsKeyHeld('D')) {
        mask |= MENU_INPUT_RIGHT;
    }
    if (Platform_IsKeyHeld(VK_RETURN) || Platform_IsKeyHeld(VK_SPACE)) {
        mask |= MENU_INPUT_CONFIRM;
    }
    if (Platform_IsKeyHeld(VK_BACK) || Platform_IsKeyHeld('B')) {
        mask |= MENU_INPUT_BACK;
    }

    Platform_LoadXInput();
    if (gXInputGetState != 0) {
        HostXInputState state;
        memset(&state, 0, sizeof(state));
        if (gXInputGetState(0, &state) == ERROR_SUCCESS) {
            WORD buttons = state.Gamepad.wButtons;
            if ((buttons & HOST_XINPUT_GAMEPAD_DPAD_UP) != 0) {
                mask |= MENU_INPUT_UP;
            }
            if ((buttons & HOST_XINPUT_GAMEPAD_DPAD_DOWN) != 0) {
                mask |= MENU_INPUT_DOWN;
            }
            if ((buttons & HOST_XINPUT_GAMEPAD_DPAD_LEFT) != 0) {
                mask |= MENU_INPUT_LEFT;
            }
            if ((buttons & HOST_XINPUT_GAMEPAD_DPAD_RIGHT) != 0) {
                mask |= MENU_INPUT_RIGHT;
            }
            if ((buttons & (HOST_XINPUT_GAMEPAD_A | HOST_XINPUT_GAMEPAD_START)) != 0) {
                mask |= MENU_INPUT_CONFIRM;
            }
            if ((buttons & (HOST_XINPUT_GAMEPAD_B | HOST_XINPUT_GAMEPAD_BACK)) != 0) {
                mask |= MENU_INPUT_BACK;
            }
        }
    }

    return mask;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            gWindowWidth = LOWORD(lParam);
            gWindowHeight = HIWORD(lParam);
            gLogicalProjectionDirty = 1;
            return 0;
        case WM_SETCURSOR:
            /* The game draws its own pointer cursor over the client area. */
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(0);
                return TRUE;
            }
            break;
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
            /* Enter/Space only: 'A' is WASD left in Platform_ReadMenuInputMask. */
            if (wParam == VK_RETURN ||
                wParam == VK_SPACE) {
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

static void RefreshWindowClientSize(void) {
    RECT clientRect;
    int width;
    int height;

    if (gWindow == 0 || !GetClientRect(gWindow, &clientRect)) {
        return;
    }

    width = clientRect.right - clientRect.left;
    height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    if (width != gWindowWidth || height != gWindowHeight) {
        gWindowWidth = width;
        gWindowHeight = height;
        gLogicalProjectionDirty = 1;
    }
}

static void ApplyLogicalProjection(int width, int height) {
    int viewportX;
    int viewportY;
    int viewportWidth;
    int viewportHeight;
    long long scaledWidth;
    long long scaledHeight;

    if (width <= 0) {
        width = 640;
    }
    if (height <= 0) {
        height = 480;
    }

    gLogicalProjectionWidth = width;
    gLogicalProjectionHeight = height;
    gLogicalProjectionDirty = 0;

    viewportX = 0;
    viewportY = 0;
    viewportWidth = gWindowWidth;
    viewportHeight = gWindowHeight;
    scaledWidth = (long long)gWindowHeight * width;
    scaledHeight = (long long)gWindowWidth * height;
    if (scaledWidth > scaledHeight) {
        viewportWidth = (int)(scaledHeight / height);
        viewportX = (gWindowWidth - viewportWidth) / 2;
    } else if (scaledWidth < scaledHeight) {
        viewportHeight = (int)(scaledWidth / width);
        viewportY = (gWindowHeight - viewportHeight) / 2;
    }
    if (viewportWidth <= 0) {
        viewportWidth = gWindowWidth;
        viewportX = 0;
    }
    if (viewportHeight <= 0) {
        viewportHeight = gWindowHeight;
        viewportY = 0;
    }

    gViewportX = viewportX;
    gViewportY = viewportY;
    gViewportWidth = viewportWidth;
    gViewportHeight = viewportHeight;

    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* The DOL draws Czan UI with a perspective camera (0x80170FA4: eye 888.9 units
       back, near 0.1, far 18000) whose z = 0 plane maps onto the 640x480 screen.
       Layer z offsets (e.g. +2..+8) only order/scale sprites slightly and are never
       clipped, so the host ortho must not clip them either. Depth test is off in 2D. */
    glOrtho(0.0, width, height, 0.0, -18000.0, 18000.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
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

    ApplyLogicalProjection(width, height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return 1;
}

void Platform_ShutdownOpenGLWindow(void) {
    int i;
    int j;

    ResetMovieAudioOutput();

    for (i = 0; i < MAX_GL_TEXTURE_SETS; i++) {
        for (j = 0; j < gTextureSets[i].count; j++) {
            if (gTextureSets[i].textures[j].id != 0) {
                glDeleteTextures(1, &gTextureSets[i].textures[j].id);
            }
        }
    }
    if (gMovieYuvTextureId != 0) {
        glDeleteTextures(1, &gMovieYuvTextureId);
        gMovieYuvTextureId = 0;
    }
    if (gFrameCaptureTexture.id != 0) {
        glDeleteTextures(1, &gFrameCaptureTexture.id);
        gFrameCaptureTexture.id = 0;
    }
    gFrameCaptureTexture.width = 0;
    gFrameCaptureTexture.height = 0;
    free(gFrameCaptureReadPixels);
    gFrameCaptureReadPixels = 0;
    gFrameCaptureReadPixelsSize = 0;
    free(gFrameCaptureScaledPixels);
    gFrameCaptureScaledPixels = 0;
    gFrameCaptureScaledPixelsSize = 0;
    free(gMovieYuvRgba);
    gMovieYuvRgba = 0;
    gMovieYuvWidth = 0;
    gMovieYuvHeight = 0;
    gMovieYuvFrameToken = -1;

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
    if (gXInputModule != 0) {
        FreeLibrary(gXInputModule);
        gXInputModule = 0;
        gXInputGetState = 0;
        gTriedXInputLoad = 0;
    }
    gPreviousMenuInputMask = 0;
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

void Platform_PollMenuInput(unsigned int *heldMask, unsigned int *triggeredMask) {
    unsigned int held;
    unsigned int triggered;

    PumpMessages();
    held = Platform_ReadMenuInputMask();
    triggered = held & ~gPreviousMenuInputMask;
    gPreviousMenuInputMask = held;

    if (heldMask != 0) {
        *heldMask = held;
    }
    if (triggeredMask != 0) {
        *triggeredMask = triggered;
    }
}

void Platform_ApplyRenderConfig(unsigned int renderConfigColor) {
    float r = (float)((renderConfigColor >> 24) & 0xFF) / 255.0f;
    float g = (float)((renderConfigColor >> 16) & 0xFF) / 255.0f;
    float b = (float)((renderConfigColor >> 8) & 0xFF) / 255.0f;
    float a = (float)(renderConfigColor & 0xFF) / 255.0f;

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Platform_SetLogicalProjection(int width, int height) {
    RefreshWindowClientSize();
    if (gLogicalProjectionDirty || width != gLogicalProjectionWidth || height != gLogicalProjectionHeight) {
        ApplyLogicalProjection(width, height);
    }
}

typedef void (APIENTRY *HostGlGenNamesProc)(int count, unsigned int *names);
typedef void (APIENTRY *HostGlBindNameProc)(unsigned int target, unsigned int name);
typedef void (APIENTRY *HostGlRenderbufferStorageProc)(unsigned int target, unsigned int format, int width, int height);
typedef void (APIENTRY *HostGlFramebufferRenderbufferProc)(unsigned int target, unsigned int attachment,
                                                           unsigned int renderbufferTarget, unsigned int renderbuffer);

static const char *gFrameDumpDirectory;
static unsigned int gFrameDumpEvery = 30;

static int FrameDump_IsEnabled(void) {
    /* Debug aid: DDRII_FRAME_DUMP=<dir> renders headless into an offscreen
       framebuffer (window presentation blocks while the display is off) and writes
       it every DDRII_FRAME_DUMP_EVERY frames (default 30) as <dir>\frame_NNNNNN.bmp. */
    static int checked;

    if (!checked) {
        const char *every = getenv("DDRII_FRAME_DUMP_EVERY");
        checked = 1;
        gFrameDumpDirectory = getenv("DDRII_FRAME_DUMP");
        if (every != 0 && atoi(every) > 0) {
            gFrameDumpEvery = (unsigned int)atoi(every);
        }
    }
    return gFrameDumpDirectory != 0 && gFrameDumpDirectory[0] != '\0';
}

static void FrameDump_BindOffscreenTarget(void) {
    static HostGlBindNameProc bindFramebuffer;
    static unsigned int framebuffer;
    static int width;
    static int height;

    if (framebuffer == 0 || width != gWindowWidth || height != gWindowHeight) {
        HostGlGenNamesProc genFramebuffers = (HostGlGenNamesProc)wglGetProcAddress("glGenFramebuffers");
        HostGlGenNamesProc genRenderbuffers = (HostGlGenNamesProc)wglGetProcAddress("glGenRenderbuffers");
        HostGlBindNameProc bindRenderbuffer = (HostGlBindNameProc)wglGetProcAddress("glBindRenderbuffer");
        HostGlRenderbufferStorageProc renderbufferStorage =
            (HostGlRenderbufferStorageProc)wglGetProcAddress("glRenderbufferStorage");
        HostGlFramebufferRenderbufferProc framebufferRenderbuffer =
            (HostGlFramebufferRenderbufferProc)wglGetProcAddress("glFramebufferRenderbuffer");
        unsigned int renderbuffers[2];

        bindFramebuffer = (HostGlBindNameProc)wglGetProcAddress("glBindFramebuffer");
        if (genFramebuffers == 0 || genRenderbuffers == 0 || bindRenderbuffer == 0 ||
            renderbufferStorage == 0 || framebufferRenderbuffer == 0 || bindFramebuffer == 0 ||
            gWindowWidth <= 0 || gWindowHeight <= 0) {
            bindFramebuffer = 0;
            return;
        }
        width = gWindowWidth;
        height = gWindowHeight;
        genFramebuffers(1, &framebuffer);
        genRenderbuffers(2, renderbuffers);
        bindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, framebuffer);
        bindRenderbuffer(0x8D41 /* GL_RENDERBUFFER */, renderbuffers[0]);
        renderbufferStorage(0x8D41, 0x8058 /* GL_RGBA8 */, width, height);
        framebufferRenderbuffer(0x8D40, 0x8CE0 /* GL_COLOR_ATTACHMENT0 */, 0x8D41, renderbuffers[0]);
        bindRenderbuffer(0x8D41, renderbuffers[1]);
        renderbufferStorage(0x8D41, 0x88F0 /* GL_DEPTH24_STENCIL8 */, width, height);
        framebufferRenderbuffer(0x8D40, 0x821A /* GL_DEPTH_STENCIL_ATTACHMENT */, 0x8D41, renderbuffers[1]);
    }
    if (bindFramebuffer != 0) {
        bindFramebuffer(0x8D40, framebuffer);
    }
}

void Platform_BeginFrame(void) {
    PumpMessages();
    RefreshWindowClientSize();
    if (FrameDump_IsEnabled()) {
        FrameDump_BindOffscreenTarget();
    }
}

static void DumpFramebufferBmp(const char *directory, unsigned int frame) {
    char path[MAX_PATH];
    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;
    unsigned char *pixels;
    int width = gWindowWidth;
    int height = gWindowHeight;
    int stride;
    FILE *file;

    if (width <= 0 || height <= 0) {
        return;
    }
    stride = (width * 3 + 3) & ~3;
    pixels = (unsigned char *)malloc((size_t)stride * (size_t)height);
    if (pixels == 0) {
        return;
    }
    if (getenv("DDRII_FRAME_DUMP_PROBE") != 0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 32, 32);
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }
    glFinish();
    glReadBuffer(0x8CE0 /* GL_COLOR_ATTACHMENT0 */);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, 0x80E0 /* GL_BGR */, GL_UNSIGNED_BYTE, pixels);

    memset(&fileHeader, 0, sizeof(fileHeader));
    memset(&infoHeader, 0, sizeof(infoHeader));
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + (DWORD)(stride * height);
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = width;
    infoHeader.biHeight = height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = BI_RGB;

    _snprintf(path, sizeof(path) - 1, "%s\\frame_%06u.bmp", directory, frame);
    path[sizeof(path) - 1] = '\0';
    file = fopen(path, "wb");
    if (file != 0) {
        fwrite(&fileHeader, sizeof(fileHeader), 1, file);
        fwrite(&infoHeader, sizeof(infoHeader), 1, file);
        fwrite(pixels, (size_t)stride, (size_t)height, file);
        fclose(file);
    }
    free(pixels);
}

void Platform_EndFrame(void) {
    gPlatformFrameCounter++;
    static unsigned int frameCounter;
    static DWORD lastFrameTime;

    if (FrameDump_IsEnabled()) {
        /* Headless: no SwapBuffers, paced to 60 Hz. */
        DWORD now;
        if (frameCounter == 0) {
            printf("frame dump: renderer '%s' glError 0x%x\n",
                   (const char *)glGetString(GL_RENDERER), (unsigned int)glGetError());
        }
        if (frameCounter % gFrameDumpEvery == 0) {
            DumpFramebufferBmp(gFrameDumpDirectory, frameCounter);
        }
        frameCounter++;
        now = timeGetTime();
        if (lastFrameTime != 0 && now - lastFrameTime < 16) {
            Sleep(16 - (now - lastFrameTime));
        }
        lastFrameTime = timeGetTime();
        return;
    }
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

    if (gBoundTextureId != gTextureSets[slot].textures[textureIndex].id) {
        glBindTexture(GL_TEXTURE_2D, gTextureSets[slot].textures[textureIndex].id);
        gBoundTextureId = gTextureSets[slot].textures[textureIndex].id;
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    return 1;
}

void Platform_DrawTexturedQuad(
    const RenderQuad *quad,
    const unsigned char *color,
    void *textureHandle,
    int textureIndex
) {
    float alpha = color[3] / 255.0f;
    int slot = (int)(long)textureHandle;
    float uMax = 1.0f;
    float vMax = 1.0f;

    if (slot >= 0 && slot < MAX_GL_TEXTURE_SETS &&
        textureIndex >= 0 && textureIndex < gTextureSets[slot].count) {
        uMax = gTextureSets[slot].textures[textureIndex].uMax;
        vMax = gTextureSets[slot].textures[textureIndex].vMax;
        if (uMax <= 0.0f) {
            uMax = 1.0f;
        }
        if (vMax <= 0.0f) {
            vMax = 1.0f;
        }
    }

    glColor4f(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, alpha);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(quad->x, quad->y, quad->z);
    glTexCoord2f(uMax, 0.0f);
    glVertex3f(quad->x + quad->width, quad->y, quad->z);
    glTexCoord2f(uMax, vMax);
    glVertex3f(quad->x + quad->width, quad->y + quad->height, quad->z);
    glTexCoord2f(0.0f, vMax);
    glVertex3f(quad->x, quad->y + quad->height, quad->z);
    glEnd();
}

void *Platform_CaptureFrameTextureRegion(int x, int y, int width, int height, int halfScale) {
    int logicalWidth;
    int logicalHeight;
    int physicalX;
    int physicalYTop;
    int physicalWidth;
    int physicalHeight;
    int srcY;
    int destWidth;
    int destHeight;

    if (gGlContext == 0 || width <= 0 || height <= 0) {
        return 0;
    }
    if (gLogicalProjectionDirty) {
        ApplyLogicalProjection(gLogicalProjectionWidth, gLogicalProjectionHeight);
    }
    logicalWidth = gLogicalProjectionWidth > 0 ? gLogicalProjectionWidth : gWindowWidth;
    logicalHeight = gLogicalProjectionHeight > 0 ? gLogicalProjectionHeight : gWindowHeight;
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > logicalWidth) {
        width = logicalWidth - x;
    }
    if (y + height > logicalHeight) {
        height = logicalHeight - y;
    }
    if (width <= 0 || height <= 0) {
        return 0;
    }
    if (gViewportWidth <= 0 || gViewportHeight <= 0) {
        gViewportX = 0;
        gViewportY = 0;
        gViewportWidth = gWindowWidth;
        gViewportHeight = gWindowHeight;
    }

    physicalX = gViewportX + (int)(((long long)x * gViewportWidth) / logicalWidth);
    physicalYTop = gViewportY + (int)(((long long)y * gViewportHeight) / logicalHeight);
    physicalWidth = (int)(((long long)width * gViewportWidth + logicalWidth - 1) / logicalWidth);
    physicalHeight = (int)(((long long)height * gViewportHeight + logicalHeight - 1) / logicalHeight);
    if (physicalX < 0) {
        physicalWidth += physicalX;
        physicalX = 0;
    }
    if (physicalYTop < 0) {
        physicalHeight += physicalYTop;
        physicalYTop = 0;
    }
    if (physicalX + physicalWidth > gWindowWidth) {
        physicalWidth = gWindowWidth - physicalX;
    }
    if (physicalYTop + physicalHeight > gWindowHeight) {
        physicalHeight = gWindowHeight - physicalYTop;
    }
    if (physicalWidth <= 0 || physicalHeight <= 0) {
        return 0;
    }

    if (gFrameCaptureTexture.id == 0) {
        glGenTextures(1, &gFrameCaptureTexture.id);
    }
    if (gFrameCaptureTexture.id == 0) {
        return 0;
    }

    srcY = gWindowHeight - physicalYTop - physicalHeight;
    if (srcY < 0) {
        srcY = 0;
    }
    destWidth = physicalWidth;
    destHeight = physicalHeight;
    if (halfScale != 0) {
        unsigned char *sourcePixels;
        unsigned char *scaledPixels;
        size_t sourceSize;
        size_t scaledSize;
        int dx;
        int dy;

        destWidth = (physicalWidth + 1) >> 1;
        destHeight = (physicalHeight + 1) >> 1;
        if (destWidth <= 0 || destHeight <= 0) {
            return 0;
        }

        sourceSize = (size_t)physicalWidth * (size_t)physicalHeight * 4u;
        scaledSize = (size_t)destWidth * (size_t)destHeight * 4u;
        sourcePixels = EnsureScratchBuffer(&gFrameCaptureReadPixels, &gFrameCaptureReadPixelsSize, sourceSize);
        scaledPixels = EnsureScratchBuffer(&gFrameCaptureScaledPixels, &gFrameCaptureScaledPixelsSize, scaledSize);
        if (sourcePixels == 0 || scaledPixels == 0) {
            return 0;
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(physicalX, srcY, physicalWidth, physicalHeight, GL_RGBA, GL_UNSIGNED_BYTE, sourcePixels);
        for (dy = 0; dy < destHeight; dy++) {
            for (dx = 0; dx < destWidth; dx++) {
                int sx0 = dx << 1;
                int sy0 = dy << 1;
                int sx1 = sx0 + 1;
                int sy1 = sy0 + 1;
                int count = 0;
                int r = 0;
                int g = 0;
                int b = 0;
                int a = 0;
                int sx;
                int sy;

                for (sy = sy0; sy <= sy1; sy++) {
                    for (sx = sx0; sx <= sx1; sx++) {
                        if (sx < physicalWidth && sy < physicalHeight) {
                            const unsigned char *src = sourcePixels +
                                ((size_t)sy * (size_t)physicalWidth + (size_t)sx) * 4u;
                            r += src[0];
                            g += src[1];
                            b += src[2];
                            a += src[3];
                            count++;
                        }
                    }
                }
                if (count != 0) {
                    unsigned char *dst = scaledPixels + ((size_t)dy * (size_t)destWidth + (size_t)dx) * 4u;
                    dst[0] = (unsigned char)(r / count);
                    dst[1] = (unsigned char)(g / count);
                    dst[2] = (unsigned char)(b / count);
                    dst[3] = (unsigned char)(a / count);
                }
            }
        }

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gFrameCaptureTexture.id);
        gBoundTextureId = gFrameCaptureTexture.id;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            destWidth,
            destHeight,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            scaledPixels);
        gFrameCaptureTexture.width = destWidth;
        gFrameCaptureTexture.height = destHeight;
        return &gFrameCaptureTexture;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gFrameCaptureTexture.id);
    gBoundTextureId = gFrameCaptureTexture.id;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, physicalX, srcY, destWidth, destHeight, 0);
    gFrameCaptureTexture.width = destWidth;
    gFrameCaptureTexture.height = destHeight;
    return &gFrameCaptureTexture;
}

void Platform_DrawCapturedTextureQuad(
    void *textureHandle,
    int x,
    int y,
    int width,
    int height,
    const unsigned int *color,
    int flipY) {
    GlCapturedTexture *captured = (GlCapturedTexture *)textureHandle;
    unsigned int c = color != 0 ? *color : 0xffffffffu;
    float topV;
    float bottomV;

    if (captured == 0 || captured->id == 0 || width == 0 || height == 0) {
        return;
    }

    topV = flipY != 0 ? 0.0f : 1.0f;
    bottomV = flipY != 0 ? 1.0f : 0.0f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, captured->id);
    gBoundTextureId = captured->id;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4ub((GLubyte)((c >> 24) & 0xff),
               (GLubyte)((c >> 16) & 0xff),
               (GLubyte)((c >> 8) & 0xff),
               (GLubyte)(c & 0xff));
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, topV);
    glVertex2i(x, y);
    glTexCoord2f(1.0f, topV);
    glVertex2i(x + width, y);
    glTexCoord2f(1.0f, bottomV);
    glVertex2i(x + width, y + height);
    glTexCoord2f(0.0f, bottomV);
    glVertex2i(x, y + height);
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

void Platform_DrawLine2D(int x0, int y0, int x1, int y1, const unsigned int *color) {
    unsigned int c = color != 0 ? *color : 0;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glColor4ub((GLubyte)((c >> 24) & 0xFF),
               (GLubyte)((c >> 16) & 0xFF),
               (GLubyte)((c >> 8) & 0xFF),
               (GLubyte)(c & 0xFF));
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2i(x0, y0);
    glVertex2i(x1, y1);
    glEnd();
    glLineWidth(1.0f);
    glEnable(GL_TEXTURE_2D);
}

void Platform_DrawTriangle2D(
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    const unsigned int *color) {
    unsigned int c = color != 0 ? *color : 0;

    glDisable(GL_TEXTURE_2D);
    glColor4ub((GLubyte)((c >> 24) & 0xFF),
               (GLubyte)((c >> 16) & 0xFF),
               (GLubyte)((c >> 8) & 0xFF),
               (GLubyte)(c & 0xFF));
    glBegin(GL_TRIANGLES);
    glVertex2i(x0, y0);
    glVertex2i(x1, y1);
    glVertex2i(x2, y2);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void Platform_DrawTexturedTriangle2D(
    int x0,
    int y0,
    float u0,
    float v0,
    int x1,
    int y1,
    float u1,
    float v1,
    int x2,
    int y2,
    float u2,
    float v2,
    void *textureHandle,
    int textureIndex,
    const unsigned int *color) {
    unsigned int c = color != 0 ? *color : 0xFFFFFFFFu;
    int slot = (int)(long)textureHandle;
    float uMax = 1.0f;
    float vMax = 1.0f;

    if (!Platform_BindTextureFromTextureSet(textureHandle, 0, textureIndex)) {
        Platform_DrawTriangle2D(x0, y0, x1, y1, x2, y2, color);
        return;
    }
    if (slot >= 0 && slot < MAX_GL_TEXTURE_SETS &&
        textureIndex >= 0 && textureIndex < gTextureSets[slot].count) {
        uMax = gTextureSets[slot].textures[textureIndex].uMax;
        vMax = gTextureSets[slot].textures[textureIndex].vMax;
        if (uMax <= 0.0f) {
            uMax = 1.0f;
        }
        if (vMax <= 0.0f) {
            vMax = 1.0f;
        }
    }

    glEnable(GL_TEXTURE_2D);
    glColor4ub((GLubyte)((c >> 24) & 0xFF),
               (GLubyte)((c >> 16) & 0xFF),
               (GLubyte)((c >> 8) & 0xFF),
               (GLubyte)(c & 0xFF));
    glBegin(GL_TRIANGLES);
    glTexCoord2f(u0 * uMax, v0 * vMax);
    glVertex2i(x0, y0);
    glTexCoord2f(u1 * uMax, v1 * vMax);
    glVertex2i(x1, y1);
    glTexCoord2f(u2 * uMax, v2 * vMax);
    glVertex2i(x2, y2);
    glEnd();
}

void Platform_DrawTexturedTriangleStrip2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex) {
    unsigned int i;
    int slot = (int)(long)textureHandle;
    float uMax = 1.0f;
    float vMax = 1.0f;

    if (points == 0 || vertexCount < 3) {
        return;
    }

    if (Platform_BindTextureFromTextureSet(textureHandle, 0, textureIndex)) {
        if (slot >= 0 && slot < MAX_GL_TEXTURE_SETS &&
            textureIndex >= 0 && textureIndex < gTextureSets[slot].count) {
            uMax = gTextureSets[slot].textures[textureIndex].uMax;
            vMax = gTextureSets[slot].textures[textureIndex].vMax;
            if (uMax <= 0.0f) {
                uMax = 1.0f;
            }
            if (vMax <= 0.0f) {
                vMax = 1.0f;
            }
        }
        glEnable(GL_TEXTURE_2D);
    }
    else {
        glDisable(GL_TEXTURE_2D);
    }

    glBegin(GL_TRIANGLE_STRIP);
    for (i = 0; i < vertexCount; i++) {
        unsigned int c = colors != 0 ? colors[i] : 0xFFFFFFFFu;
        glColor4ub((GLubyte)((c >> 24) & 0xFF),
                   (GLubyte)((c >> 16) & 0xFF),
                   (GLubyte)((c >> 8) & 0xFF),
                   (GLubyte)(c & 0xFF));
        if (texcoords != 0) {
            glTexCoord2f(texcoords[i][0] * uMax, texcoords[i][1] * vMax);
        }
        glVertex2i(points[i][0], points[i][1]);
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void Platform_DrawTexturedTriangleList2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex) {
    unsigned int i;
    int slot = (int)(long)textureHandle;
    float uMax = 1.0f;
    float vMax = 1.0f;

    if (points == 0 || vertexCount < 3) {
        return;
    }

    if (Platform_BindTextureFromTextureSet(textureHandle, 0, textureIndex)) {
        if (slot >= 0 && slot < MAX_GL_TEXTURE_SETS &&
            textureIndex >= 0 && textureIndex < gTextureSets[slot].count) {
            uMax = gTextureSets[slot].textures[textureIndex].uMax;
            vMax = gTextureSets[slot].textures[textureIndex].vMax;
            if (uMax <= 0.0f) {
                uMax = 1.0f;
            }
            if (vMax <= 0.0f) {
                vMax = 1.0f;
            }
        }
        glEnable(GL_TEXTURE_2D);
    }
    else {
        glDisable(GL_TEXTURE_2D);
    }

    glBegin(GL_TRIANGLES);
    for (i = 0; i < vertexCount; i++) {
        unsigned int c = colors != 0 ? colors[i] : 0xFFFFFFFFu;
        glColor4ub((GLubyte)((c >> 24) & 0xFF),
                   (GLubyte)((c >> 16) & 0xFF),
                   (GLubyte)((c >> 8) & 0xFF),
                   (GLubyte)(c & 0xFF));
        if (texcoords != 0) {
            glTexCoord2f(texcoords[i][0] * uMax, texcoords[i][1] * vMax);
        }
        glVertex2i(points[i][0], points[i][1]);
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void Platform_DrawMovieYuvFrame(
    const unsigned char *planeY,
    const unsigned char *planeU,
    const unsigned char *planeV,
    int width,
    int height,
    int frameToken,
    int x,
    int y,
    int drawWidth,
    int drawHeight) {
    int uvWidth;
    int pixelCount;
    int i;
    int needsUpload;

    if (planeY == 0 || planeU == 0 || planeV == 0 || width <= 0 || height <= 0) {
        return;
    }
    pixelCount = width * height;
    if (gMovieYuvRgba == 0 || gMovieYuvWidth != width || gMovieYuvHeight != height) {
        unsigned char *newPixels;

        newPixels = (unsigned char *)realloc(gMovieYuvRgba, (size_t)pixelCount * 4);
        if (newPixels == 0) {
            return;
        }
        gMovieYuvRgba = newPixels;
        gMovieYuvWidth = width;
        gMovieYuvHeight = height;
        gMovieYuvFrameToken = -1;
        if (gMovieYuvTextureId == 0) {
            glGenTextures(1, &gMovieYuvTextureId);
        }
    }

    if (drawWidth <= 0) {
        drawWidth = gWindowWidth > 0 ? gWindowWidth : width;
    }
    if (drawHeight <= 0) {
        drawHeight = gWindowHeight > 0 ? gWindowHeight : height;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gMovieYuvTextureId);
    gBoundTextureId = gMovieYuvTextureId;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    needsUpload = (gMovieYuvFrameToken != frameToken);
    if (needsUpload) {
        uvWidth = (width + 1) >> 1;
        for (i = 0; i < pixelCount; i++) {
            int px = i % width;
            int py = i / width;
            int uvIndex = (py >> 1) * uvWidth + (px >> 1);
            int yValue = planeY[i];
            int uValue = planeU[uvIndex] - 128;
            int vValue = planeV[uvIndex] - 128;
            int r = yValue + (int)(1.402f * (float)vValue);
            int g = yValue - (int)(0.344136f * (float)uValue + 0.714136f * (float)vValue);
            int b = yValue + (int)(1.772f * (float)uValue);
            unsigned char *pixel = gMovieYuvRgba + i * 4;

            pixel[0] = ClampByte(r);
            pixel[1] = ClampByte(g);
            pixel[2] = ClampByte(b);
            pixel[3] = 255;
        }
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            gMovieYuvRgba);
        gMovieYuvFrameToken = frameToken;
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_QUADS);
    if (height > width) {
        glTexCoord2f(1.0f, 0.0f);
        glVertex2i(x, y);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2i(x + drawWidth, y);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2i(x + drawWidth, y + drawHeight);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2i(x, y + drawHeight);
    }
    else {
        glTexCoord2f(0.0f, 0.0f);
        glVertex2i(x, y);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2i(x + drawWidth, y);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2i(x + drawWidth, y + drawHeight);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2i(x, y + drawHeight);
    }
    glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void Platform_PlayMoviePcm16(const short *samples, int sampleCount, int channelCount, int sampleRate) {
    WAVEFORMATEX format;
    int totalSamples;
    int byteCount;
    int slot;
    int i;

    if (samples == 0 || sampleCount <= 0 || channelCount <= 0 || sampleRate <= 0) {
        return;
    }
    if (channelCount > 2) {
        channelCount = 2;
    }

    CleanupCompletedMovieAudioBuffers();

    if (gMovieWaveOut == 0 ||
        gMovieWaveChannels != channelCount ||
        gMovieWaveRate != sampleRate) {
        ResetMovieAudioOutput();
        memset(&format, 0, sizeof(format));
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = (WORD)channelCount;
        format.nSamplesPerSec = (DWORD)sampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = (WORD)(format.nChannels * (format.wBitsPerSample / 8));
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        if (waveOutOpen(&gMovieWaveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            gMovieWaveOut = 0;
            return;
        }
        gMovieWaveChannels = channelCount;
        gMovieWaveRate = sampleRate;
    }

    slot = -1;
    for (i = 0; i < MAX_MOVIE_AUDIO_BUFFERS; i++) {
        if (gMovieWaveBuffers[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    totalSamples = sampleCount * channelCount;
    byteCount = totalSamples * (int)sizeof(short);
    gMovieWaveBuffers[slot] = (short *)malloc((size_t)byteCount);
    if (gMovieWaveBuffers[slot] == 0) {
        return;
    }
    memcpy(gMovieWaveBuffers[slot], samples, (size_t)byteCount);

    memset(&gMovieWaveHeaders[slot], 0, sizeof(gMovieWaveHeaders[slot]));
    gMovieWaveHeaders[slot].lpData = (LPSTR)gMovieWaveBuffers[slot];
    gMovieWaveHeaders[slot].dwBufferLength = (DWORD)byteCount;
    if (waveOutPrepareHeader(gMovieWaveOut, &gMovieWaveHeaders[slot], sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
        waveOutWrite(gMovieWaveOut, &gMovieWaveHeaders[slot], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        if ((gMovieWaveHeaders[slot].dwFlags & WHDR_PREPARED) != 0) {
            waveOutUnprepareHeader(gMovieWaveOut, &gMovieWaveHeaders[slot], sizeof(WAVEHDR));
        }
        free(gMovieWaveBuffers[slot]);
        gMovieWaveBuffers[slot] = 0;
        memset(&gMovieWaveHeaders[slot], 0, sizeof(gMovieWaveHeaders[slot]));
    }
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
    gBoundTextureId = 0;

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
        if (format != 0 && format != 1 && format != 2 && format != 3 &&
            format != 4 && format != 5 && format != 6 && format != 14) {
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        free(pixels);

        set->textures[set->count].id = id;
        set->textures[set->count].width = width;
        set->textures[set->count].height = height;
        set->textures[set->count].uMax = 1.0f;
        set->textures[set->count].vMax = 1.0f;
        set->count++;
        printf("OpenGL backend: loaded TPL texture %u -> %dx%d\n", i, width, height);
    }

    return textureSlot;
}

int Platform_TextureSlotInitFromTpl(TextureSlotKnownFields *textureSlot) {
    (void)textureSlot;
    return 1;
}
