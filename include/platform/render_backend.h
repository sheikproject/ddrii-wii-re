#ifndef DDRII_PLATFORM_RENDER_BACKEND_H
#define DDRII_PLATFORM_RENDER_BACKEND_H

#include "render/render_engine.h"

void Platform_ApplyRenderConfig(unsigned int renderConfigColor);
void Platform_SetLogicalProjection(int width, int height);
void Platform_BeginFrame(void);
void Platform_EndFrame(void);
int Platform_GetTextureDimensions(void *textureHandle, int textureIndex, int *width, int *height);
int Platform_BindTextureFromTextureSet(void *textureHandle, void *outTextureObject, int textureIndex);
void Platform_DrawTexturedQuad(
    const RenderQuad *quad,
    const unsigned char *color,
    void *textureHandle,
    int textureIndex
);
void *Platform_CaptureFrameTextureRegion(int x, int y, int width, int height, int halfScale);
void Platform_DrawCapturedTextureQuad(
    void *textureHandle,
    int x,
    int y,
    int width,
    int height,
    const unsigned int *color,
    int flipY);
void Platform_DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags);
void Platform_DrawLine2D(int x0, int y0, int x1, int y1, const unsigned int *color);
void Platform_DrawTriangle2D(
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    const unsigned int *color);
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
    const unsigned int *color);
void Platform_DrawTexturedTriangleStrip2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex);
void Platform_DrawTexturedTriangleList2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex);
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
    int drawHeight);
void Platform_PlayMoviePcm16(const short *samples, int sampleCount, int channelCount, int sampleRate);
unsigned int Platform_CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
);
int Platform_TextureSlotInitFromTpl(TextureSlotKnownFields *textureSlot);
int Platform_InitOpenGLWindow(const char *title, int width, int height);
void Platform_ShutdownOpenGLWindow(void);
int Platform_ShouldQuit(void);
int Platform_ConsumeConfirmPressed(void);
void Platform_PollMenuInput(unsigned int *heldMask, unsigned int *triggeredMask);

#endif
