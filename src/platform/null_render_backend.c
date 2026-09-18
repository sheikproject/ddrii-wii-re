#include "platform/render_backend.h"

#include <stdio.h>

void Platform_ApplyRenderConfig(unsigned int renderConfigColor) {
    printf("render backend: ApplyRenderConfig color=0x%08X\n", renderConfigColor);
}

void Platform_SetLogicalProjection(int width, int height) {
    printf("render backend: SetLogicalProjection %dx%d\n", width, height);
}

void Platform_BeginFrame(void) {
    puts("render backend: BeginFrame");
}

void Platform_EndFrame(void) {
    puts("render backend: EndFrame");
}

int Platform_GetTextureDimensions(void *textureHandle, int textureIndex, int *width, int *height) {
    (void)textureHandle;

    if (width != 0) {
        *width = 640;
    }
    if (height != 0) {
        *height = 480;
    }
    printf("render backend: GetTextureDimensions textureIndex=%d -> 640x480\n", textureIndex);
    return 1;
}

int Platform_BindTextureFromTextureSet(void *textureHandle, void *outTextureObject, int textureIndex) {
    (void)textureHandle;
    (void)outTextureObject;
    printf("render backend: BindTexture textureIndex=%d\n", textureIndex);
    return 1;
}

void Platform_DrawTexturedQuad(
    const RenderQuad *quad,
    const unsigned char *color,
    void *textureHandle,
    int textureIndex
) {
    (void)textureHandle;
    printf("render backend: DrawTexturedQuad x=%.2f y=%.2f w=%.2f h=%.2f texture=%d color=%02X%02X%02X%02X\n",
           quad->x,
           quad->y,
           quad->width,
           quad->height,
           textureIndex,
           color[0],
           color[1],
           color[2],
           color[3]);
}

void *Platform_CaptureFrameTextureRegion(int x, int y, int width, int height, int halfScale) {
    (void)halfScale;
    printf("render backend: CaptureFrameTextureRegion x=%d y=%d w=%d h=%d\n",
           x,
           y,
           width,
           height);
    return 0;
}

void Platform_DrawCapturedTextureQuad(
    void *textureHandle,
    int x,
    int y,
    int width,
    int height,
    const unsigned int *color,
    int flipY) {
    (void)textureHandle;
    printf("render backend: DrawCapturedTextureQuad x=%d y=%d w=%d h=%d color=0x%08X flipY=%d\n",
           x,
           y,
           width,
           height,
           color != 0 ? *color : 0,
           flipY);
}

void Platform_DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags) {
    (void)z;
    printf("render backend: DrawFilledRect x=%d y=%d w=%d h=%d color=0x%08X flags=%d\n",
           x,
           y,
           width,
           height,
           color != 0 ? *color : 0,
           flags);
}

void Platform_DrawLine2D(int x0, int y0, int x1, int y1, const unsigned int *color) {
    printf("render backend: DrawLine2D x0=%d y0=%d x1=%d y1=%d color=0x%08X\n",
           x0,
           y0,
           x1,
           y1,
           color != 0 ? *color : 0);
}

void Platform_DrawTriangle2D(
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    const unsigned int *color) {
    printf("render backend: DrawTriangle2D x0=%d y0=%d x1=%d y1=%d x2=%d y2=%d color=0x%08X\n",
           x0,
           y0,
           x1,
           y1,
           x2,
           y2,
           color != 0 ? *color : 0);
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
    (void)textureHandle;
    printf("render backend: DrawTexturedTriangle2D x0=%d y0=%d uv0=%.3f,%.3f x1=%d y1=%d uv1=%.3f,%.3f x2=%d y2=%d uv2=%.3f,%.3f texture=%d color=0x%08X\n",
           x0,
           y0,
           u0,
           v0,
           x1,
           y1,
           u1,
           v1,
           x2,
           y2,
           u2,
           v2,
           textureIndex,
           color != 0 ? *color : 0);
}

void Platform_DrawTexturedTriangleStrip2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex) {
    (void)points;
    (void)texcoords;
    (void)colors;
    (void)textureHandle;
    printf("render backend: DrawTexturedTriangleStrip2D vertices=%u texture=%d\n",
           vertexCount,
           textureIndex);
}

void Platform_DrawTexturedTriangleList2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex) {
    (void)points;
    (void)texcoords;
    (void)colors;
    (void)textureHandle;
    printf("render backend: DrawTexturedTriangleList2D vertices=%u texture=%d\n",
           vertexCount,
           textureIndex);
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
    (void)planeY;
    (void)planeU;
    (void)planeV;
    printf("render backend: DrawMovieYuvFrame frame=%d src=%dx%d dst=%d,%d %dx%d\n",
           frameToken,
           width,
           height,
           x,
           y,
           drawWidth,
           drawHeight);
}

void Platform_PlayMoviePcm16(const short *samples, int sampleCount, int channelCount, int sampleRate) {
    (void)samples;
    printf("render backend: PlayMoviePcm16 samples=%d channels=%d rate=%d\n",
           sampleCount,
           channelCount,
           sampleRate);
}

unsigned int Platform_CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
) {
    (void)textureManager;
    (void)resourceData;
    (void)resourceSizeOrTplBase;
    printf("render backend: CreateTextureFromTplResource slot=%u\n", textureSlot);
    return textureSlot;
}

int Platform_TextureSlotInitFromTpl(TextureSlotKnownFields *textureSlot) {
    (void)textureSlot;
    puts("render backend: TextureSlot_InitFromTpl");
    return 1;
}

int Platform_InitOpenGLWindow(const char *title, int width, int height) {
    (void)title;
    (void)width;
    (void)height;
    return 1;
}

void Platform_ShutdownOpenGLWindow(void) {
}

int Platform_ShouldQuit(void) {
    return 0;
}

int Platform_ConsumeConfirmPressed(void) {
    return 0;
}

void Platform_PollMenuInput(unsigned int *heldMask, unsigned int *triggeredMask) {
    if (heldMask != 0) {
        *heldMask = 0;
    }
    if (triggeredMask != 0) {
        *triggeredMask = 0;
    }
}
