#include "render/render_engine.h"

#include "platform/render_backend.h"
#include "resource/czan_link.h"
#include "resource/resource_manager.h"
#include "runtime/cache.h"
#include "runtime/memory.h"
#include "runtime/module_system.h"
#include "ui/czan_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEXTURE_SLOT_AUTO 0xFFFFFFFFu
#define UI_ROOT_TEXTURE_SLOTS 512
#define UI_ROOT_HOST_POINTERS 128

static float gUiScreenProjectionMatrix802ef368[16];
static float gUiScreenViewMatrix802ef3a8[12];
static TextureSlotKnownFields gUiRootTextureSlots[UI_ROOT_TEXTURE_SLOTS];
static TextureManagerKnownFields gUiRootTextureManager = {
    gUiRootTextureSlots,
    0,
    UI_ROOT_TEXTURE_SLOTS
};
static void *gUiRootHostPointers[UI_ROOT_HOST_POINTERS];
static int *gUiRootCurrentManager;
static unsigned int gHostNextTextureSlot;
static int gUiPromptNextSurfaceSlot = 1;

static void UiRootBootTransition_Reset(int *subManager);

static unsigned char *UiRootBootPromptHelper_Get(int bits) {
    return (unsigned char *)UiRootHostPointerFromBits(bits);
}

static void UiRootBootPromptHelper_Init(unsigned char *helper) {
    if (helper == 0) {
        return;
    }

    ClearMemory(helper, 0, 0xcc);
    helper[0] = 0;
    helper[1] = 1;
    *(int *)(void *)(helper + 0x08) = -1;
    *(int *)(void *)(helper + 0x10) = -1;
    *(int *)(void *)(helper + 0x14) = -1;
    helper[0xb0] = 0xff;
    helper[0xb1] = 0xff;
    helper[0xb2] = 0xff;
    helper[0xb3] = 0xff;
    *(float *)(void *)(helper + 0xa8) = 1.0f;
    *(float *)(void *)(helper + 0xac) = 1.0f;
    *(int *)(void *)(helper + 0xc0) = -1;
    *(int *)(void *)(helper + 0xc4) = 1;
    *(int *)(void *)(helper + 0xc8) = 1;
}

static int UiRootBootPromptHelper_AllocateBits(void) {
    unsigned char *helper = (unsigned char *)MemoryPool_AllocateAligned(0, 0xcc, 0x20);
    if (helper == 0) {
        return 0;
    }
    UiRootBootPromptHelper_Init(helper);
    return UiRootHostPointerBits(helper);
}

static void UiRootBootPromptHelper_Bind(int helperBits, int objectGroupHandle, int childObjectIndex, int auxValue) {
    unsigned char *helper = UiRootBootPromptHelper_Get(helperBits);
    if (helper == 0) {
        return;
    }

    helper[1] = 1;
    *(int *)(void *)(helper + 0xb8) = 0;
    *(int *)(void *)(helper + 0x18) = auxValue;
    *(float *)(void *)(helper + 0x1c) = 1.0f;
    *(float *)(void *)(helper + 0x20) = 1.0f;
    if (objectGroupHandle != -1 && childObjectIndex != -1) {
        *(int *)(void *)(helper + 0x10) = objectGroupHandle;
        *(int *)(void *)(helper + 0x14) = childObjectIndex;
        *(int *)(void *)(helper + 0x0c) = 1;
        CzanUiManager_SetChildObjectExtensionPointer(
            0,
            objectGroupHandle,
            childObjectIndex,
            helperBits,
            1,
            auxValue);
    }
    *(int *)(void *)(helper + 0xc4) = 1;
}

static void UiRootBootPromptHelper_Detach(int helperBits) {
    unsigned char *helper = UiRootBootPromptHelper_Get(helperBits);
    int objectGroupHandle;
    int childObjectIndex;
    int extensionSlot;

    if (helper == 0) {
        return;
    }
    objectGroupHandle = *(int *)(void *)(helper + 0x10);
    childObjectIndex = *(int *)(void *)(helper + 0x14);
    extensionSlot = *(int *)(void *)(helper + 0x18);
    if (objectGroupHandle != -1 && childObjectIndex != -1) {
        CzanUiManager_SetChildObjectExtensionPointer(
            0,
            objectGroupHandle,
            childObjectIndex,
            0,
            0,
            extensionSlot);
    }
    *(int *)(void *)(helper + 0x0c) = 0;
}

static void UiRootBootPromptHelper_Reattach(int helperBits) {
    unsigned char *helper = UiRootBootPromptHelper_Get(helperBits);
    int objectGroupHandle;
    int childObjectIndex;
    int extensionSlot;

    if (helper == 0) {
        return;
    }
    objectGroupHandle = *(int *)(void *)(helper + 0x10);
    childObjectIndex = *(int *)(void *)(helper + 0x14);
    extensionSlot = *(int *)(void *)(helper + 0x18);
    if (objectGroupHandle != -1 && childObjectIndex != -1) {
        CzanUiManager_SetChildObjectExtensionPointer(
            0,
            objectGroupHandle,
            childObjectIndex,
            helperBits,
            0,
            extensionSlot);
        *(int *)(void *)(helper + 0x0c) = 1;
    }
}

static void UiRootBootPromptHelper_SetTextIndex(int helperBits, int textIndex) {
    unsigned char *helper = UiRootBootPromptHelper_Get(helperBits);
    if (helper == 0) {
        return;
    }
    *(int *)(void *)(helper + 0xc0) = textIndex;
    ClearMemory(helper + 0x24, 0, 0x40);
    ClearMemory(helper + 0x68, 0, 0x40);
    *(int *)(void *)(helper + 0x64) = 0;
}

