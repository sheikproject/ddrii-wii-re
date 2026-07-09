#include "render/render_engine.h"

#include "platform/render_backend.h"

#define TEXTURE_SLOT_AUTO 0xFFFFFFFFu

void RenderBeginFrame(void) {
    Platform_BeginFrame();
}

void RenderEndFrame(void) {
    Platform_EndFrame();
}

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

void UiRootManager_LoadResource(int *uiRootManager, void *linkData) {
    /* 0x800FE548 links a WII UI-root resource and initializes the UI root manager.
       It creates Czan object groups from blocks 0..3 using the global Czan UI manager
       at *(DAT_802E71B8 + 0x270), initializes several sub-managers from blocks 4..8,
       creates five standalone TPL textures from blocks 9..13, and sets uiRootManager[0] = 1. */
    (void)uiRootManager;
    (void)linkData;
}

int UiRootManager_CreateReferenceObjectGroup(
    int *uiRootManager,
    int referenceObjectGroupHandle,
    int referenceChildIndex,
    unsigned char linkMode
) {
    /* 0x800FEC3C creates/clones an object group from uiRootManager[1]. If that
       base group is -1, it returns -1. Otherwise it creates a new group with
       CzanUiManager_CloneObjectGroup(global Czan UI manager, uiRootManager[1], 2, 0).

       When referenceChildIndex is not -1, the new group is linked to
       referenceObjectGroupHandle/referenceChildIndex with linkMode. It then copies
       reference child placement/color information onto the new group and enables the
       referenced child object. */
    (void)uiRootManager;
    (void)referenceObjectGroupHandle;
    (void)referenceChildIndex;
    (void)linkMode;
    return -1;
}

void UiRootSubManager_LoadCzanGroups(int *subManager, void *linkData) {
    /* 0x80100944 loads the sub-manager allocated by UiRootManager_LoadResource
       for WII block 4. It links a nested WII resource, creates Czan object groups
       from blocks 0..3 with flags=2, clones/derives several handles from group 3,
       links two temporary/root groups against reference children, allocates helper
       objects around those groups, and finishes with a sub-manager setup call.

       Exact UI role still needs confirmation, but the important porting point is
       that this function creates more global Czan UI objects through
       CzanUiManager_CreateObjectGroup(*(DAT_802E71B8 + 0x270), ...). */
    (void)subManager;
    (void)linkData;
}

void UiRootSubManager_LoadCzanGroupsWithTexture(int *subManager, void *linkData) {
    /* 0x80104538 loads the sub-manager allocated by UiRootManager_LoadResource
       for WII block 5. It links a nested WII resource, creates Czan object groups
       from blocks 0..3 with flags=0, creates/gets one extra root/reference group
       through UiRootManager_CreateReferenceObjectGroup(gUiRootManager, -1, -1, 0x1F),
       disables/hides all five groups through FUN_801750A8(..., 0), then creates one
       standalone TPL texture from block 4 and stores the texture slot at subManager[0x0B].

       This is important for the port because block 5 is another construction path for
       Czan UI groups and one associated texture before UiRootManager_DrawFrame traverses
       the global Czan object list. */
    (void)subManager;
    (void)linkData;
}

void UiRootSubManager_InitTextureFrameGroups(int *subManager) {
    /* 0x80106054 initializes the 0x7C8-byte sub-manager allocated by
       UiRootManager_LoadResource for WII block 6. It creates two rows of 0x28
       root/object groups through FUN_801002E4(gUiRootManager), with row handles
       stored at subManager + 0x08 and subManager + 0x3E8. The second row is hidden
       immediately with CzanUiManager_SetObjectGroupDrawEnabled(..., 0).

       After allocation, both rows receive texture frames 0..0x27 through
       CzanUiManager_SetObjectTextureFrame(global Czan UI manager, group, 0, frame, 0). */
    (void)subManager;
}

void UiRootSubManager_LoadLinkedObjectGroup(int *subManager, void *linkData) {
    /* 0x80106C34 loads the 0x14-byte sub-manager allocated by UiRootManager_LoadResource
       for WII block 6 and stored at uiRootManager[0x0F]. It creates one Czan object
       group from linkData with flags=2, stores that handle at subManager[0], then
       creates/retrieves a linked reference group through
       UiRootManager_CreateReferenceObjectGroup(gUiRootManager, group, 0, 0x1F)
       and stores it at subManager[1]. */
    (void)subManager;
    (void)linkData;
}

void UiRootSubManager_LoadIndexedHiddenGroups(int *subManager, void *linkData, int setupValue) {
    /* 0x8011E0A4 links a WII resource, creates six Czan object groups from blocks
       0..5 with flags=2, stores the group handles at subManager[0..5], and hides
       every group immediately with CzanUiManager_SetObjectGroupDrawEnabled(..., 0).

       For each group, it asks the Czan UI manager for the group's child count and
       each child object. It reads each child object's ID at object +0x160, then
       writes a byte lookup table:

       subManager + groupIndex * 0x80 + 0x17 + objectId = childIndex

       Header/control fields are also initialized:
       subManager[0xCC6] = -1, [0xCC7] = setupValue, [0xCC8] = 0, [0xCC9] = 1.
       The per-group follow-up call is
       UiRootSubManager_ConfigureIndexedHiddenGroup(subManager, groupIndex, setupValue). */
    (void)subManager;
    (void)linkData;
    (void)setupValue;
}

void UiRootSubManager_ConfigureIndexedHiddenGroup(int *subManager, int groupIndex, int setupValue) {
    /* 0x8011E3E8 configures one group created by UiRootSubManager_LoadIndexedHiddenGroups.
       The original recovers subManager/groupIndex from the engine context helper, but
       the caller passes the same logical values as this exported helper.

       Confirmed work:
       - exits unless subManager[0xCC9] is nonzero
       - writes setupValue to subManager[0xCC7]
       - reads a five-word config row from DAT_80291AB0 + groupIndex * 0x14
       - for every configured child, queries the Czan UI object/sprite, computes UV or
         normalized quad values, writes per-child data at subManager + groupIndex * 0x800 + 0x318,
         and applies it through FUN_80175998
       - in widescreen mode, applies extra per-child transform/offset data through
         CzanUiManager_ApplyChildObjectOffset or FUN_801752AC depending the config row. */
    (void)subManager;
    (void)groupIndex;
    (void)setupValue;
}

void UiRootManager_DrawFrame(int *uiRootManager) {
    /* 0x800FEB58 is the UI root draw/update pass. If uiRootManager[0] is nonzero,
       it brackets a global Czan UI draw with several manager update calls.

       The important confirmed draw call is:
       CzanUiManager_DrawObjectListReverse(*(DAT_802E71B8 + 0x270), -1)

       uiRootManager[0x0B] is a reentrancy/draw guard: the Czan global object list is
       drawn only when it is zero, then the guard is reset at the end of the pass. */
    (void)uiRootManager;
}
