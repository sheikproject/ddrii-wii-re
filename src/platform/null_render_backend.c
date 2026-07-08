#include "platform/render_backend.h"

#include <stdio.h>

void Platform_ApplyRenderConfig(unsigned int renderConfigColor) {
    printf("render backend: ApplyRenderConfig color=0x%08X\n", renderConfigColor);
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
