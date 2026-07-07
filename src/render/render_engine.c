#include "render/render_engine.h"

#include "platform/render_backend.h"

#define TEXTURE_SLOT_AUTO 0xFFFFFFFFu

void ApplyRenderConfig(int screenManager, const unsigned int *renderConfigColor) {
    (void)screenManager;

    /* Original copies a 4-byte render config/color to screenManager +0x48,
       then calls the GX render-state wrapper at 0x801D4690. */
    Platform_ApplyRenderConfig(renderConfigColor != 0 ? *renderConfigColor : 0);
}

int GetTextureDimensions(void *textureHandle, int textureIndex, int *width, int *height) {
    return Platform_GetTextureDimensions(textureHandle, textureIndex, width, height);
}

void DrawTexturedQuad(
    RenderQuad *position,
    int width,
    int height,
    unsigned char *color,
    void *textureHandle,
    int textureIndex
) {
    RenderQuad quad = *position;

    /* Original sets GX state, binds textureIndex from textureHandle, then emits a
       four-vertex quad with full UVs: (0,0), (1,0), (1,1), (0,1). */
    quad.width = (float)width;
    quad.height = (float)height;
    BindTextureFromTextureSet(textureHandle, 0, textureIndex);
    Platform_DrawTexturedQuad(&quad, color, textureHandle, textureIndex);
}

unsigned int CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
) {
    TextureSlotKnownFields *slot;

    if (textureSlot == TEXTURE_SLOT_AUTO) {
        textureSlot = textureManager->nextTextureSlot;
    }

    if (textureSlot >= textureManager->slotCount) {
        return textureSlot;
    }

    slot = &textureManager->slots[textureSlot];
    slot->resourceData = resourceData;
    slot->resourceSizeOrTplBase = resourceSizeOrTplBase;
    TextureSlot_InitFromTpl(slot);
    Platform_CreateTextureFromTplResource(textureManager, resourceData, resourceSizeOrTplBase, textureSlot);
    textureManager->nextTextureSlot++;
    return textureSlot;
}

int TextureSlot_InitFromTpl(TextureSlotKnownFields *textureSlot) {
    /* Original relocates the TPL texture descriptor table and image/palette offsets,
       then allocates one 0x20-byte GX texture object per TPL texture. */
    return Platform_TextureSlotInitFromTpl(textureSlot);
}

int BindTextureFromTextureSet(void *textureHandle, void *outTextureObject, int textureIndex) {
    /* Original looks up the TPL texture info for textureIndex, initializes a GX texture
       object, configures LOD, and returns 1 if the texture was usable. */
    return Platform_BindTextureFromTextureSet(textureHandle, outTextureObject, textureIndex);
}

void DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags) {
    /* Original is used by BootLogoModule_Tick for the fade overlay rectangle. */
    Platform_DrawFilledRect(x, y, z, width, height, color, flags);
}
