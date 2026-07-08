#ifndef DDRII_PLATFORM_RENDER_BACKEND_H
#define DDRII_PLATFORM_RENDER_BACKEND_H

#include "render/render_engine.h"

void Platform_ApplyRenderConfig(unsigned int renderConfigColor);
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
void Platform_DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags);
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

#endif
