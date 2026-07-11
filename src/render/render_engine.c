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

void RenderFlushPendingState(void) {
    /* 0x801D3B70 flushes the render context dirty flags at context +0x5FC before a
       primitive batch is emitted.

       Confirmed dirty bits:
       - 0x00000001 -> RenderFlushTexGenState / FUN_801D5D80
       - 0x00000002 -> RenderFlushNoOpState / FUN_801D6690
       - 0x00000004 -> write context +0x254 through GX command 0x61
       - 0x00000008 -> RenderFlushVertexDescriptorState / FUN_801D29A0
       - 0x00000010 -> RenderFlushVertexAttributeFormatState / FUN_801D2F30
       - 0x00000018 -> RenderRecomputeVertexStride / FUN_801D2A50
       - 0x00000F00 -> writes BP/registers 0x100A..0x100D from context +0xA8..+0xB4
       - 0x0100F000 -> writes 0x1009 and 0x100E.. from context +0x254/+0xB8..
       - 0x02FF0000 -> writes 0x103F/0x1040.. and paired +0x1050.. values
       - 0x04000000 -> RenderFlushMatrixIndexState(0/5) / FUN_801D7D10
       - 0x08000000 -> RenderFlushProjectionState / FUN_801D77B0
       - 0x10000000 -> RenderFlushViewportState / FUN_801D7A90

       It clears context +0x5FC to zero after flushing. */
}

void RenderFlushTexGenState(void) {
    /* 0x801D5D80 is reached from RenderFlushPendingState dirty bit 0x01.

       It validates texture-coordinate generator dependencies before the next draw:
       - reads context +0x254 for the active texgen count/configuration
       - checks source pairs packed in context +0x170 for the first four generators
       - checks additional per-generator state around context +0x150/+0x5A4
       - calls FUN_801D5CF0 when a required texgen/source bit is missing from the
         active mask at context +0x5E4/+0x5E8

       This is part of the model material path, but it belongs to the generic GX
       state cache rather than the Czan model parser itself. */
}

void RenderCopyTexGenState(int sourceSlot, int destinationSlot) {
    /* 0x801D5CF0 copies cached texture-generator BP/XF state from sourceSlot to
       destinationSlot and writes the destination pair immediately.

       Confirmed fields:
       - source context +0x564 supplies two 10-bit values
       - destination context +0x108 receives the low value
       - destination context +0x128 receives the high value
       - source context +0x584 controls the extra enable/type bit at bit 16 of both
         destination words through countLeadingZeros((value & 3) - 1)
       - both destination words are written through FIFO command 0x61

       RenderFlushTexGenState calls this when its active masks show that a texgen
       destination still needs state copied from a source slot. */
    (void)sourceSlot;
    (void)destinationSlot;
}

void RenderFlushNoOpState(void) {
    /* 0x801D6690 is currently an empty flush slot reached from dirty bit 0x02.
       Keeping the export matters because RenderFlushPendingState still dispatches
       it in the original engine. */
}

void RenderFlushVertexDescriptorState(void) {
    /* 0x801D29A0 flushes the vertex descriptor words:
       - command 0x08/register 0x50 receives context +0x14
       - command 0x08/register 0x60 receives context +0x18
       - command 0x10/register 0x1008 receives a derived summary of enabled normal
         and color/texture descriptor state, including the special attribute state
         tracked at context +0x524/+0x525
       - context +2 is marked active/clean afterward. */
}

void RenderFlushVertexAttributeFormatState(void) {
    /* 0x801D2F30 flushes per-vertex-format attribute words selected by the dirty
       byte at context +0x5FB. For each dirty format index it writes:
       - register 0x70 | format from context +0x1C + format * 4
       - register 0x80 | format from context +0x3C + format * 4
       - register 0x90 | format from context +0x5C + format * 4
       then clears context +0x5FB. */
}

void RenderRecomputeVertexStride(void) {
    /* 0x801D2A50 recomputes the current vertex stride/count at context +6 when
       descriptor or format state changed. It sums descriptor bits from context
       +0x14/+0x18 using the small lookup tables at DAT_802E6CD8/6CDC/6CE0 and
       applies a normal/vector multiplier from the attr-10 format state. */
}

void RenderFlushProjectionState(void) {
    /* 0x801D77B0 flushes seven projection-related words from context
       +0x528..+0x540 through FIFO command 0x10, beginning at register/index 0x61020.

       The exact matrix layout still needs confirmation, but this is the dirty-bit
       0x08000000 path and is separate from the viewport flush at 0x801D7A90. */
}

