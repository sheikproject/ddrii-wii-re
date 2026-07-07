#ifndef DDRII_RENDER_RENDER_ENGINE_H
#define DDRII_RENDER_RENDER_ENGINE_H

typedef struct RenderQuad {
    float x;
    float y;
    float z;
    float width;
    float height;
} RenderQuad;

typedef struct TextureSlotKnownFields {
    void *resourceData;
    int resourceSizeOrTplBase;
    void *tplHeader;
    void *gxTextureObjects;
} TextureSlotKnownFields;

typedef struct TextureManagerKnownFields {
    TextureSlotKnownFields *slots;
    unsigned int nextTextureSlot;
    unsigned int slotCount;
} TextureManagerKnownFields;

void ApplyRenderConfig(int screenManager, const unsigned int *renderConfigColor);
int GetTextureDimensions(void *textureHandle, int textureIndex, int *width, int *height);
void DrawTexturedQuad(
    RenderQuad *position,
    int width,
    int height,
    unsigned char *color,
    void *textureHandle,
    int textureIndex
);
unsigned int CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
);
int TextureSlot_InitFromTpl(TextureSlotKnownFields *textureSlot);
int BindTextureFromTextureSet(void *textureHandle, void *outTextureObject, int textureIndex);
void DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags);

#endif