static void UiRootBootPromptHelper_SetEffectSlot(int helperBits, int effectSlot) {
    unsigned char *helper = UiRootBootPromptHelper_Get(helperBits);
    if (helper == 0) {
        return;
    }
    *(int *)(void *)(helper + 0xbc) = effectSlot;
}

static void UiRootBootPromptHelper_EnsureSurface(unsigned char *helper) {
    /* 0x800FD874 calls the font manager allocator (0x800B912C) once and stores
       the surface slot at helper +8. The host reserves a stable slot id here; the
       backing glyph/surface pixels are owned by the pending 0x800B937C port. */
    if (helper != 0 && *(int *)(void *)(helper + 8) == -1) {
        *(int *)(void *)(helper + 8) = gUiPromptNextSurfaceSlot++;
    }
}

void UiPromptEffectHelper_DrawByBits(int helperBits) {
    unsigned char *helper = UiRootBootPromptHelper_Get(helperBits);
    const char *text;
    int *textManager;

    /* 0x800FD908 is the Czan child extension draw callback used by boot/select
       prompt helpers. The real function resolves text through gManager_802E70B0,
       prepares DAT_802E70E8 font state, and calls 0x800B937C. Until the full font
       surface renderer is ported, keep the same gating and text-selection side
       effects and report a ready status without drawing fake host placeholders. */
    if (helper == 0 ||
        *(int *)(void *)(helper + 0x10) == -1 ||
        *(int *)(void *)(helper + 0x14) == -1 ||
        *(int *)(void *)(helper + 0x0c) == 0 ||
        *(int *)(void *)(helper + 0xb8) != 0) {
        return;
    }
    UiRootBootPromptHelper_EnsureSurface(helper);

    if (*(int *)(void *)(helper + 0xc0) == -1) {
        text = (const char *)(helper + 0x24);
    }
    else {
        textManager = GameMain_GetTextManager();
        TextManager_SelectBank(textManager, *(int *)(void *)(helper + 0xbc));
        text = TextManager_GetText(textManager, *(int *)(void *)(helper + 0xc0));
    }

    *(int *)(void *)(helper + 100) =
        (text != 0 && text[0] != '\0' && strcmp(text, "ERROR") != 0) ? 1 : 0;
}

static unsigned int Render_ReadBe32(const unsigned char *data) {
    return ((unsigned int)data[0] << 24) |
           ((unsigned int)data[1] << 16) |
           ((unsigned int)data[2] << 8) |
           (unsigned int)data[3];
}

int UiRootHostPointerBits(void *pointer) {
    int i;

    if (pointer == 0) {
        return 0;
    }
    for (i = 1; i < UI_ROOT_HOST_POINTERS; i++) {
        if (gUiRootHostPointers[i] == pointer) {
            return i;
        }
    }
    for (i = 1; i < UI_ROOT_HOST_POINTERS; i++) {
        if (gUiRootHostPointers[i] == 0) {
            gUiRootHostPointers[i] = pointer;
            return i;
        }
    }
    return 0;
}

void *UiRootHostPointerFromBits(int bits) {
    if (0 < bits && bits < UI_ROOT_HOST_POINTERS) {
        return gUiRootHostPointers[bits];
    }
    return 0;
}

static int UiRootAllocObjectBits(int size) {
    void *object = MemoryPool_AllocateAligned(0, size, 0x20);
    if (object == 0) {
        return 0;
    }
    ClearMemory(object, 0, size);
    return UiRootHostPointerBits(object);
}