void RenderFlushViewportState(void) {
    /* 0x801D7A90 flushes the viewport transform through FIFO command 0x10/register
       0x5101A. It derives scale/offset values from context +0x544..+0x560:
       - +0x54C and +0x550 are scaled by FLOAT_802EA8A0
       - +0x554/+0x558/+0x55C participate in the depth transform
       - +0x544/+0x548 contribute to viewport origin offsets

       This is the dirty-bit 0x10000000 path. */
}

void RenderFlushMatrixIndexState(int selector) {
    /* 0x801D7D10 flushes one of two cached matrix/index words. selector < 5 writes
       context +0x80 to command/register pair 0x08/0x30 and 0x10/0x1018. selector >= 5
       writes context +0x84 to 0x08/0x40 and 0x10/0x1019. The pending-state flush calls
       it for selectors 0 and 5 when dirty bit 0x04000000 is set. */
    (void)selector;
}

void RenderClearVertexDescriptors(void) {
    /* 0x801D2B80 resets the GX vertex descriptor state in the render context:
       context +0x14 = 0x200, context +0x18 = 0, bytes +0x524/+0x525 = 0, then
       marks dirty bit 0x08. */
}

void RenderBeginPrimitiveBatch(unsigned char primitiveType, unsigned char vertexFormat, unsigned short vertexCount) {
    /* 0x801D3DF0 is the GXBegin wrapper.

       Confirmed behavior:
       - flushes pending state through RenderFlushPendingState / FUN_801D3B70 when
         the render context dirty flag at DAT_802EA7C8[0x17F] is set.
       - if the render context has no active primitive, emits an internal 0x98
         primitive and enough zero data to clear/fill the previous context batch,
         then marks the context active.
       - writes the actual begin command to the GX FIFO:
           DAT_CC008000 = primitiveType | vertexFormat
           RAM_CC008000 = vertexCount

       In the model submitters the common call is
       RenderBeginPrimitiveBatch(0x98, 0, vertexCount), which is GX primitive type
       0x98 with vertex format 0. */
    (void)primitiveType;
    (void)vertexFormat;
    (void)vertexCount;
}

void RenderSetVertexArray(int attribute, unsigned int arrayBase, unsigned int stride) {
    unsigned int gxAttribute;

    /* 0x801D2FB0 is the GXSetArray wrapper. Attribute 0x19 is normalized to 10,
       then the function writes the array base and stride to paired GX registers:

         register 0xA0 | (attribute - 9) -> arrayBase & 0x3FFFFFFF
         register 0xB0 | (attribute - 9) -> stride

       Known attributes from the Czan model path:
         9  -> position array
         10 -> normal/vector array
         11 -> color array
         13 -> generated texcoord array */
    gxAttribute = (attribute == 0x19) ? 10u : (unsigned int)attribute;
    (void)gxAttribute;
    (void)arrayBase;
    (void)stride;
}

void RenderSetVertexAttrDescriptor(
    unsigned int vertexFormat,
    int attribute,
    unsigned int attrType,
    unsigned int componentType,
    unsigned int componentCount) {
    /* 0x801D2BC0 is the GXSetVtxAttrFmt-style wrapper for one vertex format.

       It packs attrType/componentType/componentCount into render-context words:
         vertexFormat slot +0x1C for attrs 9..13
         vertexFormat slot +0x3C for attrs 14..17
         vertexFormat slot +0x5C for attrs 17..20

       The Czan model code uses this mostly as:
         attr 9  position      componentType 4, componentCount 0
         attr 10 normal/vector componentType 4, componentCount 0
         attr 11 color         componentType 5, componentCount 0
         attr 13 texcoord      componentType 4, componentCount 0

       It marks dirty bit 0x10 and marks the vertex format slot dirty in byte
       renderContext +0x5FB. */
    (void)vertexFormat;
    (void)attribute;
    (void)attrType;
    (void)componentType;
    (void)componentCount;
}

void RenderSetVertexAttrFormat(int attribute, unsigned int format) {
    /* 0x801D2730 is the GXSetVtxDesc-style wrapper. It stores per-attribute format
       bits in the render context and marks dirty bit 0x08.

       Observed Czan model meanings:
         format 1 -> direct/indexed attribute mode without a separate array base
         format 3 -> array-backed attribute mode; caller also calls RenderSetVertexArray
         format 0 -> disabled

       Special attributes:
         10 and 0x19 share a small exclusive state at context +0x520/+0x524/+0x525
         and update bits 0x0B..0x0C in context word +0x14. */
    (void)attribute;
    (void)format;
}

void RenderSetBlendMode(unsigned int blendEnabled, unsigned int srcFactor, unsigned int dstFactor, unsigned int logicOp) {
    /* 0x801D7110 writes the GX blend/control register through FIFO command 0x61.

       Packed fields recovered from the original:
       - bit 0 uses blendEnabled & 1
       - srcFactor uses bits 8..10
       - dstFactor uses bits 5..7
       - logicOp uses bits 12..15
       - blendEnabled values 2/3 also influence additional control bits through
         countLeadingZeros(param - 2/3)

       CzanModel_ApplyMaterialBlendMode calls this with tuples such as
       (1,4,5,5), (1,0,5,5), and (1,4,1,5). */
    (void)blendEnabled;
    (void)srcFactor;
    (void)dstFactor;
    (void)logicOp;
}

void RenderSetAlphaUpdate(unsigned int enabled) {
    /* 0x801D7240 updates bit 6 of context +0x22C, writes that register through FIFO
       command 0x61, and clears the context active-primitive marker. */
    (void)enabled;
}

void RenderSetAlphaCompare(unsigned int compare0, unsigned int reference0, unsigned int op, unsigned int compare1, unsigned int reference1) {
    /* 0x801D6B70 writes a packed GX alpha compare register:

       0xF3000000 |
       ((op & 3) << 22) |
       ((compare1 & 7) << 19) |
       ((compare0 & 7) << 16) |
       ((reference1 & 0xFF) << 8) |
       (reference0 & 0xFF)

       Czan material state uses this for compare modes:
       - RenderSetAlphaCompare(7, 0,    1, 7, 0)
       - RenderSetAlphaCompare(4, 0xA0, 0, 3, 0xFF)
       - RenderSetAlphaCompare(4, 0,    0, 3, 0xFF) */
    (void)compare0;
    (void)reference0;
    (void)op;
    (void)compare1;
    (void)reference1;
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

void DrawLine2D(int x0, int y0, int x1, int y1, const unsigned int *color) {
    Platform_DrawLine2D(x0, y0, x1, y1, color);
}

void DrawTriangle2D(
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    const unsigned int *color) {
    Platform_DrawTriangle2D(x0, y0, x1, y1, x2, y2, color);
}

void DrawTexturedTriangle2D(
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
    Platform_DrawTexturedTriangle2D(
        x0, y0, u0, v0,
        x1, y1, u1, v1,
        x2, y2, u2, v2,
        textureHandle, textureIndex, color);
}

void DrawTexturedTriangleStrip2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex) {
    Platform_DrawTexturedTriangleStrip2D(points, texcoords, colors, vertexCount, textureHandle, textureIndex);
}

void UiRootManager_LoadResource(int *uiRootManager, void *linkData) {
    /* 0x800FE548 links a WII UI-root resource and initializes the UI root manager.
       It creates Czan object groups from blocks 0..3 using the global Czan UI manager
       at *(DAT_802E71B8 + 0x270), initializes several sub-managers from blocks 4..8,
       creates five standalone TPL textures from blocks 9..13, and sets uiRootManager[0] = 1. */
    (void)uiRootManager;
    (void)linkData;
}

void UiEffectController_ResetOrStartFade(double duration, int *effectController) {
    /* 0x800FB698 resets or starts a fade-like UI/effect controller transition.

       If effectController +0x44 is -1, the original returns immediately. Positive
       durations set +0x4C active and store the duration at +0x68. Zero/non-positive
       durations clear active ids at +0x44/+0x58/+0x5C/+0x60, clear +0x4C, and reset
       +0x64/+0x68. */
    (void)duration;
    (void)effectController;
}

void UiEffectController_StartMultiTargetFade(double duration, int *effectController, int primaryTarget, int secondaryTargetA, int secondaryTargetB) {
    /* 0x800FB608 starts a fade/effect transition when the effect controller is
       enabled (+0x40 != 0) and no primary target is active (+0x44 == -1).

       It stores:
       +0x44 = primaryTarget
       +0x4C = 0
       +0x58 = -1
       +0x5C = secondaryTargetA
       +0x60 = secondaryTargetB
       +0x64 = 0.0f
       +0x68 = duration */
    (void)duration;
    (void)effectController;
    (void)primaryTarget;
    (void)secondaryTargetA;
    (void)secondaryTargetB;
}

void UiEffectController_StartSingleTargetFade(double duration, int *effectController, int primaryTarget, int secondaryTarget) {
    /* 0x800FB64C starts the single-secondary-target fade/effect variant when the
       effect controller is enabled and no primary target is active.

       It stores:
       +0x44 = primaryTarget
       +0x4C = 0
       +0x50 = 1
       +0x58 = secondaryTarget
       +0x5C/+0x60 = -1
       +0x64 = 0.0f
       +0x68 = duration */
    (void)duration;
    (void)effectController;
    (void)primaryTarget;
    (void)secondaryTarget;
}

int UiEffectController_GetState(int *effectController) {
    /* 0x800FD3B8 returns the UI/effect controller state at +0x48. */
    if (effectController == 0) {
        return 0;
    }
    return effectController[0x12];
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