void RenderBeginFrame(void) {
    Platform_BeginFrame();
    Platform_SetLogicalProjection(RuntimeVideo_GetFramebufferWidth(), RuntimeVideo_GetFramebufferHeight());
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

void BuildPerspectiveProjectionMatrix(double fovYRadians, double aspect, double nearZ, double farZ, float *outMatrix44) {
    double cotHalfFov;
    double depthScale;
    int i;

    /* FUN_801B0C30 builds the perspective matrix used by the UI/screen projection
       setup. The recovered constants match the standard cot(fov/2) and near/far
       depth terms. */
    if (outMatrix44 == 0) {
        return;
    }

    for (i = 0; i < 16; i++) {
        outMatrix44[i] = 0.0f;
    }

    cotHalfFov = 1.0 / tan(fovYRadians * 0.5);
    depthScale = 1.0 / (farZ - nearZ);

    outMatrix44[0] = (float)(cotHalfFov / aspect);
    outMatrix44[5] = (float)cotHalfFov;
    outMatrix44[10] = (float)(-nearZ * depthScale);
    outMatrix44[11] = (float)(-(farZ * nearZ) * depthScale);
    outMatrix44[14] = -1.0f;
}

void UiScreenProjection_UpdateGlobals(void) {
    float screenWidth;
    float screenHeight;
    float aspect;

    /* FUN_80170FA4 updates the global UI projection/view matrices at DAT_802EF368
       and DAT_802EF3A8 from display config stored under DAT_802E71B8 +0x258.
       Use the recovered low-level VI state as the host source of truth, not the
       OpenGL window projector. */
    screenWidth = (float)RuntimeVideo_GetFramebufferWidth();
    screenHeight = (float)RuntimeVideo_GetFramebufferHeight();
    aspect = screenHeight != 0.0f ? screenWidth / screenHeight : 1.0f;

    BuildPerspectiveProjectionMatrix(45.0 * 3.14159265358979323846 / 180.0,
                                     aspect,
                                     1.0,
                                     10000.0,
                                     gUiScreenProjectionMatrix802ef368);

    ClearMemory(gUiScreenViewMatrix802ef3a8, 0, sizeof(gUiScreenViewMatrix802ef3a8));
    gUiScreenViewMatrix802ef3a8[0] = 1.0f;
    gUiScreenViewMatrix802ef3a8[5] = 1.0f;
    gUiScreenViewMatrix802ef3a8[10] = 1.0f;
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

static int gDebugTextGlyphSize;
static void *gDebugTextFontBacking802ee1d8;
static unsigned short gDebugTextFontMode802ee1dc;
static float gDebugTextFontScaleU802ee1e8;
static float gDebugTextFontScaleV802ee1ec;
static int gDebugTextFontPlaneCount802ee1f0;

void DebugText_SetGlyphSize(int glyphSize) {
    /* 0x80145060 stores DAT_802EE1E4, the glyph quad size used by DebugText_Draw. */
    gDebugTextGlyphSize = glyphSize;
}

void DebugText_InitFontBacking(void) {
    int allocationSize;
    unsigned short atlasWidth;
    unsigned short atlasHeight;
    unsigned short textureWidth;
    unsigned short textureHeight;

    /* FUN_80144F48 initializes the low-level debug text/font backing:
       DAT_802EE1E4 = 0x18, DAT_802EE1F0 = 6, allocates a font backing buffer,
       calls FUN_801A9050 to load/decode the font planes, then derives UV scale
       ratios from header fields +0x10/+0x12 over +0x1E/+0x20. The Wii chooses the
       allocation size from hardware/display mode; the host uses the normal fallback
       size seen in the function. */
    gDebugTextGlyphSize = 0x18;
    gDebugTextFontPlaneCount802ee1f0 = 6;
    gDebugTextFontMode802ee1dc = 0;

    allocationSize = 0x20120;
    gDebugTextFontBacking802ee1d8 = MemoryPool_AllocateAligned(1, allocationSize, 0x20);
    if (gDebugTextFontBacking802ee1d8 == 0) {
        gDebugTextFontScaleU802ee1e8 = 1.0f;
        gDebugTextFontScaleV802ee1ec = 1.0f;
        return;
    }

    ClearMemory(gDebugTextFontBacking802ee1d8, 0, allocationSize);
    DebugText_LoadFontPlanes(gDebugTextFontBacking802ee1d8);

    atlasWidth = *(unsigned short *)((unsigned char *)gDebugTextFontBacking802ee1d8 + 0x10);
    atlasHeight = *(unsigned short *)((unsigned char *)gDebugTextFontBacking802ee1d8 + 0x12);
    textureWidth = *(unsigned short *)((unsigned char *)gDebugTextFontBacking802ee1d8 + 0x1e);
    textureHeight = *(unsigned short *)((unsigned char *)gDebugTextFontBacking802ee1d8 + 0x20);
    gDebugTextFontScaleU802ee1e8 = textureWidth != 0 ? (float)atlasWidth / (float)textureWidth : 1.0f;
    gDebugTextFontScaleV802ee1ec = textureHeight != 0 ? (float)atlasHeight / (float)textureHeight : 1.0f;
}

int DebugText_LoadFontPlanes(void *fontMemory) {
    /* FUN_801A9050 selects which debug font plane(s) to load. It reads a cached
       video/region flag, uses fixed Wii source offsets, and calls FUN_801A8460 plus
       FUN_801A8DE0 to decode one or two font planes. The host cannot read those
       fixed Wii addresses yet, so this is the named integration point. */
    if (fontMemory == 0) {
        return 0;
    }

    return 1;
}

int DebugText_LoadFontPlane(void *scratchOrCompressedData, int planeIndex, void *fontMemory) {
    /* FUN_801A8460 reads a packed "Yay" font blob for one plane from fixed source
       offsets, decompresses it, and for plane 1 patches a few glyph pixels. The
       host exposes the name but waits for the fixed-address read and Yay decoder
       before mutating the font buffer. */
    (void)scratchOrCompressedData;
    (void)planeIndex;
    (void)fontMemory;
    return 0;
}

void DebugText_DecodePackedGlyphPlane(void *fontHeader, void *source, void *destination) {
    unsigned char *header;
    unsigned char *src;
    unsigned char *dst;
    unsigned char *palette;
    int mode;
    unsigned int outputSize;
    unsigned int packedCount;
    int i;

    /* FUN_801A8DE0 expands 2-bit glyph data through the four-byte palette at
       header +0x2C. mode 0 packs two palette nibbles per output byte; mode 2 writes
       four palette bytes per packed source byte. */
    if (fontHeader == 0 || source == 0 || destination == 0) {
        return;
    }

    header = (unsigned char *)fontHeader;
    src = (unsigned char *)source;
    dst = (unsigned char *)destination;
    palette = header + 0x2c;
    mode = *(short *)(header + 0x18);
    outputSize = *(unsigned int *)(header + 0x28);

    if (mode == 0) {
        packedCount = outputSize / 2;
        for (i = (int)packedCount - 1; i >= 0; i--) {
            unsigned char packed = src[i];
            dst[i * 2 + 0] = (unsigned char)((palette[packed >> 6] & 0xf0) |
                                             (palette[(packed >> 4) & 3] & 0x0f));
            dst[i * 2 + 1] = (unsigned char)((palette[(packed >> 2) & 3] & 0xf0) |
                                             (palette[packed & 3] & 0x0f));
        }
    }
    else if (mode == 2) {
        packedCount = (outputSize + 3U) / 4U;
        for (i = (int)packedCount - 1; i >= 0; i--) {
            unsigned char packed = src[i];
            dst[i * 4 + 0] = palette[packed >> 6];
            dst[i * 4 + 1] = palette[(packed >> 4) & 3];
            dst[i * 4 + 2] = palette[(packed >> 2) & 3];
            dst[i * 4 + 3] = palette[packed & 3];
        }
    }

    FlushDataCacheRange((unsigned int)(uintptr_t)destination, (int)outputSize);
}

void DebugText_ConfigureRenderState(int textureMap) {
    /* 0x80145390 configures GX state for bitmap debug-text quads. */
    (void)textureMap;
    RenderSetBlendMode(1, 4, 5, 5);
    RenderClearVertexDescriptors();
    RenderSetVertexAttrFormat(9, 1);
    RenderSetVertexAttrFormat(0x0d, 1);
    RenderSetVertexAttrFormat(0x0b, 2);
    RenderSetVertexAttrDescriptor(0, 9, 1, 3, 0);
    RenderSetVertexAttrDescriptor(0, 0x0d, 1, 4, 0);
    RenderSetVertexAttrDescriptor(0, 0x0b, 1, 5, 0);
}

void DebugText_Draw(int x, int y, const char *text) {
    /* 0x80145104 draws bitmap debug text by resolving glyphs, loading texture map 7,
       and emitting one quad per glyph. The host keeps the named draw call visible in
       stdout until the font texture globals DAT_802EE1D8/DAT_802932E0 are mapped. */
    DebugText_ConfigureRenderState(7);
    printf("debug text (%d,%d size=%d): %s\n",
           x,
           y,
           gDebugTextGlyphSize,
           text != 0 ? text : "");
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
    if (!BindTextureFromTextureSet(textureHandle, 0, textureIndex)) {
        return;
    }
    Platform_DrawTexturedQuad(&quad, color, textureHandle, textureIndex);
}

void *CaptureFrameTextureRegion(int x, int y, int width, int height, int halfScale) {
    return Platform_CaptureFrameTextureRegion(x, y, width, height, halfScale);
}

void DrawCapturedTextureQuad(
    void *textureHandle,
    int x,
    int y,
    int width,
    int height,
    const unsigned int *color,
    int flipY) {
    Platform_DrawCapturedTextureQuad(textureHandle, x, y, width, height, color, flipY);
}

unsigned int CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
) {
    const unsigned char *data = (const unsigned char *)resourceData;
    TextureSlotKnownFields *slot;

    if (textureManager == 0 ||
        textureManager->slots == 0 ||
        data == 0 ||
        resourceSizeOrTplBase < 0x20 ||
        Render_ReadBe32(data) != 0x0020AF30u) {
        return TEXTURE_SLOT_AUTO;
    }

    if (textureSlot == TEXTURE_SLOT_AUTO) {
        textureSlot = gHostNextTextureSlot;
        gHostNextTextureSlot++;
    }

    if (textureSlot >= textureManager->slotCount) {
        return textureSlot;
    }

    slot = &textureManager->slots[textureSlot];
    slot->resourceData = resourceData;
    slot->resourceSizeOrTplBase = resourceSizeOrTplBase;
    TextureSlot_InitFromTpl(slot);
    Platform_CreateTextureFromTplResource(textureManager, resourceData, resourceSizeOrTplBase, textureSlot);
    if (textureManager->nextTextureSlot <= textureSlot) {
        textureManager->nextTextureSlot = textureSlot + 1;
    }
    return textureSlot;
}

int TextureSlot_InitFromTpl(TextureSlotKnownFields *textureSlot) {
    /* Original relocates the TPL texture descriptor table and image/palette offsets,
       then allocates one 0x20-byte GX texture object per TPL texture. */
    return Platform_TextureSlotInitFromTpl(textureSlot);
}

int TextureSlot_Release(TextureSlotKnownFields *textureSlot) {
    /* 0x801467FC releases one 0x14-byte zanTexture slot. The original frees nested
       GX/TPL allocation records when the slot owns them, then clears all four slot
       words and returns 1 when anything was released. */
    int released = 0;

    if (textureSlot == 0) {
        return 0;
    }

    if (textureSlot->gxTextureObjects != 0) {
        MemoryPool_Free(0, (int)(uintptr_t)textureSlot->gxTextureObjects);
        textureSlot->resourceSizeOrTplBase = 0;
        textureSlot->resourceData = 0;
        textureSlot->tplHeader = 0;
        textureSlot->gxTextureObjects = 0;
        released = 1;
    }
    if (textureSlot->resourceData != 0) {
        textureSlot->resourceSizeOrTplBase = 0;
        textureSlot->resourceData = 0;
        released = 1;
    }

    return released;
}

int TextureManager_DeleteTexture(TextureManagerKnownFields *textureManager, int textureSlot) {
    TextureSlotKnownFields *slot;
    int released;

    /* 0x80146B8C releases an indexed texture slot from the manager and decrements
       the manager's live/next count when the slot release succeeds. */
    if (textureManager == 0 ||
        textureManager->slots == 0 ||
        textureSlot < 0 ||
        (unsigned int)textureSlot >= textureManager->slotCount) {
        return 0;
    }

    slot = &textureManager->slots[textureSlot];
    if (slot->resourceData == 0) {
        return 0;
    }

    released = TextureSlot_Release(slot);
    if (released == 1) {
        if (textureManager->nextTextureSlot != 0) {
            textureManager->nextTextureSlot--;
        }
    }
    else {
        RuntimeDebugAssert("zanTexture", 0, "warning DeleteTexture");
    }
    return released;
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

void DrawTexturedTriangleList2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex) {
    Platform_DrawTexturedTriangleList2D(points, texcoords, colors, vertexCount, textureHandle, textureIndex);
}

void DrawMovieYuvFrame(
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
    Platform_DrawMovieYuvFrame(planeY, planeU, planeV, width, height, frameToken, x, y, drawWidth, drawHeight);
}

void PlayMoviePcm16(const short *samples, int sampleCount, int channelCount, int sampleRate) {
    Platform_PlayMoviePcm16(samples, sampleCount, channelCount, sampleRate);
}

void UiRootManager_RegisterResource(int *uiRootManager, void *linkData) {
    /* Boot-safe half of 0x800FE548. The real game can have comAF resident before
       every screen is visible; visibility is driven later by UI state helpers.
       The host eager loader used to instantiate every CAE group immediately, which
       made all comAF sprites appear at once during boot. */
    unsigned int linkSize;
    unsigned int blockIndex;
    unsigned int blockCount;
    CzanLinkBlock block;

    if (uiRootManager == 0 || linkData == 0) {
        return;
    }

    gUiRootCurrentManager = uiRootManager;
    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }

    blockCount = CzanLinkResource_GetBlockCount(linkData, linkSize);
    for (blockIndex = 0; blockIndex < blockCount; blockIndex++) {
        if (CzanLinkResource_GetBlock(linkData, linkSize, blockIndex, &block)) {
            HostCzan_RegisterLinkSize(block.data, block.size);
        }
    }

    if (uiRootManager[0x0c] == 0 && CzanLinkResource_GetBlock(linkData, linkSize, 4, &block)) {
        int objectBits = UiRootAllocObjectBits(0xe8);
        uiRootManager[0x0c] = objectBits;
        HostCzan_RegisterLinkSize(block.data, block.size);
        UiRootSubManager_LoadCzanGroups((int *)UiRootHostPointerFromBits(objectBits), (void *)block.data);
    }

    uiRootManager[0] = 1;
    uiRootManager[0x13] = UiRootHostPointerBits(linkData);
}

void UiRootManager_LoadResource(int *uiRootManager, void *linkData) {
    /* 0x800FE548 links a WII UI-root resource and initializes the UI root manager.
       It creates Czan object groups from blocks 0..3 using the global Czan UI manager
       at *(DAT_802E71B8 + 0x270), initializes several sub-managers from blocks 4..8,
       creates five standalone TPL textures from blocks 9..13, and sets uiRootManager[0] = 1. */
    unsigned int linkSize;
    unsigned int blockCount;
    unsigned int blockIndex;
    CzanLinkBlock block;
    int groupHandle;
    int objectBits;

    if (uiRootManager == 0 || linkData == 0) {
        return;
    }

    gUiRootCurrentManager = uiRootManager;
    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }

    blockCount = CzanLinkResource_GetBlockCount(linkData, linkSize);
    for (blockIndex = 0; blockIndex < 4 && blockIndex < blockCount; blockIndex++) {
        if (!CzanLinkResource_GetBlock(linkData, linkSize, blockIndex, &block)) {
            continue;
        }

        HostCzan_RegisterLinkSize(block.data, block.size);
        groupHandle = CzanUiManager_CreateObjectGroup(0, (void *)block.data, 0, 0);
        uiRootManager[1 + blockIndex] = groupHandle;
    }

    objectBits = UiRootAllocObjectBits(0xe8);
    uiRootManager[0x0c] = objectBits;
    if (CzanLinkResource_GetBlock(linkData, linkSize, 4, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        UiRootSubManager_LoadCzanGroups((int *)UiRootHostPointerFromBits(objectBits), (void *)block.data);
    }

    objectBits = UiRootAllocObjectBits(0x84);
    uiRootManager[0x0d] = objectBits;
    if (CzanLinkResource_GetBlock(linkData, linkSize, 5, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        UiRootSubManager_LoadCzanGroupsWithTexture((int *)UiRootHostPointerFromBits(objectBits), (void *)block.data);
    }

    objectBits = UiRootAllocObjectBits(0x7c8);
    uiRootManager[0x0e] = objectBits;
    UiRootSubManager_InitTextureFrameGroups((int *)UiRootHostPointerFromBits(objectBits));

    objectBits = UiRootAllocObjectBits(0x14);
    uiRootManager[0x0f] = objectBits;
    if (CzanLinkResource_GetBlock(linkData, linkSize, 6, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        UiRootSubManager_LoadLinkedObjectGroup((int *)UiRootHostPointerFromBits(objectBits), (void *)block.data);
    }

    uiRootManager[0x10] = UiRootAllocObjectBits(0x10);
    if (CzanLinkResource_GetBlock(linkData, linkSize, 7, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
    }

    objectBits = UiRootAllocObjectBits(0x3a8);
    uiRootManager[0x12] = objectBits;
    if (CzanLinkResource_GetBlock(linkData, linkSize, 8, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
    }

    for (blockIndex = 0; blockIndex < 5; blockIndex++) {
        if (CzanLinkResource_GetBlock(linkData, linkSize, blockIndex + 9, &block)) {
            uiRootManager[5 + blockIndex] = (int)CreateTextureFromTplResource(
                &gUiRootTextureManager,
                (void *)block.data,
                (int)block.size,
                TEXTURE_SLOT_AUTO);
        }
        else {
            uiRootManager[5 + blockIndex] = -1;
        }
    }

    objectBits = UiRootAllocObjectBits(0x20);
    uiRootManager[0x11] = objectBits;
    uiRootManager[0] = 1;
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
       referenceObjectGroupHandle/referenceChildIndex with linkMode. It then
       suppresses the referenced child object through CzanUiManager_SetChildObjectEnabled
       with value 1, matching FUN_801002FC. */
    int sourceGroupHandle;
    int cloneGroupHandle;

    if (uiRootManager == 0) {
        uiRootManager = gUiRootCurrentManager;
    }
    if (uiRootManager == 0) {
        return -1;
    }

    sourceGroupHandle = uiRootManager[1];
    if (sourceGroupHandle == -1) {
        return -1;
    }

    cloneGroupHandle = CzanUiManager_CloneObjectGroup(0, sourceGroupHandle, 2, 0);
    if (cloneGroupHandle == -1) {
        return -1;
    }

    if (referenceChildIndex != -1 && referenceObjectGroupHandle != -1) {
        CzanUiManager_LinkObjectGroupToReferenceObject(
            0,
            cloneGroupHandle,
            referenceObjectGroupHandle,
            referenceChildIndex,
            linkMode);
        CzanUiManager_SetChildObjectEnabled(0, referenceObjectGroupHandle, referenceChildIndex, 1);
    }

    return cloneGroupHandle;
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
    unsigned int linkSize;
    unsigned int blockIndex;
    unsigned int blockCount;
    int groupHandle;

    if (subManager == 0 || linkData == 0) {
        return;
    }
    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }
    blockCount = CzanLinkResource_GetBlockCount(linkData, linkSize);
    for (blockIndex = 0; blockIndex < 4 && blockIndex < blockCount; blockIndex++) {
        CzanLinkBlock block;
        if (CzanLinkResource_GetBlock(linkData, linkSize, blockIndex, &block)) {
            HostCzan_RegisterLinkSize(block.data, block.size);
            subManager[blockIndex] = CzanUiManager_CreateObjectGroup(0, (void *)block.data, 2, 0);
        }
        else {
            subManager[blockIndex] = -1;
        }
    }

    groupHandle = subManager[3];
    for (blockIndex = 4; blockIndex <= 8; blockIndex++) {
        subManager[blockIndex] =
            groupHandle != -1 ? CzanUiManager_CloneObjectGroup(0, groupHandle, 2, 0) : -1;
    }

    subManager[9] = UiRootManager_CreateReferenceObjectGroup(0, subManager[1], 0x0d, 0x1f);
    if (subManager[9] != -1 && subManager[1] != -1) {
        int referenceEdge = CzanUiManager_GetChildObjectReferenceEdge(0, subManager[1], 0x0d);
        CzanUiManager_AlignObjectGroupByReferenceEdge(0, subManager[9], referenceEdge, 0);
    }

    subManager[10] = UiRootManager_CreateReferenceObjectGroup(0, subManager[2], 0x10, 0x1f);
    if (subManager[10] != -1 && subManager[2] != -1) {
        int referenceEdge = CzanUiManager_GetChildObjectReferenceEdge(0, subManager[2], 0x10);
        CzanUiManager_AlignObjectGroupByReferenceEdge(0, subManager[10], referenceEdge, 0);
    }

    subManager[0x1d] = subManager[3] != -1 ?
        CzanUiManager_GetChildObjectReferenceEdge(0, subManager[3], 0) :
        0;

    subManager[0x1f] = UiRootBootPromptHelper_AllocateBits();
    UiRootBootPromptHelper_Bind(subManager[0x1f], subManager[1], 9, 0);
    UiRootBootPromptHelper_Detach(subManager[0x1f]);
    if (UiRootBootPromptHelper_Get(subManager[0x1f]) != 0) {
        *(int *)(void *)(UiRootBootPromptHelper_Get(subManager[0x1f]) + 0xc8) = 0;
    }

    subManager[0x20] = UiRootBootPromptHelper_AllocateBits();
    UiRootBootPromptHelper_Bind(subManager[0x20], subManager[2], 0x0c, 0);
    UiRootBootPromptHelper_Detach(subManager[0x20]);
    if (UiRootBootPromptHelper_Get(subManager[0x20]) != 0) {
        *(int *)(void *)(UiRootBootPromptHelper_Get(subManager[0x20]) + 0xc8) = 0;
    }

    for (blockIndex = 0; blockIndex < 6; blockIndex++) {
        subManager[0x21 + (int)blockIndex] = UiRootBootPromptHelper_AllocateBits();
        UiRootBootPromptHelper_Bind(
            subManager[0x21 + (int)blockIndex],
            subManager[3 + (int)blockIndex],
            2,
            0);
        UiRootBootPromptHelper_Detach(subManager[0x21 + (int)blockIndex]);
        if (UiRootBootPromptHelper_Get(subManager[0x21 + (int)blockIndex]) != 0) {
            *(int *)(void *)(UiRootBootPromptHelper_Get(subManager[0x21 + (int)blockIndex]) + 0xc8) = 0;
        }
    }

    UiRootBootTransition_Reset(subManager);
}

static void UiRootBootTransition_Reset(int *subManager) {
    int i;

    if (subManager == 0) {
        return;
    }

    subManager[0x0c] = 0;
    subManager[0x0e] = 0;
    subManager[0x0d] = 4;
    subManager[0x10] = 0;
    subManager[0x0b] = -1;
    subManager[0x0f] = 0;
    subManager[0x11] = -1;
    subManager[0x1b] = -1;
    subManager[0x1a] = 1;
    subManager[0x1c] = 0;
    subManager[0x1e] = -1;
    ClearMemory(subManager + 0x12, 0, 0x20);

    if (subManager[0] != -1) {
        CzanUiManager_SetObjectGroupEnabled(0, subManager[0], 1);
    }

    for (i = 0; i < 0x0b; i++) {
        if (subManager[i] != -1) {
            CzanUiManager_SetObjectGroupDisplayFlags(0, subManager[i], 1, 1);
        }
    }

    for (i = 3; i < 9; i++) {
        if (subManager[i] != -1) {
            CzanUiManager_AlignObjectGroupByReferenceEdge(0, subManager[i], subManager[0x1d], 0);
        }
    }

    for (i = 0; i < 2; i++) {
        UiRootBootPromptHelper_Detach(subManager[0x1f + i]);
    }
    for (i = 0; i < 6; i++) {
        UiRootBootPromptHelper_Detach(subManager[0x21 + i]);
    }
}

void UiRootBootTransition_Update(int *subManager) {
    if (subManager == 0 || subManager[0x0b] == -1) {
        return;
    }

    if (subManager[0x0c] == 0) {
        if (subManager[0] == -1 || CzanUiManager_IsObjectGroupAnimationDone(0, subManager[0]) != 0) {
            subManager[0x0c] = 1;
            subManager[0x10] = 0;
            if (subManager[0x44 / 4] == -1) {
                subManager[0x44 / 4] = 0;
            }
        }
    }
    else if (subManager[0x0c] == 2) {
        if (subManager[0] == -1 || CzanUiManager_IsObjectGroupAnimationDone(0, subManager[0]) != 0) {
            UiRootBootTransition_Reset(subManager);
        }
    }
}

void UiRootBootTransition_AttachPromptHelpersForStart(int *subManager) {
    int helperIndex;
    int i;

    /* 0x801023D8 reattaches one table-selected primary prompt helper through
       0x800FD7AC, then reattaches the six option helpers at subManager +0x84..+0x98.
       The DAT_8027BA50 mode table is still being recovered, so this uses the
       already-selected active helper index and keeps the child index from the
       construction-time 0x800FD630 bind. */
    if (subManager == 0) {
        return;
    }
    helperIndex = subManager[0x0e];
    if (helperIndex < 0 || helperIndex > 1) {
        helperIndex = 0;
    }
    UiRootBootPromptHelper_Reattach(subManager[0x1f + helperIndex]);
    for (i = 0; i < 6; i++) {
        UiRootBootPromptHelper_Reattach(subManager[0x21 + i]);
    }
}

void UiRootBootTransition_ConfigurePromptHelper(int *subManager, int effectSlot, int baseEffectId) {
    int helperIndex;

    if (subManager == 0) {
        return;
    }
    helperIndex = subManager[0x0e];
    if (helperIndex < 0 || helperIndex > 7) {
        return;
    }
    UiRootBootPromptHelper_SetEffectSlot(subManager[0x1f + helperIndex], effectSlot);
    UiRootBootPromptHelper_SetTextIndex(subManager[0x1f + helperIndex], baseEffectId);
}

void UiRootBootTransition_ClearActivePromptHelperText(int *subManager) {
    unsigned char *helper;
    int helperIndex;

    if (subManager == 0) {
        return;
    }
    helperIndex = subManager[0x0e];
    if (helperIndex < 0 || helperIndex > 7) {
        return;
    }
    helper = UiRootBootPromptHelper_Get(subManager[0x1f + helperIndex]);
    if (helper != 0) {
        ClearMemory(helper + 0x68, 0, 0x40);
    }
}

void UiRootBootTransition_SetSelectedOptionHelperText(int *subManager, int selectedOption) {
    int helperIndex;

    if (subManager == 0 || subManager[0x1c] != 1 || subManager[0x1b] == -1) {
        return;
    }
    helperIndex = subManager[0x0e];
    if (helperIndex < 0 || helperIndex > 7) {
        return;
    }
    UiRootBootPromptHelper_SetTextIndex(
        subManager[0x1f + helperIndex],
        subManager[0x1b] + selectedOption);
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
    unsigned int linkSize;
    unsigned int blockIndex;
    CzanLinkBlock block;

    if (subManager == 0 || linkData == 0) {
        return;
    }
    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }

    for (blockIndex = 0; blockIndex < 4; blockIndex++) {
        if (CzanLinkResource_GetBlock(linkData, linkSize, blockIndex, &block)) {
            HostCzan_RegisterLinkSize(block.data, block.size);
            subManager[blockIndex] = CzanUiManager_CreateObjectGroup(0, (void *)block.data, 0, 0);
            CzanUiManager_SetObjectGroupDrawEnabled(0, subManager[blockIndex], 0);
        }
        else {
            subManager[blockIndex] = -1;
        }
    }

    subManager[4] = UiRootManager_CreateReferenceObjectGroup(0, -1, -1, 0x1f);
    CzanUiManager_SetObjectGroupDrawEnabled(0, subManager[4], 0);
    if (CzanLinkResource_GetBlock(linkData, linkSize, 4, &block)) {
        subManager[0x0b] = (int)CreateTextureFromTplResource(
            &gUiRootTextureManager,
            (void *)block.data,
            (int)block.size,
            TEXTURE_SLOT_AUTO);
    }
    else {
        subManager[0x0b] = -1;
    }
}

void UiRootSubManager_InitTextureFrameGroups(int *subManager) {
    /* 0x80106054 initializes the 0x7C8-byte sub-manager allocated by
       UiRootManager_LoadResource for WII block 6. It creates two rows of 0x28
       root/object groups through FUN_801002E4(gUiRootManager), with row handles
       stored at subManager + 0x08 and subManager + 0x3E8. The second row is hidden
       immediately with CzanUiManager_SetObjectGroupDrawEnabled(..., 0).

       After allocation, both rows receive texture frames 0..0x27 through
       CzanUiManager_SetObjectTextureFrame(global Czan UI manager, group, 0, frame, 0). */
    int bankIndex;
    int frameIndex;

    if (subManager == 0) {
        return;
    }

    for (bankIndex = 0; bankIndex < 2; bankIndex++) {
        int *bank = subManager + bankIndex * (0x3e0 / 4);
        for (frameIndex = 0; frameIndex < 0x28; frameIndex++) {
            int groupHandle = UiRootManager_CreateReferenceObjectGroup(0, -1, -1, 0x1f);
            bank[2 + frameIndex] = groupHandle;
            CzanUiManager_SetObjectGroupEnabled(0, groupHandle, 1);
            if (bankIndex == 1) {
                CzanUiManager_SetObjectGroupDrawEnabled(0, groupHandle, 0);
            }
        }

        /* The Wii object list is populated by the surrounding UI state, but the
           host cache is flat and would otherwise draw bank 0 before any mask
           reset/update has run. Force one first-pass mask comparison so the
           recovered runtime gate, not the clone defaults, decides visibility. */
        bank[0xb0 / 4] = ~bank[0xa8 / 4];
        bank[0xb4 / 4] = ~bank[0xac / 4];
        bank[0xb8 / 4] = -1;
    }

    for (frameIndex = 0; frameIndex < 0x28; frameIndex++) {
        CzanUiManager_SetObjectTextureFrame(0, subManager[2 + frameIndex], 0, frameIndex, 0);
    }
    for (frameIndex = 0; frameIndex < 0x28; frameIndex++) {
        CzanUiManager_SetObjectTextureFrame(
            0,
            subManager[(0x3e8 / 4) + frameIndex],
            0,
            frameIndex,
            0);
    }
}

void UiRootSubManager_LoadLinkedObjectGroup(int *subManager, void *linkData) {
    /* 0x80106C34 loads the 0x14-byte sub-manager allocated by UiRootManager_LoadResource
       for WII block 6 and stored at uiRootManager[0x0F]. It creates one Czan object
       group from linkData with flags=2, stores that handle at subManager[0], then
       creates/retrieves a linked reference group through
       UiRootManager_CreateReferenceObjectGroup(gUiRootManager, group, 0, 0x1F)
       and stores it at subManager[1]. */
    unsigned int linkSize;

    if (subManager == 0 || linkData == 0) {
        return;
    }
    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }
    subManager[0] = CzanUiManager_CreateObjectGroup(0, linkData, 2, 0);
    subManager[1] = UiRootManager_CreateReferenceObjectGroup(0, subManager[0], 0, 0x1f);
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
    if (uiRootManager == 0 || uiRootManager[0] == 0) {
        return;
    }

    if (uiRootManager[0x0b] == 0) {
        uiRootManager[0x0b] = 1;
        CzanUiManager_DrawObjectListReverse(0, -1);
        uiRootManager[0x0b] = 0;
    }
}

void UiRootManager_DrawGlobalCzanListOnce(int *uiRootManager, int shouldDraw) {
    /* 0x800FEBE8 is the narrower title/select draw helper used by
       CSelectTitleFlow_Draw. It uses uiRootManager +0x2C as a once-per-pass guard,
       draws the global Czan object list when requested, then leaves the guard set
       for the caller's surrounding pass to clear. */
    if (uiRootManager == 0) {
        return;
    }

    if (uiRootManager[0x2c / 4] == 0) {
        if (shouldDraw == 1) {
            CzanUiManager_DrawObjectListReverse(0, -1);
        }
        uiRootManager[0x2c / 4] = 1;
    }
}

void UiRootManager_DrawBootCzanGroups(int *uiRootManager) {
    int *bootController;
    int groupHandles[18];
    int groupCount = 0;
    int i;

    /* Host-scoped companion for the cSelect state-0 boot/save/title phase. The
       retail global list has already been curated by the UI-root submanagers; the
       host keeps all decoded CAE groups resident in one flat cache, so drawing the
       full list lets later select/comAF assets bleed into the strap prompt phase. */
    if (uiRootManager == 0) {
        return;
    }

    for (i = 1; i <= 4; i++) {
        if (uiRootManager[i] >= 0) {
            groupHandles[groupCount++] = uiRootManager[i];
        }
    }

    bootController = (int *)UiRootHostPointerFromBits(uiRootManager[0x0c]);
    if (bootController != 0) {
        for (i = 0; i <= 10 && groupCount < (int)(sizeof(groupHandles) / sizeof(groupHandles[0])); i++) {
            if (bootController[i] >= 0) {
                groupHandles[groupCount++] = bootController[i];
            }
        }
    }

    CzanUiManager_DrawObjectGroupsReverse(0, groupHandles, groupCount);
}

void UiRootManager_UpdateGlobalCzanListOnce(int *uiRootManager, int shouldUpdate) {
    /* 0x800FEB08 is the companion update guard for the global Czan UI list. When
       shouldUpdate is zero it calls FUN_801745B8, then marks uiRootManager +0x28
       so the surrounding pass cannot update the same list twice. */
    if (uiRootManager == 0) {
        return;
    }

    if (uiRootManager[0x28 / 4] == 0) {
        if (shouldUpdate == 0) {
            CzanUiManager_UpdateObjectList(0);
        }
        uiRootManager[0x28 / 4] = 1;
    }
}
