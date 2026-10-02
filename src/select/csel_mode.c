#include "select/csel_mode.h"

#include "game/cgame.h"
#include "render/render_engine.h"
#include "platform/render_backend.h"
#include "model/czan_model.h"
#include "model/zmb_zab.h"
#include "resource/czan_link.h"
#include "runtime/memory.h"
#include "runtime/math.h"
#include "runtime/module_system.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned int gCSelModeHostLinkResourceSize;
static int *gGlobalUiFrameState802e71f8;

#define CSELECT_COMMON_DRAW_CACHE_COUNT 3
#define CSELECT_COMMON_DRAW_VERTEX_CAP 32768
#define CSELECT_COMMON_DRAW_PRIMITIVE_CAP 8192
#define CSELECT_COMMON_DRAW_BATCH_VERTEX_CAP (CSELECT_COMMON_DRAW_VERTEX_CAP * 3)
#define CSELECT_COMMON_CAMERA_OBJECT_CAP 512

typedef struct CSelectCommonDrawCache {
    int *model;
    void *primaryBlock;
    unsigned int primaryBlockSize;
    int textureSlot;
    unsigned int vertexCount;
    unsigned int primitiveCount;
    float vertices[CSELECT_COMMON_DRAW_VERTEX_CAP][3];
    float texcoords[CSELECT_COMMON_DRAW_VERTEX_CAP][2];
    unsigned int colors[CSELECT_COMMON_DRAW_VERTEX_CAP];
    unsigned int primitiveStart[CSELECT_COMMON_DRAW_PRIMITIVE_CAP];
    unsigned int primitiveVertexCount[CSELECT_COMMON_DRAW_PRIMITIVE_CAP];
    unsigned int primitiveTextureIndex[CSELECT_COMMON_DRAW_PRIMITIVE_CAP];
    unsigned char primitiveMaterialMode[CSELECT_COMMON_DRAW_PRIMITIVE_CAP];
    float boundsMin[3];
    float boundsMax[3];
} CSelectCommonDrawCache;

static CSelectCommonDrawCache gCSelectCommonDrawCaches[CSELECT_COMMON_DRAW_CACHE_COUNT];
static unsigned int gCSelectCommonDrawCacheCursor;
static int gCSelectCommonBatchPoints[CSELECT_COMMON_DRAW_BATCH_VERTEX_CAP][2];
static float gCSelectCommonBatchTexcoords[CSELECT_COMMON_DRAW_BATCH_VERTEX_CAP][2];
static unsigned int gCSelectCommonBatchColors[CSELECT_COMMON_DRAW_BATCH_VERTEX_CAP];

static void CzanTextureSurface_InitHost(int *surface) {
    if (surface == 0) {
        return;
    }

    /* 0x800F9898: the executable initializes this 0x1c-byte surface at
       selectCommon +0x234 before the select_cmn resource is bound. The host does
       not store a raw heap pointer in +0x00 because pointers are wider than the
       original 32-bit field. */
    memset(surface, 0, 0x1c);
    surface[3] = 6;
}

static void CzanTextureSurface_AllocateHost(int *surface, int width, int height, int format, int heap) {
    if (surface == 0) {
        return;
    }

    if (width < 0) {
        width = 0;
    }
    if (height < 0) {
        height = 0;
    }

    /* 0x800F9984 layout:
       +0x00 pixels, +0x04 byteSize, +0x08 heap, +0x0c format,
       +0x10 width, +0x12 height, +0x14 flags. */
    surface[0] = (width > 0 && height > 0) ? 1 : 0;
    surface[1] = width * height * 4;
    surface[2] = heap;
    surface[3] = format;
    *(unsigned short *)(void *)((unsigned char *)surface + 0x10) = (unsigned short)width;
    *(unsigned short *)(void *)((unsigned char *)surface + 0x12) = (unsigned short)height;
    surface[5] = 0;
}

void CSelectCommon_Init(int *selectCommon) {
    unsigned char *bytes;

    if (selectCommon == 0) {
        return;
    }

    bytes = (unsigned char *)selectCommon;

    /* 0x8009812C constructs the common select background: two CtsStageObj
       layers, one CzanModelOwner camera/controller, one capture surface, and
       three CSelModeEntry records. The host only has direct structs for the
       pieces it emulates, but the offsets and initial matrix/state match. */
    CtsStageObj_InitBase((int *)(void *)(bytes + 0x48));
    CtsStageObj_InitBase((int *)(void *)(bytes + 0xb8));
    CzanModelOwner_Init((int *)(void *)(bytes + 0x128));
    CzanTextureSurface_InitHost((int *)(void *)(bytes + 0x234));
    CSelModeEntry_Init((CSelModeEntryKnownFields *)(void *)(bytes + 0x254));
    CSelModeEntry_Init((CSelModeEntryKnownFields *)(void *)(bytes + 0x2a4));
    CSelModeEntry_Init((CSelModeEntryKnownFields *)(void *)(bytes + 0x2f4));

    memset(bytes, 0, 0x18);
    Matrix34_SetIdentity((float *)(void *)(bytes + 0x18));
    *(int *)(void *)(bytes + 0x344) = -1;
    *(int *)(void *)(bytes + 0x348) = 0;
    *(int *)(void *)(bytes + 0x36c) = 1;
    *(int *)(void *)(bytes + 0x370) = 0;
}

#define HOST_CZAN_MAX_GROUPS 512
#define HOST_CZAN_MAX_OBJECTS 4096
#define HOST_CZAN_MAX_LINK_SIZES 128
#define HOST_CZAN_TEXTURE_SLOTS 512

typedef struct HostCzanObject {
    int used;
    int groupHandle;
    int childIndex;
    int descriptorType;
    int drawState172;
    int suppressDraw173;
    int drawEnabled;
    int listedForDraw;
    int activeByte17d;
    int animationMode175;
    int animationReset174;
    int currentAnimation;
    int referenceEdge160;
    int referenceEdge164;
    int referenceEdgeActive184;
    int drawPriority168;
    int drawLayer18c;
    unsigned int animationCommandOffset;
    int animationWaitTicks;
    int animationDoneB1;
    int animationDoneB2;
    int animationPlaying171;  /* +0x171: set by 0x80172CC8 when the started anim has frames */
    int presented0b0;         /* +0xB0: cleared on start, set once the script ran a frame */
    float animationStartFrame;
    float animationClockB4;   /* +0xB4: animation time in frames, advanced by fps/60 */
    int textureFrameSet148;   /* +0x148 != -2: frame chosen by code, overrides opcode 04 */
    int textureFrame148;
    int linkedHandle188;
    int extensionHelperBits;
    int extensionHelperReleaseFlag;
    int extensionHelperSlot;
    int spriteRenderMode;
    float depth;
    int textureSlot;
    int textureIndex;
    float baseX;
    float baseY;
    float localOffsetX;
    float localOffsetY;
    float positionOffsetX;
    float positionOffsetY;
    float animationOffsetX;
    float animationOffsetY;
    float baseScaleX;
    float baseScaleY;
    float baseScaleZ;
    float scaleX;
    float scaleY;
    float scaleZ;
    float localOffsetZ;
    float rotationX;
    float rotationY;
    float rotationZ;
    float uvBaseX;
    float uvBaseY;
    float uvOffsetX;
    float uvOffsetY;
    float uvSpanX;
    float uvSpanY;
    float cornerOffsetTopLeftX;
    float cornerOffsetTopLeftY;
    float cornerOffsetTopRightX;
    float cornerOffsetTopRightY;
    float cornerOffsetBottomRightX;
    float cornerOffsetBottomRightY;
    float cornerOffsetBottomLeftX;
    float cornerOffsetBottomLeftY;
    float x;
    float y;
    int width;
    int height;
    unsigned char color[4];
    unsigned char blockColor[4];  /* 0x80172F30 colour block, multiplied into the draw colour */
    const unsigned char *metadata;
    unsigned int metadataSize;
    unsigned int descriptorOffset;
    char name[17];
} HostCzanObject;

typedef struct HostCzanGroup {
    int used;
    int state;
    int childCount;
    int firstObjectIndex;
    unsigned char groupByte08;
    unsigned char groupByte09;
    unsigned char statusFlags24;
    unsigned char byte25;
} HostCzanGroup;

typedef struct HostLinkSizeEntry {
    const void *data;
    unsigned int size;
} HostLinkSizeEntry;

static TextureSlotKnownFields gHostTextureSlots[HOST_CZAN_TEXTURE_SLOTS];
static TextureManagerKnownFields gHostTextureManager = {
    gHostTextureSlots,
    0,
    HOST_CZAN_TEXTURE_SLOTS
};
static HostCzanGroup gHostCzanGroups[HOST_CZAN_MAX_GROUPS];
static HostCzanObject gHostCzanObjects[HOST_CZAN_MAX_OBJECTS];
static HostLinkSizeEntry gHostLinkSizes[HOST_CZAN_MAX_LINK_SIZES];
static void *gUiManagerHostPointers[32];
static CSelModeEntryKnownFields *gHostCSelModeActiveEntries;
static unsigned char *gHostCSelModeActiveMode;

static const int CSelMode_SoundOrTextIdTableHost[CSEL_MODE_ENTRY_COUNT] = {
    -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static float CSelMode_PositionTableHost[6][4][3] = {
    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f}, {-20.0f, -20.0f, 0.0f}, {-20.0f, -20.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f}, {-20.0f, -20.0f, 0.0f}, {-20.0f, -20.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{-16.0f, -16.0f, 0.0f}, {-4.0f, -4.0f, 0.0f}, {-10.0f, -10.0f, 0.0f}, {-16.0f, -16.0f, 0.0f}},
    {{-16.0f, -16.0f, 0.0f}, {-20.0f, -20.0f, 0.0f}, {-20.0f, -20.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}
};

static float CSelMode_AnimationTableHost[6][4][3] = {
    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f}, {-0.37f, 0.0f, 0.0f}, {-0.37f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f}, {-0.37f, 0.0f, 0.0f}, {-0.37f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{-0.3f, 0.0f, 0.0f}, {-0.1f, 0.0f, 0.0f}, {-0.2f, 0.0f, 0.0f}, {-0.3f, 0.0f, 0.0f}},
    {{-0.3f, 0.0f, 0.0f}, {-0.37f, 0.0f, 0.0f}, {-0.37f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}
};

static const int CSelModeEntry_PriorityBaseTableHost[8] = {
    -16384, -8192, -512, -18000, -16000, -8000, 1, -500
};

static int UiManagerHostPointerBits(void *pointer) {
    int i;

    if (pointer == 0) {
        return 0;
    }
    for (i = 1; i < (int)(sizeof(gUiManagerHostPointers) / sizeof(gUiManagerHostPointers[0])); i++) {
        if (gUiManagerHostPointers[i] == pointer) {
            return i;
        }
    }
    for (i = 1; i < (int)(sizeof(gUiManagerHostPointers) / sizeof(gUiManagerHostPointers[0])); i++) {
        if (gUiManagerHostPointers[i] == 0) {
            gUiManagerHostPointers[i] = pointer;
            return i;
        }
    }
    return HostPointer_ToBits32(pointer, "uimanager");
}

static void *UiManagerHostPointerFromBits(int bits) {
    if (0 < bits && bits < (int)(sizeof(gUiManagerHostPointers) / sizeof(gUiManagerHostPointers[0])) &&
        gUiManagerHostPointers[bits] != 0) {
        return gUiManagerHostPointers[bits];
    }
    return (void *)(uintptr_t)bits;
}

static void HostCzan_Reset(void) {
    memset(gHostCzanGroups, 0, sizeof(gHostCzanGroups));
    memset(gHostCzanObjects, 0, sizeof(gHostCzanObjects));
    memset(gHostLinkSizes, 0, sizeof(gHostLinkSizes));
    memset(gHostTextureSlots, 0, sizeof(gHostTextureSlots));
    gHostTextureManager.nextTextureSlot = 0;
}

void HostCzan_RegisterLinkSize(const void *data, unsigned int size) {
    int i;

    if (data == 0 || size == 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_LINK_SIZES; i++) {
        if (gHostLinkSizes[i].data == data || gHostLinkSizes[i].data == 0) {
            gHostLinkSizes[i].data = data;
            gHostLinkSizes[i].size = size;
            return;
        }
    }
}

static int CSelMode_IsVerbose(void) {
    return getenv("DDRII_HOST_VERBOSE") != 0;
}

static int *CSelMode_GetSelectCommon(void) {
    int *selectCommon;

    if (gHostCSelModeActiveMode != 0) {
        selectCommon = (int *)RuntimeHostPointerFromBits(*(int *)(void *)(gHostCSelModeActiveMode + 0x140));
        if (selectCommon != 0) {
            return selectCommon;
        }
    }

    selectCommon = GameMain_GetCharacterAssetManager();
    if (selectCommon == 0) {
        return 0;
    }
    return (int *)((unsigned char *)selectCommon + 0x28168);
}

static int CSelMode_SelectLayoutVariant(void) {
    int regionIndex = GlobalRuntimeContext_SelectRegionVariant(GlobalRuntimeContext_Get());

    if (regionIndex == 1) {
        return 0;
    }
    if (regionIndex == 3) {
        return 1;
    }
    if (regionIndex == 4) {
        return 2;
    }
    if (regionIndex == 2) {
        return 3;
    }
    if (regionIndex == 5) {
        return 4;
    }
    if (regionIndex == 0) {
        return 5;
    }
    return 0;
}

unsigned int HostCzan_GetRegisteredLinkSize(const void *data) {
    int i;

    for (i = 0; i < HOST_CZAN_MAX_LINK_SIZES; i++) {
        if (gHostLinkSizes[i].data == data) {
            return gHostLinkSizes[i].size;
        }
    }

    return 0;
}

static int HostCzan_FindFreeGroup(void) {
    int i;

    for (i = 0; i < HOST_CZAN_MAX_GROUPS; i++) {
        if (!gHostCzanGroups[i].used) {
            return i;
        }
    }

    return -1;
}

static int HostCzan_FindFreeObject(void) {
    int i;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (!gHostCzanObjects[i].used) {
            return i;
        }
    }

    return -1;
}

static int HostCzan_FindObjectIndexByGroupChild(int groupHandle, int childIndex) {
    int i;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == groupHandle &&
            gHostCzanObjects[i].childIndex == childIndex) {
            return i;
        }
    }

    return -1;
}

static void HostCzan_DefaultObjectPlacement(HostCzanObject *object, int groupHandle, int childIndex) {
    (void)groupHandle;
    (void)childIndex;

    object->baseX = 0.0f;
    object->baseY = 0.0f;
    object->x = 0.0f;
    object->y = 0.0f;
}

static void HostCzan_UpdateObjectPosition(HostCzanObject *object) {
    if (object == 0) {
        return;
    }
    object->x = object->localOffsetX + object->positionOffsetX;
    object->y = object->localOffsetY + object->positionOffsetY;
}

static void HostCzan_ProjectCorner(const HostCzanObject *object, float drawX, float drawY,
                                   float lx, float ly, float *outX, float *outY) {
    /* The DOL draws Czan sprites in 3D through a perspective camera 888.9 units in
       front of the 640x480 screen plane (0x80170FA4). Corners are rotated X (opcode
       0x11), then Y (0x12), then Z (0x10), placed at the object's z (opcodes 0x05 +
       0x0C) and projected about the screen centre. Screen y points down, so the
       y-up rotations are written with ly negated. */
    const float degrees = 3.14159265358979323846f / 180.0f;
    const float eyeDistance = 888.9f;
    float ax = object->rotationX * degrees;
    float ay = object->rotationY * degrees;
    float az = object->rotationZ * degrees;
    float y1 = ly * cosf(ax);
    float z1 = -ly * sinf(ax);
    float x2 = lx * cosf(ay) + z1 * sinf(ay);
    float z2 = -lx * sinf(ay) + z1 * cosf(ay);
    float x3 = x2 * cosf(az) - y1 * sinf(az);
    float y3 = x2 * sinf(az) + y1 * cosf(az);
    float z = object->depth + object->localOffsetZ + z2;
    float scale = eyeDistance - z > 1.0f ? eyeDistance / (eyeDistance - z) : eyeDistance;

    *outX = 320.0f + (drawX + x3 - 320.0f) * scale;
    *outY = 240.0f + (drawY + y3 - 240.0f) * scale;
}

static void HostCzan_ResolveLinkedTransform(const HostCzanObject *object, float *x, float *y,
                                            float *scaleX, float *scaleY, float *alpha) {
    /* Sprite +0x1B0 parent chain with +0x1B4 inherit flags: 0x4 = the parent's
       scale (also applied to the child's offset), 0x10 = the parent's alpha, and the
       parent position always. The mode-select floor reflections (anchors with scale
       y -1 and low alpha, link 0x1F) depend on this. */
    int depth;

    *x = object->x;
    *y = object->y;
    *scaleX = 1.0f;
    *scaleY = 1.0f;
    *alpha = 1.0f;
    for (depth = 0; depth < 16 && object->linkedHandle188 != 0; depth++) {
        int referenceGroup = (object->linkedHandle188 >> 16) & 0xffff;
        int referenceChild = (object->linkedHandle188 >> 8) & 0xff;
        int mode = object->linkedHandle188 & 0xff;
        int referenceIndex = HostCzan_FindObjectIndexByGroupChild(referenceGroup, referenceChild);
        const HostCzanObject *reference;

        if (referenceIndex < 0) {
            break;
        }
        reference = &gHostCzanObjects[referenceIndex];
        if ((mode & 0x04) != 0) {
            *x *= reference->scaleX;
            *y *= reference->scaleY;
            *scaleX *= reference->scaleX;
            *scaleY *= reference->scaleY;
        }
        if ((mode & 0x10) != 0) {
            *alpha *= (float)reference->color[3] / 255.0f;
        }
        *x += reference->x;
        *y += reference->y;
        object = reference;
    }
}

static void HostCzan_GetObjectLinkedPosition(const HostCzanObject *object, float *x, float *y) {
    /* Sprite +0x1B0 parent links chain: a reference object can itself be linked
       (prompt indicator -> panel child -> window), so the parent transform is
       resolved through the whole chain, not just one level. */
    int depth;

    *x = object->x;
    *y = object->y;
    for (depth = 0; depth < 16 && object->linkedHandle188 != 0; depth++) {
        int referenceGroup = (object->linkedHandle188 >> 16) & 0xffff;
        int referenceChild = (object->linkedHandle188 >> 8) & 0xff;
        int referenceIndex = HostCzan_FindObjectIndexByGroupChild(referenceGroup, referenceChild);
        if (referenceIndex < 0) {
            break;
        }
        object = &gHostCzanObjects[referenceIndex];
        *x += object->x;
        *y += object->y;
    }
}

static void HostCzan_UpdateObjectScale(HostCzanObject *object) {
    if (object == 0) {
        return;
    }
    object->scaleX = object->baseScaleX + object->animationOffsetX;
    object->scaleY = object->baseScaleY + object->animationOffsetY;
    object->scaleZ = object->baseScaleZ;
}

static int HostCzan_ShouldSkipObject(const HostCzanObject *object);
static int HostCzan_GetObjectReferenceEdge(const HostCzanObject *object);
static unsigned short ReadBe16(const unsigned char *p);
static unsigned int ReadBe32(const unsigned char *p);
static unsigned int HostCzan_GetAnimationCommandOffset(const HostCzanObject *object, int animationIndex);
static void HostCzan_RunAnimationScript(HostCzanObject *object, int allowUnknownOpcode);
static double HostCzan_GetObjectAnimationDuration(const HostCzanObject *object, int animationIndex, double fallbackDuration);
static int HostCzan_FindObjectIndexByGroupChild(int groupHandle, int childIndex);

static void HostCzan_SetDefaultSpriteCenter(HostCzanObject *object) {
    if (object == 0) {
        return;
    }

    object->baseX = (float)object->width * 0.5f;
    object->baseY = (float)object->height * 0.5f;
    HostCzan_UpdateObjectPosition(object);
}

static void HostCzan_SetDefaultSpriteFullSizeBase(HostCzanObject *object) {
    if (object == 0) {
        return;
    }

    object->baseX = (float)object->width;
    object->baseY = (float)object->height;
    HostCzan_UpdateObjectPosition(object);
}

static unsigned char HostCzan_GetCurrentAnimationFlags2(const HostCzanObject *object) {
    const unsigned char *descriptor;
    unsigned int animationCount;
    unsigned int animationTableOffset;
    const unsigned char *entry;

    if (object == 0 ||
        object->metadata == 0 ||
        object->currentAnimation < 0 ||
        object->descriptorOffset + 0x20u > object->metadataSize) {
        return 0;
    }

    descriptor = object->metadata + object->descriptorOffset;
    animationCount = ReadBe16(descriptor + 0x16);
    animationTableOffset = ReadBe32(descriptor + 0x1C);
    if ((unsigned int)object->currentAnimation >= animationCount ||
        animationTableOffset + (unsigned int)(object->currentAnimation + 1) * 0x10u > object->metadataSize) {
        return 0;
    }

    entry = object->metadata + animationTableOffset + (unsigned int)object->currentAnimation * 0x10u;
    return entry[2];
}

static void HostCzan_ResetAnimationToEntry(HostCzanObject *object) {
    if (object == 0) {
        return;
    }

    object->animationCommandOffset = HostCzan_GetAnimationCommandOffset(object, object->currentAnimation);
    object->animationWaitTicks = 0;
    object->animationDoneB1 = 1;
    object->animationDoneB2 = 0;
    object->animationStartFrame = 0.0f;
}

static void HostCzan_PreplayInitialAnimation(HostCzanObject *object) {
    int oldMode;
    int frame;
    int frameCount;

    if (object == 0) {
        return;
    }

    oldMode = object->animationMode175;
    object->animationMode175 = 3;
    object->animationCommandOffset = HostCzan_GetAnimationCommandOffset(object, object->currentAnimation);
    object->animationWaitTicks = 0;
    object->animationDoneB1 = 0;
    object->animationDoneB2 = 0;

    frameCount = (int)HostCzan_GetObjectAnimationDuration(object, object->currentAnimation, 0.0);
    if (frameCount < 0) {
        frameCount = 0;
    }
    for (frame = 0; frame < frameCount; frame++) {
        HostCzan_RunAnimationScript(object, 1);
    }

    object->animationMode175 = oldMode;
    HostCzan_ResetAnimationToEntry(object);
}

static void HostCzan_ApplyAnimationValue(HostCzanObject *object, int opcode, float value) {
    if (object == 0) {
        return;
    }

    switch (opcode) {
        case 0x01:
            object->referenceEdge160 = (int)value;
            break;
        case 0x04:
            /* Opcode 04 writes the animation frame +0xA8; it is only shown while
               +0x148 is -2 (auto), see 0x80175448. */
            if (!object->textureFrameSet148) {
                object->textureIndex = (int)value;
            }
            break;
        case 0x05:
            object->depth = value;
            break;
        case 0x06:
            if ((HostCzan_GetCurrentAnimationFlags2(object) & 2u) != 0) {
                value = (float)object->width * 0.5f;
            }
            object->baseX = value;
            HostCzan_UpdateObjectPosition(object);
            break;
        case 0x07:
            if ((HostCzan_GetCurrentAnimationFlags2(object) & 2u) != 0) {
                value = (float)object->height * 0.5f;
            }
            object->baseY = value;
            HostCzan_UpdateObjectPosition(object);
            break;
        case 0x08:
            object->localOffsetX = value;
            HostCzan_UpdateObjectPosition(object);
            break;
        case 0x09:
            object->localOffsetY = value;
            HostCzan_UpdateObjectPosition(object);
            break;
        case 0x0C:
            object->localOffsetZ = value;
            break;
        case 0x0D:
            object->baseScaleZ = value;
            HostCzan_UpdateObjectScale(object);
            break;
        case 0x0E:
            object->baseScaleX = value;
            HostCzan_UpdateObjectScale(object);
            break;
        case 0x0F:
            object->baseScaleY = value;
            HostCzan_UpdateObjectScale(object);
            break;
        case 0x10:
            object->rotationZ = value;
            break;
        case 0x11:
            object->rotationX = value;
            break;
        case 0x12:
            object->rotationY = value;
            break;
        case 0x18:
            object->color[3] = (unsigned char)value;
            break;
        case 0x36:
            if (object->width != 0) {
                object->uvOffsetX = (((float)object->width * 0.5f) - value) / (float)object->width;
            }
            break;
        case 0x37:
            if (object->height != 0) {
                object->uvOffsetY = (((float)object->height * 0.5f) - value) / (float)object->height;
            }
            break;
        case 0x39:
            object->cornerOffsetTopLeftX = value;
            break;
        case 0x3A:
            object->cornerOffsetTopLeftY = value;
            break;
        case 0x3B:
            object->cornerOffsetTopRightX = value - (float)object->width;
            break;
        case 0x3C:
            object->cornerOffsetTopRightY = value;
            break;
        case 0x3D:
            object->cornerOffsetBottomRightX = value;
            break;
        case 0x3E:
            object->cornerOffsetBottomRightY = value - (float)object->height;
            break;
        case 0x3F:
            object->cornerOffsetBottomLeftX = value - (float)object->width;
            break;
        case 0x40:
            object->cornerOffsetBottomLeftY = value - (float)object->height;
            break;
        case 0x25:
        case 0x28:
        case 0x2B:
        case 0x2E:
        case 0x31:
            object->color[0] = (unsigned char)value;
            break;
        case 0x26:
        case 0x29:
        case 0x2C:
        case 0x2F:
        case 0x32:
            object->color[1] = (unsigned char)value;
            break;
        case 0x27:
        case 0x2A:
        case 0x2D:
        case 0x30:
        case 0x33:
            object->color[2] = (unsigned char)value;
            break;
        case 0x41:
            object->width = (int)value;
            break;
        case 0x42:
            object->height = (int)value;
            break;
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
            /* The retail interpreter consumes these CAE values but does not
               publish a visible sprite-field change for them. */
            break;
        default:
            break;
    }
}

static int HostCzan_GetMaskClipRect(const HostCzanObject *object, float *x, float *y, float *w, float *h) {
    /* Host stand-in for the Czan alpha masks: the mode-select silhouettes
       ('cha_sil*') are masked by hidden 'white_window_al' siblings shaped like the
       button window, so clip them to the window (child 0) rectangle. */
    int index;
    const HostCzanObject *window;
    float wx;
    float wy;
    float sx;
    float sy;
    float alpha;

    if (strncmp(object->name, "cha_sil", 7) != 0) {
        return 0;
    }
    index = HostCzan_FindObjectIndexByGroupChild(object->groupHandle, 0);
    if (index < 0) {
        return 0;
    }
    window = &gHostCzanObjects[index];
    HostCzan_ResolveLinkedTransform(window, &wx, &wy, &sx, &sy, &alpha);
    sx *= window->scaleX;
    sy *= window->scaleY;
    *x = wx - window->baseX * sx;
    *y = wy - window->baseY * sy;
    *w = (float)window->width * sx;
    *h = (float)window->height * sy;
    if (*w < 0.0f) {
        *x += *w;
        *w = -*w;
    }
    if (*h < 0.0f) {
        *y += *h;
        *h = -*h;
    }
    return 1;
}

static void HostCzan_DrawObjectUnclipped(HostCzanObject *object) {
    RenderQuad quad;
    unsigned int dummyColor;
    float width;
    float height;
    float visibleWidth;
    float visibleHeight;
    float drawX;
    float drawY;
    int drawWidth;
    int drawHeight;
    int useCustomQuad;
    int useRotationZ;
    unsigned char drawColor[4];
    HostCzanObject linkedView;
    int channel;

    if (object == 0 ||
        HostCzan_ShouldSkipObject(object) ||
        !object->used ||
        !object->drawEnabled ||
        object->suppressDraw173 != 0 ||
        object->width <= 0 ||
        object->height <= 0) {
        return;
    }
    if (object->linkedHandle188 != 0) {
        float inheritedScaleX;
        float inheritedScaleY;
        float inheritedAlpha;

        linkedView = *object;
        HostCzan_ResolveLinkedTransform(object, &linkedView.x, &linkedView.y,
                                        &inheritedScaleX, &inheritedScaleY, &inheritedAlpha);
        linkedView.scaleX *= inheritedScaleX;
        linkedView.scaleY *= inheritedScaleY;
        linkedView.color[3] = (unsigned char)((float)linkedView.color[3] * inheritedAlpha + 0.5f);
        linkedView.linkedHandle188 = 0;
        object = &linkedView;
    }
    for (channel = 0; channel < 4; channel++) {
        drawColor[channel] = (unsigned char)((object->color[channel] * object->blockColor[channel] + 127) / 255);
    }

    width = (float)object->width * object->scaleX;
    height = (float)object->height * object->scaleY;
    visibleWidth = width < 0.0f ? -width : width;
    visibleHeight = height < 0.0f ? -height : height;
    if (visibleWidth <= 0.0f || visibleHeight <= 0.0f) {
        return;
    }
    drawWidth = (int)(width + (width < 0.0f ? -0.5f : 0.5f));
    drawHeight = (int)(height + (height < 0.0f ? -0.5f : 0.5f));
    if (drawWidth == 0 || drawHeight == 0) {
        return;
    }

    HostCzan_GetObjectLinkedPosition(object, &drawX, &drawY);

    quad.x = drawX - object->baseX * object->scaleX;
    quad.y = drawY - object->baseY * object->scaleY;
    quad.z = object->depth + object->localOffsetZ;
    quad.width = width;
    quad.height = height;
    useRotationZ = object->rotationZ != 0.0f;

    if (object->textureSlot >= 0) {
        useCustomQuad =
            useRotationZ ||
            object->rotationX != 0.0f ||
            object->rotationY != 0.0f ||
            object->depth + object->localOffsetZ > 0.5f ||
            object->depth + object->localOffsetZ < -0.5f ||
            object->uvBaseX != 0.0f ||
            object->uvBaseY != 0.0f ||
            object->uvOffsetX != 0.0f ||
            object->uvOffsetY != 0.0f ||
            object->uvSpanX != 1.0f ||
            object->uvSpanY != 1.0f ||
            object->cornerOffsetTopLeftX != 0.0f ||
            object->cornerOffsetTopLeftY != 0.0f ||
            object->cornerOffsetTopRightX != 0.0f ||
            object->cornerOffsetTopRightY != 0.0f ||
            object->cornerOffsetBottomRightX != 0.0f ||
            object->cornerOffsetBottomRightY != 0.0f ||
            object->cornerOffsetBottomLeftX != 0.0f ||
            object->cornerOffsetBottomLeftY != 0.0f;
        if (useCustomQuad) {
            float lx0 = -object->baseX * object->scaleX + object->cornerOffsetTopLeftX * object->scaleX;
            float ly0 = -object->baseY * object->scaleY + object->cornerOffsetTopLeftY * object->scaleY;
            float lx1 = ((float)object->width - object->baseX) * object->scaleX +
                        object->cornerOffsetTopRightX * object->scaleX;
            float ly1 = -object->baseY * object->scaleY + object->cornerOffsetTopRightY * object->scaleY;
            float lx2 = ((float)object->width - object->baseX) * object->scaleX +
                        object->cornerOffsetBottomRightX * object->scaleX;
            float ly2 = ((float)object->height - object->baseY) * object->scaleY +
                        object->cornerOffsetBottomRightY * object->scaleY;
            float lx3 = -object->baseX * object->scaleX + object->cornerOffsetBottomLeftX * object->scaleX;
            float ly3 = ((float)object->height - object->baseY) * object->scaleY +
                        object->cornerOffsetBottomLeftY * object->scaleY;
            float x0, y0, x1, y1, x2, y2, x3, y3;
            float u0 = object->uvBaseX + object->uvOffsetX;
            float v0 = object->uvBaseY + object->uvOffsetY;
            float u1 = u0 + object->uvSpanX;
            float v1 = v0 + object->uvSpanY;
            unsigned int packedColor;

            HostCzan_ProjectCorner(object, drawX, drawY, lx0, ly0, &x0, &y0);
            HostCzan_ProjectCorner(object, drawX, drawY, lx1, ly1, &x1, &y1);
            HostCzan_ProjectCorner(object, drawX, drawY, lx2, ly2, &x2, &y2);
            HostCzan_ProjectCorner(object, drawX, drawY, lx3, ly3, &x3, &y3);
            packedColor =
                ((unsigned int)drawColor[0] << 24) |
                ((unsigned int)drawColor[1] << 16) |
                ((unsigned int)drawColor[2] << 8) |
                (unsigned int)drawColor[3];

            DrawTexturedTriangle2D(
                (int)(x0 + 0.5f), (int)(y0 + 0.5f), u0, v0,
                (int)(x1 + 0.5f), (int)(y1 + 0.5f), u1, v0,
                (int)(x2 + 0.5f), (int)(y2 + 0.5f), u1, v1,
                (void *)(long)object->textureSlot,
                object->textureIndex,
                &packedColor);
            DrawTexturedTriangle2D(
                (int)(x0 + 0.5f), (int)(y0 + 0.5f), u0, v0,
                (int)(x2 + 0.5f), (int)(y2 + 0.5f), u1, v1,
                (int)(x3 + 0.5f), (int)(y3 + 0.5f), u0, v1,
                (void *)(long)object->textureSlot,
                object->textureIndex,
                &packedColor);
        }
        else {
            DrawTexturedQuad(
                &quad,
                drawWidth,
                drawHeight,
                drawColor,
                (void *)(long)object->textureSlot,
                object->textureIndex);
        }
    }
    else {
        dummyColor = 0xFFFFFF80u;
        DrawFilledRect((int)quad.x, (int)quad.y, 0, drawWidth, drawHeight, &dummyColor, 0);
    }

}

static void HostCzan_DrawObject(HostCzanObject *object) {
    float clipX;
    float clipY;
    float clipW;
    float clipH;

    if (object != 0 && HostCzan_GetMaskClipRect(object, &clipX, &clipY, &clipW, &clipH)) {
        Platform_SetLogicalScissor(1, clipX, clipY, clipW, clipH);
        HostCzan_DrawObjectUnclipped(object);
        Platform_SetLogicalScissor(0, 0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    HostCzan_DrawObjectUnclipped(object);
}

static int HostCzan_IsWindowLikeObject(const HostCzanObject *object) {
    if (object == 0) {
        return 0;
    }

    return strncmp(object->name, "white_window", 12) == 0 ||
           object->width > 360 ||
           object->height > 280;
}

static int HostCzan_ShouldSkipObject(const HostCzanObject *object) {
    if (object == 0) {
        return 1;
    }

    return strncmp(object->name, "@dummy", 6) == 0;
}

static int HostCzan_IsObjectDrawableForRetailList(const HostCzanObject *object) {
    double duration;

    if (object == 0 ||
        HostCzan_ShouldSkipObject(object) ||
        !object->used ||
        !object->drawEnabled ||
        object->suppressDraw173 != 0 ||
        object->width <= 0 ||
        object->height <= 0 ||
        object->color[3] == 0 ||
        object->currentAnimation < 0 ||
        object->presented0b0 == 0) {
        return 0;
    }

    /* FUN_801745B8 appends objects whose selected CAE animation has a nonzero
       duration. Some host-decoded CAE descriptors still expose a zero duration
       while keeping a valid command stream, so keep those objects in the retail
       list instead of collapsing a phase down to one surviving sprite. */
    duration = HostCzan_GetObjectAnimationDuration(object, object->currentAnimation, 0.0);
    if (duration == 0.0 && object->animationCommandOffset == 0) {
        return 0;
    }

    if (object->textureSlot < 0 && object->descriptorType != 2) {
        return 0;
    }

    return 1;
}

static int HostCzan_CompareObjectDrawOrder(int leftIndex, int rightIndex) {
    const HostCzanObject *left = &gHostCzanObjects[leftIndex];
    const HostCzanObject *right = &gHostCzanObjects[rightIndex];

    int leftKey = HostCzan_GetObjectReferenceEdge(left);
    int rightKey = HostCzan_GetObjectReferenceEdge(right);

    if (leftKey != rightKey) {
        return leftKey < rightKey ? -1 : 1;
    }
    if (left->depth != right->depth) {
        return left->depth < right->depth ? -1 : 1;
    }
    if (left->groupHandle != right->groupHandle) {
        return left->groupHandle < right->groupHandle ? -1 : 1;
    }
    return left->childIndex - right->childIndex;
}

static void HostCzan_SortObjectIndexesForRetailList(int *objectIndexes, int objectCount) {
    int i;

    for (i = 1; i < objectCount; i++) {
        int key = objectIndexes[i];
        int j = i - 1;
        while (j >= 0 && HostCzan_CompareObjectDrawOrder(objectIndexes[j], key) > 0) {
            objectIndexes[j + 1] = objectIndexes[j];
            j--;
        }
        objectIndexes[j + 1] = key;
    }
}

static unsigned short ReadBe16(const unsigned char *p) {
    return (unsigned short)(((unsigned int)p[0] << 8) | (unsigned int)p[1]);
}

static unsigned int ReadBe32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) |
           (unsigned int)p[3];
}

static float ReadBeFloat(const unsigned char *p) {
    union {
        unsigned int u;
        float f;
    } value;

    value.u = ReadBe32(p);
    return value.f;
}

static int HostCzan_CommandValueCount(int opcode) {
    switch (opcode) {
        case 0x01:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x15:
        case 0x16:
        case 0x18:
        case 0x1A:
        case 0x1E:
        case 0x1F:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2A:
        case 0x2B:
        case 0x2C:
        case 0x2D:
        case 0x2E:
        case 0x2F:
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x36:
        case 0x37:
        case 0x39:
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
            return 1;
        case 0x1B:
            return 2;
        case 0x1C:
        case 0x1D:
            return 3;
        default:
            return 0;
    }
}

static void HostCzan_ApplyInitialAnimation(
    HostCzanObject *object,
    const unsigned char *metadata,
    unsigned int metadataSize,
    const unsigned char *descriptor,
    int animationIndex) {
    unsigned int animationCount;
    unsigned int animationTableOffset;
    const unsigned char *entry;
    unsigned int commandOffset;
    unsigned int offset;
    int guard;

    if (object == 0 || metadata == 0 || descriptor == 0 || animationIndex < 0) {
        return;
    }

    animationCount = ReadBe16(descriptor + 0x16);
    animationTableOffset = ReadBe32(descriptor + 0x1C);
    if ((unsigned int)animationIndex >= animationCount ||
        animationTableOffset >= metadataSize ||
        animationTableOffset + (unsigned int)(animationIndex + 1) * 0x10u > metadataSize) {
        return;
    }

    entry = metadata + animationTableOffset + (unsigned int)animationIndex * 0x10u;
    commandOffset = ReadBe32(entry + 0x0C);
    if (ReadBe32(entry + 0x08) == 0 || commandOffset >= metadataSize) {
        return;
    }

    offset = commandOffset;
    for (guard = 0; guard < 256 && offset + 4 <= metadataSize; guard++) {
        int opcode = (int)ReadBeFloat(metadata + offset);
        int valueCount;
        float value;

        offset += 4;
        if (opcode == 0) {
            break;
        }

        valueCount = HostCzan_CommandValueCount(opcode);
        if (offset + (unsigned int)valueCount * 4u > metadataSize) {
            break;
        }

        value = valueCount > 0 ? ReadBeFloat(metadata + offset) : 0.0f;
        HostCzan_ApplyAnimationValue(object, opcode, value);

        offset += (unsigned int)valueCount * 4u;
    }

    HostCzan_UpdateObjectPosition(object);
}

static unsigned int HostCzan_GetAnimationCommandOffset(const HostCzanObject *object, int animationIndex) {
    const unsigned char *descriptor;
    unsigned int animationCount;
    unsigned int animationTableOffset;
    const unsigned char *entry;

    if (object == 0 ||
        object->metadata == 0 ||
        animationIndex < 0 ||
        object->descriptorOffset + 0x20u > object->metadataSize) {
        return 0;
    }

    descriptor = object->metadata + object->descriptorOffset;
    animationCount = ReadBe16(descriptor + 0x16);
    animationTableOffset = ReadBe32(descriptor + 0x1C);
    if ((unsigned int)animationIndex >= animationCount ||
        animationTableOffset + (unsigned int)(animationIndex + 1) * 0x10u > object->metadataSize) {
        return 0;
    }

    entry = object->metadata + animationTableOffset + (unsigned int)animationIndex * 0x10u;
    if (ReadBe32(entry + 0x08) == 0) {
        return 0;
    }
    return ReadBe32(entry + 0x0C);
}

static float HostCzan_GetAnimationTickStep(const HostCzanObject *object) {
    /* 0x801717C8..0x80171828: objects have +0x180 = 1 (set by the constructor
       0x801713B4), so the per-frame step is the animation entry's rate (+0x04,
       30 for the UI CAE files) / (refresh 60 / (frame skip + 1)). */
    const unsigned char *descriptor;
    unsigned int animationTableOffset;
    unsigned int rate;

    if (object == 0 || object->metadata == 0 || object->currentAnimation < 0 ||
        object->descriptorOffset + 0x20u > object->metadataSize) {
        return 1.0f;
    }
    descriptor = object->metadata + object->descriptorOffset;
    animationTableOffset = ReadBe32(descriptor + 0x1C) + (unsigned int)object->currentAnimation * 0x10u;
    if (animationTableOffset + 0x10u > object->metadataSize) {
        return 1.0f;
    }
    rate = ReadBe32(object->metadata + animationTableOffset + 0x04);
    return rate != 0 ? (float)rate / 60.0f : 1.0f;
}

static void HostCzan_StartAnimationState(HostCzanObject *object, int animationIndex) {
    /* 0x80172CC8 (frame 0): clear presented/done, select the animation and mark it
       playing only when it has frames (+0x171). */
    object->presented0b0 = 0;
    object->animationDoneB1 = 0;
    object->animationDoneB2 = 0;
    object->currentAnimation = animationIndex;
    object->animationCommandOffset = HostCzan_GetAnimationCommandOffset(object, animationIndex);
    object->animationWaitTicks = 0;
    object->animationClockB4 = 0.0f;
    object->animationPlaying171 =
        HostCzan_GetObjectAnimationDuration(object, animationIndex, 0.0) != 0.0 ||
        object->animationCommandOffset != 0;
}

static void HostCzan_ApplyCreateFlags(HostCzanObject *object, unsigned int flags, int animationIndex,
                                      unsigned int descriptorFlags) {
    /* 0x80173414: creation flag switch, then per-descriptor flag 0x04 (start anim 0).
       The object constructor (~0x80171290) leaves +0xB0 = 0, +0xB1 = 1 (done) and
       +0xB2 = 0, so a never-started group reads as idle/finished. */
    object->animationPlaying171 = 0;
    object->presented0b0 = 0;
    object->animationDoneB1 = 1;
    object->animationDoneB2 = 0;
    switch (flags) {
        case 1:
        case 2:
            HostCzan_StartAnimationState(object, animationIndex);
            HostCzan_RunAnimationScript(object, 1);
            object->animationCommandOffset = HostCzan_GetAnimationCommandOffset(object, animationIndex);
            object->animationWaitTicks = 0;
            object->animationPlaying171 = 0;
            object->animationDoneB1 = 1;
            object->animationDoneB2 = 0;
            if (flags == 2) {
                object->presented0b0 = 0;
            }
            break;
        case 3:
        case 4:
            HostCzan_PreplayInitialAnimation(object);
            if (flags == 4) {
                object->presented0b0 = 0;
            }
            break;
        default:
            break;
    }
    if ((descriptorFlags & 0x004u) != 0) {
        HostCzan_StartAnimationState(object, 0);
    }
}

static void HostCzan_RunAnimationScript(HostCzanObject *object, int allowUnknownOpcode) {
    /* 0x80171B98: one animation tick. Execute commands until a wait is pending or the
       frame is ended, then count the wait down once. Wait opcodes decide visibility:
         0x15 N  wait N frames, presented (+0xB0 = 1), end the frame
         0x16 N  wait N frames, NOT presented (+0xB0 = 0)
       so an object is only drawn once its script reaches a 0x15 wait (dummy anchors
       only ever use 0x16 and are never drawn). End opcode 0 (0x80171C00): reset mode
       +0x174 == 0 finishes (hides the object when +0x175 == 1), 1 restarts in the same
       tick, other values restart and end the tick. */
    int guard;
    int endFrame = 0;

    if (object == 0 ||
        object->metadata == 0 ||
        object->currentAnimation < 0 ||
        object->animationCommandOffset == 0 ||
        object->animationCommandOffset >= object->metadataSize) {
        return;
    }

    for (guard = 0;
         guard < 256 && !endFrame && object->animationWaitTicks == 0 &&
         object->animationCommandOffset != 0 &&
         object->animationCommandOffset + 4 <= object->metadataSize;
         guard++) {
        unsigned int offset = object->animationCommandOffset;
        int opcode = (int)ReadBeFloat(object->metadata + offset);
        int valueCount;
        float value;

        offset += 4;
        if (opcode == 0) {
            if (object->animationReset174 == 0) {
                if (object->animationMode175 == 1) {
                    object->suppressDraw173 = 1;
                }
                object->animationDoneB1 = 1;
                object->animationCommandOffset = 0;
                return;
            }
            object->animationCommandOffset =
                HostCzan_GetAnimationCommandOffset(object, object->currentAnimation);
            object->animationDoneB2 = 1;
            if (object->animationReset174 != 1) {
                endFrame = 1;
            }
            continue;
        }

        valueCount = HostCzan_CommandValueCount(opcode);
        if (offset + (unsigned int)valueCount * 4u > object->metadataSize) {
            object->animationCommandOffset = 0;
            return;
        }

        value = valueCount > 0 ? ReadBeFloat(object->metadata + offset) : 0.0f;
        switch (opcode) {
            case 0x15:
                object->animationWaitTicks = (int)value;
                object->presented0b0 = 1;
                endFrame = 1;
                break;
            case 0x16:
                object->animationWaitTicks = (int)value;
                object->presented0b0 = 0;
                break;
            case 0x1E:
                object->spriteRenderMode = (int)value;
                break;
            default:
                HostCzan_ApplyAnimationValue(object, opcode, value);
                if (!allowUnknownOpcode && HostCzan_CommandValueCount(opcode) == 0) {
                    object->animationCommandOffset = 0;
                    return;
                }
                break;
        }

        offset += (unsigned int)valueCount * 4u;
        object->animationCommandOffset = offset;
    }

    if (object->animationDoneB1 == 0 && object->animationWaitTicks > 0) {
        object->animationWaitTicks--;
    }
}

static double HostCzan_GetObjectAnimationDuration(const HostCzanObject *object, int animationIndex, double fallbackDuration) {
    const unsigned char *descriptor;
    unsigned int animationCount;
    unsigned int animationTableOffset;
    const unsigned char *entry;

    if (object == 0 || object->metadata == 0) {
        return fallbackDuration;
    }
    if (animationIndex < 0) {
        animationIndex = object->currentAnimation;
    }
    if (animationIndex < 0 ||
        object->descriptorOffset + 0x20u > object->metadataSize) {
        return fallbackDuration;
    }

    descriptor = object->metadata + object->descriptorOffset;
    animationCount = ReadBe16(descriptor + 0x16);
    animationTableOffset = ReadBe32(descriptor + 0x1C);
    if ((unsigned int)animationIndex >= animationCount ||
        animationTableOffset + (unsigned int)(animationIndex + 1) * 0x10u > object->metadataSize) {
        return fallbackDuration;
    }

    entry = object->metadata + animationTableOffset + (unsigned int)animationIndex * 0x10u;
    return (double)(float)ReadBe32(entry + 0x08);
}

static void CSelMode_LogCaeDescriptors(const CzanLinkBlock *objectBlock, unsigned int blockIndex) {
    CzanLinkBlock nestedBlock;
    CzanLinkBlock caeBlock;
    unsigned int descriptorCount;
    unsigned int descriptorOffset;
    unsigned int i;

    if (!CzanLinkResource_GetBlock(objectBlock->data, objectBlock->size, 1, &caeBlock)) {
        return;
    }

    if (caeBlock.size < 0x10 ||
        caeBlock.data[0] != 'C' ||
        caeBlock.data[1] != 'A' ||
        caeBlock.data[2] != 'E' ||
        caeBlock.data[3] != '_') {
        return;
    }

    descriptorCount = ReadBe16(caeBlock.data + 8);
    descriptorOffset = ReadBe32(caeBlock.data + 0x0C);
    if (descriptorOffset >= caeBlock.size) {
        return;
    }

    printf("CSelMode: block %u CAE descriptors=%u\n", blockIndex, descriptorCount);
    for (i = 0; i < descriptorCount && i < 8; i++) {
        const unsigned char *descriptor = caeBlock.data + descriptorOffset + i * 0x20;
        if ((unsigned int)(descriptor - caeBlock.data) + 0x20 > caeBlock.size) {
            break;
        }

        printf("CSelMode:   desc %u name=%.16s tex=%u type=%u animOff=0x%X\n",
               i,
               descriptor,
               ReadBe16(descriptor + 0x10),
               (unsigned int)descriptor[0x14],
               ReadBe32(descriptor + 0x1C));
    }

    if (CzanLinkResource_GetBlock(objectBlock->data, objectBlock->size, 0, &nestedBlock) &&
        CzanLinkResource_IsValid(nestedBlock.data, nestedBlock.size)) {
        printf("CSelMode: block %u texture WII blockCount=%u\n",
               blockIndex,
               CzanLinkResource_GetBlockCount(nestedBlock.data, nestedBlock.size));
    }
}

/* 0x80272358, dumped from main.dol (SHA-256 6cdc2392...). */
const CSelModeChoice CSelMode_ChoiceTable[5] = {
    { 0x02, 0x01, 0x01, 0x00 },
    { 0x02, 0x02, 0x07, 0x00 },
    { 0x1B, 0x03, 0x05, 0x00 },
    { 0x15, 0x06, 0x00, 0x00 },
    { 0x0C, 0x01, 0x00, 0x00 },
};

/* 0x80272348: CSelMode navigation table, 4 bytes per selected index:
   [0] right (mask 8), [1] left (mask 4), [2] up (mask 1), [3] down (mask 2).
   CSelMode_Update (0x8006EAC8) indexes it as table[selected * 4 + dir] with no
   bounds check, so index 4 reads the first word of the choice table that follows
   in .data (00 00 00 02). Those 4 bytes are included to match that behavior. */
static const signed char kCSelModeNavTable[5][4] = {
    { 2, 2, 3, 1 },
    { 3, 3, 0, 2 },
    { 0, 0, 1, 3 },
    { 1, 1, 2, 0 },
    { 0, 0, 0, 2 },
};

int CSelMode_NavigateSelection(int selectedModeIndex, int leftPressed, int rightPressed,
                               int upPressed, int downPressed) {
    if (selectedModeIndex < 0 || selectedModeIndex > 4) {
        return selectedModeIndex;
    }
    /* Neighbour table 0x80272348 is [up(8), down(4), left(1), right(2)]. Priority
       order matches 0x8006ED34..0x8006EDC8: left, right, up, down. */
    if (leftPressed) {
        return kCSelModeNavTable[selectedModeIndex][2];
    }
    if (rightPressed) {
        return kCSelModeNavTable[selectedModeIndex][3];
    }
    if (upPressed) {
        return kCSelModeNavTable[selectedModeIndex][0];
    }
    if (downPressed) {
        return kCSelModeNavTable[selectedModeIndex][1];
    }
    return selectedModeIndex;
}

int CSelMode_ModeIdToSelectedIndex(int modeId) {
    switch (modeId) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 3:
            return 2;
        case 6:
            return 3;
        default:
            return 0;
    }
}

const CSelModeChoice *CSelMode_GetChoice(int selectedModeIndex) {
    if (selectedModeIndex < 0 || selectedModeIndex >= 5) {
        return 0;
    }
    return &CSelMode_ChoiceTable[selectedModeIndex];
}

int CSelMode_MoveSelection(int selectedModeIndex, int direction) {
    /* Console host helper: right/left go through the original navigation table. */
    return CSelMode_NavigateSelection(selectedModeIndex, direction < 0, direction > 0, 0, 0);
}

int CSelMode_Init(void *cselMode) {
    unsigned char *bytes = (unsigned char *)cselMode;
    int i;

    /* Original initializes a 14-entry controller at +0x160.
       Each entry is 0x50 bytes and uses callbacks at 0x8006309C/0x800630D8. */
    if (bytes != 0) {
        ClearMemory(bytes, 0, 0x5c0);
        for (i = 0; i < CSEL_MODE_ENTRY_COUNT; i++) {
            CSelModeEntry_Init(bytes + 0x160 + i * CSEL_MODE_ENTRY_SIZE);
        }
    }
    return 0;
}

void CSelMode_OnEnter(void *cselMode, void *linkData) {
    unsigned char *bytes = (unsigned char *)cselMode;
    unsigned int blockCount;
    unsigned int linkSize;
    CzanLinkBlock block;
    int sharedModeButtonGroup;
    int entryIndex;
    int layoutVariant;
    int referenceGroupHandle;

    /* Original links the Czan resource, builds Czan UI object groups from blocks
       0..6, reuses one shared mode-button object group across entries 7..13,
       applies region-specific position/animation tables to entries 6..13, and
       sets modeState at +0x130 to 1. */
    blockCount = CzanLinkResource_GetBlockCount(linkData, gCSelModeHostLinkResourceSize);
    if (blockCount == 0) {
        if (CSelMode_IsVerbose()) {
            puts("CSelMode: linkData is not a valid WII resource");
        }
        return;
    }

    linkSize = gCSelModeHostLinkResourceSize;
    if (linkSize == 0) {
        linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    }
    if (CSelMode_IsVerbose()) {
        printf("CSelMode: WII link blockCount=%u\n", blockCount);
    }

    if (bytes == 0) {
        return;
    }

    for (entryIndex = 0; entryIndex < CSEL_MODE_ENTRY_COUNT; entryIndex++) {
        CSelModeEntry_Init(bytes + 0x160 + entryIndex * CSEL_MODE_ENTRY_SIZE);
    }
    gHostCSelModeActiveEntries = (CSelModeEntryKnownFields *)(void *)(bytes + 0x160);
    gHostCSelModeActiveMode = bytes;

    if (CzanLinkResource_GetBlock(linkData, linkSize, 0, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(bytes + 0x160, (void *)block.data);
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 2, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(bytes + 0x1b0, (void *)block.data);
    }
    sharedModeButtonGroup = -1;
    if (CzanLinkResource_GetBlock(linkData, linkSize, 1, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        sharedModeButtonGroup = CSelModeEntry_AddUiObject(bytes + 0x340, (void *)block.data);
    }
    for (entryIndex = 1; entryIndex < 4; entryIndex++) {
        CSelModeEntry_AddChildUiObject(bytes + 0x160 + (entryIndex + 6) * CSEL_MODE_ENTRY_SIZE, sharedModeButtonGroup);
    }
    for (entryIndex = 0; entryIndex < 4; entryIndex++) {
        CSelModeEntry_AddChildUiObject(bytes + 0x160 + (entryIndex + 10) * CSEL_MODE_ENTRY_SIZE, sharedModeButtonGroup);
    }

    if (CzanLinkResource_GetBlock(linkData, linkSize, 3, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(bytes + 0x200, (void *)block.data);
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 4, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(bytes + 0x250, (void *)block.data);
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 5, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(bytes + 0x2a0, (void *)block.data);
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 6, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(bytes + 0x2f0, (void *)block.data);
    }

    layoutVariant = CSelMode_SelectLayoutVariant();
    for (entryIndex = 0; entryIndex < 4; entryIndex++) {
        unsigned char *entryA = bytes + 0x160 + (entryIndex + 6) * CSEL_MODE_ENTRY_SIZE;
        unsigned char *entryB = bytes + 0x160 + (entryIndex + 10) * CSEL_MODE_ENTRY_SIZE;
        float *pos = &CSelMode_PositionTableHost[layoutVariant][entryIndex][0];
        float *anim = &CSelMode_AnimationTableHost[layoutVariant][entryIndex][0];

        CSelModeEntry_SetPositionOrLayout(entryA, 0, 6, pos);
        CSelModeEntry_SetAnimationOrLayout(entryA, 0, 6, anim);
        CSelModeEntry_SetPositionOrLayout(entryB, 0, 6, pos);
        CSelModeEntry_SetAnimationOrLayout(entryB, 0, 6, anim);
    }

    referenceGroupHandle = gHostCSelModeActiveEntries[0].objectHandles[0];
    for (entryIndex = 0; entryIndex < CSEL_MODE_ENTRY_COUNT; entryIndex++) {
        int referenceChild = CSelMode_SoundOrTextIdTableHost[entryIndex];
        if (referenceChild != -1 &&
            gHostCSelModeActiveEntries[entryIndex].objectHandleCount > 0 &&
            gHostCSelModeActiveEntries[entryIndex].objectHandles[0] >= 0 &&
            referenceGroupHandle >= 0) {
            CzanUiManager_LinkObjectGroupToReferenceObject(
                0,
                gHostCSelModeActiveEntries[entryIndex].objectHandles[0],
                referenceGroupHandle,
                referenceChild,
                0x1f);
        }
    }

    /* 0x8006E718..0x8006E98C */
    {
        int *pointerManager = GameMain_GetPointerManager();
        int *parentSelectData = (int *)RuntimeHostPointerFromBits(*(int *)(void *)(bytes + 0x10));
        int childIndex;

        /* 0x8006F2A0: parent game mode 1/2/3/6 -> selection 0..3. */
        *(int *)(void *)(bytes + 0x134) =
            parentSelectData != 0 ? CSelMode_ModeIdToSelectedIndex(parentSelectData[0]) : 0;
        *(int *)(void *)(bytes + 0x13c) = HostPointer_ToBits32(pointerManager, "cselmode pointer");
        for (entryIndex = 0; entryIndex < 7; entryIndex++) {
            ((int *)(void *)(bytes + 0x144))[entryIndex] = -1;   /* cue handles */
        }

        for (entryIndex = 0; entryIndex < 6; entryIndex++) {
            CSelModeEntry_PlayObject(bytes + 0x1b0, 0, entryIndex, *(int *)(void *)(bytes + 0x134), 0);
        }

        /* Mode buttons 6..9: triplet {0, mode, pointer region of child 0};
           mirrors 10..13: {0, mode, -1}. Only child mode+1 of children 1..4 shows;
           children 0/5/6 take texture frame = mode. */
        for (entryIndex = 0; entryIndex < 8; entryIndex++) {
            int mode = entryIndex & 3;
            unsigned char *entry = bytes + 0x160 + (entryIndex + 6) * CSEL_MODE_ENTRY_SIZE;
            int triplet[3];

            triplet[0] = 0;
            triplet[1] = mode;
            triplet[2] = entryIndex < 4 ?
                PointerManager_RegisterRegion(pointerManager, CSelModeEntry_GetObjectHandle(entry, 0), 0) :
                -1;
            CSelModeEntry_SetTransformTriplet(entry, triplet);
            for (childIndex = 1; childIndex <= 4; childIndex++) {
                CSelModeEntry_SetObjectEnabled(entry, 0, childIndex, 1);
            }
            CSelModeEntry_SetObjectEnabled(entry, 0, mode + 1, 0);
            CSelModeEntry_PlayObject(entry, 0, 0, mode, 0);
            CSelModeEntry_PlayObject(entry, 0, 5, mode, 0);
            CSelModeEntry_PlayObject(entry, 0, 6, mode, 0);
        }

        for (entryIndex = 0; entryIndex < CSEL_MODE_ENTRY_COUNT; entryIndex++) {
            if (entryIndex != 1) {
                CSelModeEntry_SetObjectFlags(bytes + 0x160 + entryIndex * CSEL_MODE_ENTRY_SIZE, 0, 0x40);
            }
        }
        PointerManager_SetSingleMode(pointerManager, 1);
        *(int *)(void *)(bytes + 0x130) = CSEL_MODE_STATE_ENTER_ANIM;
    }

    if (CSelMode_IsVerbose()) {
        unsigned int i;
        for (i = 0; i < blockCount && i < 7; i++) {
            if (CzanLinkResource_GetBlock(linkData, linkSize, i, &block) &&
                CzanLinkResource_IsValid(block.data, block.size)) {
                printf("CSelMode: block %u offset=0x%X size=0x%X nested=%u\n",
                       i,
                       (unsigned int)(block.data - (const unsigned char *)linkData),
                       block.size,
                       CzanLinkResource_GetBlockCount(block.data, block.size));
                CSelMode_LogCaeDescriptors(&block, i);
            }
        }
    }
}

static unsigned char *CSelMode_GetEntry(unsigned char *bytes, int entryIndex) {
    return bytes + 0x160 + entryIndex * CSEL_MODE_ENTRY_SIZE;
}

static int CSelMode_GetEntryMode(unsigned char *bytes, int entryIndex) {
    /* 0x80110C10: entry +0x48 (mode index from the OnEnter triplet). */
    return ((CSelModeEntryKnownFields *)(void *)CSelMode_GetEntry(bytes, entryIndex))->transformOrState1;
}

int CSelMode_Update(void) {
    /* 0x8006EAC8 (skip-frame argument 0). Returns 1 while mode select stays, else
       the next CSelect state from the choice table at 0x80272358. */
    unsigned char *bytes = gHostCSelModeActiveMode;
    int *inputManager;
    int *pointerManager;
    int upPressed;
    int downPressed;
    int leftPressed;
    int rightPressed;
    int confirmPressed;
    int backPressed;
    int previousSelection;
    int selectionChanged = 0;
    int entryIndex;
    int *selection;
    int *state;

    if (bytes == 0 || gHostCSelModeActiveEntries == 0) {
        return 1;
    }
    selection = (int *)(void *)(bytes + 0x134);
    state = (int *)(void *)(bytes + 0x130);
    previousSelection = *selection;
    pointerManager = (int *)RuntimeHostPointerFromBits(*(int *)(void *)(bytes + 0x13c));
    inputManager = GameMain_GetInputOrMenuStateManager();
    upPressed = (int)InputOrMenuStateManager_TestRepeatMask(inputManager, 4, 8);
    downPressed = (int)InputOrMenuStateManager_TestRepeatMask(inputManager, 4, 4);
    leftPressed = (int)InputOrMenuStateManager_TestRepeatMask(inputManager, 4, 1);
    rightPressed = (int)InputOrMenuStateManager_TestRepeatMask(inputManager, 4, 2);
    confirmPressed = (int)InputOrMenuStateManager_IsConfirmPressed(inputManager, 4);
    backPressed = (int)InputOrMenuStateManager_IsBackPressed(inputManager, 4);

    /* 0x8010EEC4: D-pad use holds the pointer back for 1.5 s. */
    if (upPressed || downPressed || leftPressed || rightPressed) {
        PointerManager_SetCooldown(pointerManager, 4);
    }

    switch (*state) {
        case CSEL_MODE_STATE_ENTER_ANIM:
            CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, 0), 0, 0, 0, 0);
            CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, 1), 0, 0, 0, 0);
            for (entryIndex = 6; entryIndex <= 13; entryIndex++) {
                CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, entryIndex), 0, 0, 0, 0);
            }
            CGameUiRoot_SetMenuPresentationMode(GameMain_GetUiRootManager(), 0, 1, 0x19, 0);
            *(int *)(void *)(bytes + 0x144) =
                CharacterAssetManager_PlayCue(GameMain_GetCharacterAssetManager(), 0x258);
            *state = CSEL_MODE_STATE_WAIT_ENTER_ANIM;
            break;
        case CSEL_MODE_STATE_WAIT_ENTER_ANIM:
            if (CSelModeEntry_IsObjectAnimationDone(CSelMode_GetEntry(bytes, 0), 0) != 0) {
                /* 0x287: mode-select voice call. */
                *(int *)(void *)(bytes + 0x158) =
                    CharacterAssetManager_PlayCue(GameMain_GetCharacterAssetManager(), 0x287);
                *state = CSEL_MODE_STATE_INPUT;
            }
            break;
        case CSEL_MODE_STATE_INPUT:
            if (getenv("DDRII_TRACE_CSELMODE") != 0) {
                static int dumped;
                if (!dumped) {
                    dumped = 1;
                    CzanUiManager_DebugDumpGroup(CSelModeEntry_GetObjectHandle(CSelMode_GetEntry(bytes, 7), 0), "button7");
                    {
                        int groups[0x50];
                        int count = UiRootManager_GetPresentationGroups(GameMain_GetUiRootManager(), groups, 0x50);
                        int k;
                        for (k = 0; k < count && k < 0x28; k++) {
                            CzanUiManager_DebugDumpGroup(groups[k], "present");
                        }
                    }
                }
            }
            /* Pointer over one of the four mode buttons selects it (0x8010EC54). */
            for (entryIndex = 6; entryIndex <= 9; entryIndex++) {
                CSelModeEntryKnownFields *entry =
                    (CSelModeEntryKnownFields *)(void *)CSelMode_GetEntry(bytes, entryIndex);

                if (PointerManager_HitTestRegion(pointerManager, 0, entry->transformOrState2) != 0) {
                    *selection = entry->transformOrState1;
                    /* 0x8010F0A4 rumble for the hovering remotes when it changed. */
                    break;
                }
            }
            /* Neighbour table 0x80272348: [up, down, left, right]. */
            if (leftPressed) {
                *selection = kCSelModeNavTable[*selection][2];
            }
            else if (rightPressed) {
                *selection = kCSelModeNavTable[*selection][3];
            }
            else if (upPressed) {
                *selection = kCSelModeNavTable[*selection][0];
            }
            else if (downPressed) {
                *selection = kCSelModeNavTable[*selection][1];
            }

            /* Buttons 6..13: anim 2 (highlight) on the selected mode, else anim 0. */
            for (entryIndex = 6; entryIndex <= 13; entryIndex++) {
                unsigned char *entry = CSelMode_GetEntry(bytes, entryIndex);
                int target = CSelMode_GetEntryMode(bytes, entryIndex) == *selection ? 2 : 0;

                if (CSelModeEntry_GetCachedAnimationId(entry, 0) != target) {
                    CSelModeEntry_StartObjectAnimation(0.0, entry, 0, target, 0, 0);
                    if (target == 2) {
                        selectionChanged = 1;
                    }
                }
            }
            if (selectionChanged && *selection != 4) {
                /* Description panels: entries 2..5, only selection+2 shown. */
                CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, *selection + 2), 0, 0, 0, 0);
                for (entryIndex = 2; entryIndex <= 5; entryIndex++) {
                    CSelModeEntry_SetObjectEnabled(CSelMode_GetEntry(bytes, entryIndex), 0, -1, 1);
                }
                CSelModeEntry_SetObjectEnabled(CSelMode_GetEntry(bytes, *selection + 2), 0, -1, 0);
            }
            if (selectionChanged) {
                for (entryIndex = 0; entryIndex < 6; entryIndex++) {
                    CSelModeEntry_PlayObject(CSelMode_GetEntry(bytes, 1), 0, entryIndex, *selection, 0);
                }
                CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, 1), 0, 2, 0, 0);
            }
            if (*selection != previousSelection) {
                /* 0x8006EF8C: restart the mode voice (0x800CC9A4, not ported) and play
                   the cursor cue 0x24E. */
                *(int *)(void *)(bytes + 0x154) =
                    CharacterAssetManager_PlayCue(GameMain_GetCharacterAssetManager(), 0x24e);
            }
            if (confirmPressed || backPressed) {
                RuntimeDebugReport("CSelMode: %s on mode %d\n", confirmPressed ? "confirm" : "back", *selection);
            }
            if (confirmPressed) {
                CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, *selection + 6), 0, 1, 0, 0);
                CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, *selection + 10), 0, 1, 0, 0);
                *(int *)(void *)(bytes + 0x14c) =
                    CharacterAssetManager_PlayCue(GameMain_GetCharacterAssetManager(), 0x265);
                *state = CSEL_MODE_STATE_LEAVE_ANIM;
            }
            else if (backPressed) {
                *selection = 4;
                *(int *)(void *)(bytes + 0x150) =
                    CharacterAssetManager_PlayCue(GameMain_GetCharacterAssetManager(), 0x253);
                *state = CSEL_MODE_STATE_LEAVE_ANIM;
            }
            break;
        case CSEL_MODE_STATE_LEAVE_ANIM:
            CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, 0), 0, 1, 0, 0);
            CSelModeEntry_StartObjectAnimation(0.0, CSelMode_GetEntry(bytes, 1), 0, 1, 0, 0);
            *(int *)(void *)(bytes + 0x148) =
                CharacterAssetManager_PlayCue(GameMain_GetCharacterAssetManager(), 0x257);
            if (*selection != 4) {
                CSelectCommon_AdvanceBackgroundForward(CSelMode_GetSelectCommon(), 0);
            }
            CGameUiRoot_SetMenuPresentationMode(GameMain_GetUiRootManager(), 0, 0, 0, 0);
            *state = CSEL_MODE_STATE_WAIT_LEAVE_ANIM;
            break;
        case CSEL_MODE_STATE_WAIT_LEAVE_ANIM:
            /* Also waits for the voice cue +0x158 to finish (0x80024598); the host
               has no cue playback state yet, so it counts as finished. */
            if (CSelModeEntry_IsObjectAnimationDone(CSelMode_GetEntry(bytes, 0), 0) != 0) {
                *state = CSEL_MODE_STATE_COMMIT;
            }
            break;
        default:
            break;
    }

    for (entryIndex = 0; entryIndex < CSEL_MODE_ENTRY_COUNT; entryIndex++) {
        CSelModeEntry_ActivateObject(CSelMode_GetEntry(bytes, entryIndex), 0);
    }

    if (*state == CSEL_MODE_STATE_COMMIT) {
        /* 0x8006F180: game mode words from the choice of button selection+6 (the
           back choice 4 reads button 10), next state from choice selection. The
           0x8011FE9C.. setup of gManager_802E70D0 +0x2F1C is not ported yet. */
        const CSelModeChoice *choice = CSelMode_GetChoice(CSelMode_GetEntryMode(bytes, *selection + 6));
        int *parentSelectData = (int *)RuntimeHostPointerFromBits(*(int *)(void *)(bytes + 0x10));

        if (parentSelectData != 0 && choice != 0) {
            parentSelectData[0] = choice->gameModeId;
            parentSelectData[1] = choice->gameModeSubId;
            parentSelectData[2] = choice->gameModeExtraId;
        }
        choice = CSelMode_GetChoice(*selection != 4 ? CSelMode_GetEntryMode(bytes, *selection + 6) : 4);
        return choice != 0 ? choice->nextSelectState : 1;
    }
    return 1;
}

void CSelMode_Release(void) {
    /* Destructor part: drop the four mode-button pointer regions. */
    unsigned char *bytes = gHostCSelModeActiveMode;
    int entryIndex;

    if (bytes == 0) {
        return;
    }
    for (entryIndex = 6; entryIndex <= 9; entryIndex++) {
        CSelModeEntryKnownFields *entry = (CSelModeEntryKnownFields *)(void *)CSelMode_GetEntry(bytes, entryIndex);
        PointerManager_UnregisterRegion(GameMain_GetPointerManager(), entry->transformOrState2);
        entry->transformOrState2 = -1;
    }
}

void CSelMode_SetInitialSelectedMode(void *cselMode) {
    (void)cselMode;

    /* Original reads **(cselMode + 0x10), maps mode IDs 1/2/3/6 to indices 0..3,
       and writes selectedModeIndex at cselMode + 0x134. */
}

void CSelMode_SetHostLinkResourceSize(unsigned int resourceSize) {
    gCSelModeHostLinkResourceSize = resourceSize;
}

static CSelectCommonDrawCache *CSelectCommon_GetDrawCache(int *model) {
    unsigned int i;
    CSelectCommonDrawCache *freeCache = 0;

    if (model == 0) {
        return 0;
    }
    for (i = 0; i < CSELECT_COMMON_DRAW_CACHE_COUNT; i++) {
        if (gCSelectCommonDrawCaches[i].model == model) {
            return &gCSelectCommonDrawCaches[i];
        }
        if (freeCache == 0 && gCSelectCommonDrawCaches[i].model == 0) {
            freeCache = &gCSelectCommonDrawCaches[i];
        }
    }
    if (freeCache != 0) {
        memset(freeCache, 0, sizeof(*freeCache));
        freeCache->model = model;
        return freeCache;
    }

    freeCache = &gCSelectCommonDrawCaches[gCSelectCommonDrawCacheCursor++ % CSELECT_COMMON_DRAW_CACHE_COUNT];
    memset(freeCache, 0, sizeof(*freeCache));
    freeCache->model = model;
    return freeCache;
}

static void CSelectCommon_RebuildDrawCache(CSelectCommonDrawCache *cache, int *model) {
    CzanModelSubmittedPrimitiveBuffer primitiveBuffer;
    void *primaryBlock;
    unsigned int primaryBlockSize;
    int textureSlot;

    if (cache == 0 || model == 0) {
        return;
    }

    primaryBlock = CzanModel_GetHostPrimaryBlock(model);
    primaryBlockSize = CzanModel_GetHostPrimaryBlockSize(model);
    textureSlot = model[0x15];
    if (cache->primaryBlock == primaryBlock &&
        cache->primaryBlockSize == primaryBlockSize &&
        cache->textureSlot == textureSlot) {
        return;
    }

    cache->primaryBlock = primaryBlock;
    cache->primaryBlockSize = primaryBlockSize;
    cache->textureSlot = textureSlot;
    cache->vertexCount = 0;
    cache->primitiveCount = 0;
    if (primaryBlock == 0 || primaryBlockSize == 0) {
        return;
    }

    primitiveBuffer.vertices = cache->vertices;
    primitiveBuffer.texcoords = cache->texcoords;
    primitiveBuffer.colors = cache->colors;
    primitiveBuffer.vertexObjectIndex = 0;
    primitiveBuffer.vertexCapacity = CSELECT_COMMON_DRAW_VERTEX_CAP;
    primitiveBuffer.vertexCount = 0;
    primitiveBuffer.primitiveStart = cache->primitiveStart;
    primitiveBuffer.primitiveVertexCount = cache->primitiveVertexCount;
    primitiveBuffer.primitiveTextureIndex = cache->primitiveTextureIndex;
    primitiveBuffer.primitiveMaterialMode = cache->primitiveMaterialMode;
    primitiveBuffer.primitiveCapacity = CSELECT_COMMON_DRAW_PRIMITIVE_CAP;
    primitiveBuffer.primitiveCount = 0;
    primitiveBuffer.submittedObjectCount = 0;

    CzanModel_SubmitVisibleZmbPrimitiveStreams(primaryBlock, primaryBlockSize, &primitiveBuffer);
    cache->vertexCount = primitiveBuffer.vertexCount;
    cache->primitiveCount = primitiveBuffer.primitiveCount;
    memcpy(cache->boundsMin, primitiveBuffer.boundsMin, sizeof(cache->boundsMin));
    memcpy(cache->boundsMax, primitiveBuffer.boundsMax, sizeof(cache->boundsMax));
}

static void CSelectCommon_CopyName(char *outName, unsigned int outNameSize, const unsigned char *data, unsigned int availableSize) {
    unsigned int i;

    if (outName == 0 || outNameSize == 0) {
        return;
    }

    for (i = 0; i + 1 < outNameSize && i < availableSize; i++) {
        outName[i] = (char)data[i];
        if (outName[i] == '\0') {
            return;
        }
    }
    outName[i] = '\0';
}

static int CSelectCommon_ApplyCameraZabTranslation(const void *zabData, unsigned int zabSize, float *cameraMatrix) {
    const unsigned char *zab = (const unsigned char *)zabData;
    unsigned int channelCount;
    unsigned int channelIndex;

    if (zab == 0 || cameraMatrix == 0 || zabSize < 0x30 || memcmp(zab, "ZAB ", 4) != 0) {
        return 0;
    }

    channelCount = ReadBe32(zab + 0x0c);
    for (channelIndex = 0; channelIndex < channelCount; channelIndex++) {
        unsigned int channelOffset = 0x30 + channelIndex * 0x40;
        unsigned int keyGroupCount;
        unsigned int keyGroupOffset;
        unsigned int groupIndex;
        char channelName[32];

        if (channelOffset + 0x40 > zabSize) {
            break;
        }

        CSelectCommon_CopyName(channelName, sizeof(channelName), zab + channelOffset, zabSize - channelOffset);
        if (strcmp(channelName, "BG_Camera01") != 0) {
            continue;
        }

        keyGroupCount = ReadBe32(zab + channelOffset + 0x34);
        keyGroupOffset = ReadBe32(zab + channelOffset + 0x3c);
        for (groupIndex = 0; groupIndex < keyGroupCount; groupIndex++) {
            unsigned int groupOffset = keyGroupOffset + groupIndex * 0x10;
            unsigned int keyType;
            unsigned int keyCount;
            unsigned int keyOffset;

            if (groupOffset + 0x10 > zabSize) {
                break;
            }

            keyType = ReadBe32(zab + groupOffset);
            keyCount = ReadBe32(zab + groupOffset + 8);
            keyOffset = ReadBe32(zab + groupOffset + 0x0c);
            if (keyType == 0 && keyCount > 0 && keyOffset + 0x10 <= zabSize) {
                cameraMatrix[3] = ReadBeFloat(zab + keyOffset + 4);
                cameraMatrix[7] = ReadBeFloat(zab + keyOffset + 8);
                cameraMatrix[11] = ReadBeFloat(zab + keyOffset + 0x0c);
                return 1;
            }
        }
    }

    return 0;
}

static int CSelectCommon_ApplyModelCameraAnimation(int *model, float *cameraMatrix) {
    int continuationIndex;

    if (model == 0 || cameraMatrix == 0) {
        return 0;
    }

    continuationIndex = *(int *)(void *)((unsigned char *)model + 0x234);
    if (continuationIndex >= 0) {
        void *zabData = CzanModel_GetHostContinuationBlock(model, continuationIndex);
        unsigned int zabSize = HostCzan_GetRegisteredLinkSize(zabData);
        return CSelectCommon_ApplyCameraZabTranslation(zabData, zabSize, cameraMatrix);
    }

    return 0;
}

static int CSelectCommon_FindZmbObjectByName(
    const unsigned char *zmb,
    unsigned int zmbSize,
    unsigned int objectEntryOffset,
    unsigned int objectCount,
    const char *name) {
    unsigned int objectIndex;

    if (zmb == 0 || name == 0 || name[0] == '\0') {
        return -1;
    }

    for (objectIndex = 0; objectIndex < objectCount; objectIndex++) {
        unsigned int entryOffset = objectEntryOffset + objectIndex * 0xa0u;
        char objectName[32];

        if (entryOffset >= zmbSize) {
            break;
        }

        CSelectCommon_CopyName(objectName, sizeof(objectName), zmb + entryOffset, zmbSize - entryOffset);
        if (strcmp(objectName, name) == 0) {
            return (int)objectIndex;
        }
    }

    return -1;
}

static int CSelectCommon_GetSelectCameraMatrix(int *modelOwner, float *outMatrix) {
    int *model;
    const unsigned char *zmb;
    unsigned int zmbSize;
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    int cameraObjectIndex;
    float worldMatrices[CSELECT_COMMON_CAMERA_OBJECT_CAP][12];

    if (modelOwner == 0 || outMatrix == 0) {
        return 0;
    }

    model = CzanModelOwner_GetHostModel(modelOwner);
    if (model == 0) {
        return 0;
    }

    zmb = (const unsigned char *)CzanModel_GetHostPrimaryBlock(model);
    zmbSize = CzanModel_GetHostPrimaryBlockSize(model);
    if (zmb == 0 || zmbSize < 0x30 || memcmp(zmb, "ZMB ", 4) != 0) {
        return 0;
    }

    objectTableOffset = ReadBe32(zmb + 0x20);
    if (objectTableOffset > zmbSize || zmbSize - objectTableOffset < 0x0c) {
        return 0;
    }

    objectCount = ReadBe32(zmb + objectTableOffset);
    objectEntryOffset = ReadBe32(zmb + objectTableOffset + 8);
    if (objectEntryOffset > zmbSize || objectCount == 0) {
        return 0;
    }
    if (objectCount > CSELECT_COMMON_CAMERA_OBJECT_CAP) {
        objectCount = CSELECT_COMMON_CAMERA_OBJECT_CAP;
    }

    CzanModel_BuildZmbObjectWorldMatrices(
        zmb,
        zmbSize,
        objectEntryOffset,
        objectCount,
        worldMatrices,
        CSELECT_COMMON_CAMERA_OBJECT_CAP);
    cameraObjectIndex = CSelectCommon_FindZmbObjectByName(
        zmb,
        zmbSize,
        objectEntryOffset,
        objectCount,
        "BG_Camera01");
    if (cameraObjectIndex < 0 || (unsigned int)cameraObjectIndex >= objectCount) {
        cameraObjectIndex = 0;
    }
    memcpy(outMatrix, worldMatrices[cameraObjectIndex], sizeof(worldMatrices[0]));
    CSelectCommon_ApplyModelCameraAnimation(model, outMatrix);
    return 1;
}

static int CSelectCommon_ProjectModelViewPoint(float x, float y, float z, int *outX, int *outY) {
    float depth;
    float screenCenterX;
    float screenCenterY;
    float fovDeg;
    float aspect;
    float focalX;
    float focalY;
    float f;

    if (outX == 0 || outY == 0) {
        return 0;
    }

    depth = -z;
    if (depth < 1.0f) {
        depth = z;
    }
    if (depth < 1.0f) {
        return 0;
    }

    screenCenterX = (float)RuntimeVideo_GetFramebufferWidth() * 0.5f;
    screenCenterY = (float)RuntimeVideo_GetFramebufferHeight() * 0.5f;
    aspect = (float)RuntimeVideo_GetFramebufferWidth() / (float)RuntimeVideo_GetFramebufferHeight();
    fovDeg = 45.0f;
    if (aspect <= 0.00001f) {
        aspect = 1.333333373f;
    }
    f = 1.0f / tanf((fovDeg * 3.141592741f) / 360.0f);
    focalX = ((float)RuntimeVideo_GetFramebufferWidth() * 0.5f) * (f / aspect);
    focalY = ((float)RuntimeVideo_GetFramebufferHeight() * 0.5f) * f;

    *outX = (int)(screenCenterX + (x * focalX) / depth);
    *outY = (int)(screenCenterY - (y * focalY) / depth);
    return 1;
}

static void CSelectCommon_FlushTriangleBatch(
    unsigned int *batchVertexCount,
    void *textureHandle,
    int textureIndex) {
    if (batchVertexCount == 0 || *batchVertexCount == 0) {
        return;
    }

    DrawTexturedTriangleList2D(
        gCSelectCommonBatchPoints,
        gCSelectCommonBatchTexcoords,
        gCSelectCommonBatchColors,
        *batchVertexCount,
        textureHandle,
        textureIndex);
    *batchVertexCount = 0;
}

static void CSelectCommon_DrawCachedModel(
    int *model,
    float centerYOffset,
    float scaleBias,
    const float *modelViewMatrix) {
    CSelectCommonDrawCache *cache;
    int pass;

    cache = CSelectCommon_GetDrawCache(model);
    CSelectCommon_RebuildDrawCache(cache, model);
    if (cache == 0 || cache->vertexCount == 0 || cache->primitiveCount == 0) {
        return;
    }

    (void)centerYOffset;
    (void)scaleBias;

    for (pass = 0; pass < 2; pass++) {
        unsigned int primitiveIndex;
        unsigned int batchVertexCount = 0;
        int batchTextureIndex = -0x7fffffff;
        void *batchTextureHandle = cache->textureSlot >= 0 ? (void *)(intptr_t)cache->textureSlot : 0;

        for (primitiveIndex = 0; primitiveIndex < cache->primitiveCount; primitiveIndex++) {
            unsigned int start = cache->primitiveStart[primitiveIndex];
            unsigned int count = cache->primitiveVertexCount[primitiveIndex];
            int textureIndex = (int)cache->primitiveTextureIndex[primitiveIndex];
            int materialBlendPass = (cache->primitiveMaterialMode[primitiveIndex] & 0x7fu) != 0;
            unsigned int localIndex;
            unsigned int triangleIndex;
            int projectedOk = 1;
            int points[2048][2];
            float texcoords[2048][2];
            unsigned int colors[2048];

            if (materialBlendPass != pass) {
                continue;
            }
            if (start >= cache->vertexCount) {
                continue;
            }
            if (start + count > cache->vertexCount) {
                count = cache->vertexCount - start;
            }
            if (count < 3) {
                continue;
            }
            if (count > 2048) {
                count = 2048;
            }
            if (textureIndex != batchTextureIndex) {
                CSelectCommon_FlushTriangleBatch(
                    &batchVertexCount,
                    batchTextureHandle,
                    batchTextureIndex);
                batchTextureIndex = textureIndex;
            }

            for (localIndex = 0; localIndex < count; localIndex++) {
                unsigned int vertexIndex = start + localIndex;
                float worldPoint[3];

                worldPoint[0] = cache->vertices[vertexIndex][0];
                worldPoint[1] = cache->vertices[vertexIndex][1];
                worldPoint[2] = cache->vertices[vertexIndex][2];
                if (modelViewMatrix != 0) {
                    CzanModel_TransformPoint(modelViewMatrix, worldPoint, worldPoint);
                }
                projectedOk = CSelectCommon_ProjectModelViewPoint(
                    worldPoint[0],
                    worldPoint[1],
                    worldPoint[2],
                    &points[localIndex][0],
                    &points[localIndex][1]);
                if (!projectedOk) {
                    break;
                }
                texcoords[localIndex][0] = cache->texcoords[vertexIndex][0];
                texcoords[localIndex][1] = cache->texcoords[vertexIndex][1];
                colors[localIndex] = cache->colors[vertexIndex];
            }

            if (!projectedOk) {
                continue;
            }

            for (triangleIndex = 2; triangleIndex < count; triangleIndex++) {
                unsigned int src0;
                unsigned int src1;
                unsigned int src2 = triangleIndex;
                unsigned int out;

                if (batchVertexCount + 3 > CSELECT_COMMON_DRAW_BATCH_VERTEX_CAP) {
                    CSelectCommon_FlushTriangleBatch(
                        &batchVertexCount,
                        batchTextureHandle,
                        batchTextureIndex);
                }

                if ((triangleIndex & 1u) == 0) {
                    src0 = triangleIndex - 2;
                    src1 = triangleIndex - 1;
                }
                else {
                    src0 = triangleIndex - 1;
                    src1 = triangleIndex - 2;
                }

                out = batchVertexCount;
                gCSelectCommonBatchPoints[out][0] = points[src0][0];
                gCSelectCommonBatchPoints[out][1] = points[src0][1];
                gCSelectCommonBatchTexcoords[out][0] = texcoords[src0][0];
                gCSelectCommonBatchTexcoords[out][1] = texcoords[src0][1];
                gCSelectCommonBatchColors[out] = colors[src0];
                out++;

                gCSelectCommonBatchPoints[out][0] = points[src1][0];
                gCSelectCommonBatchPoints[out][1] = points[src1][1];
                gCSelectCommonBatchTexcoords[out][0] = texcoords[src1][0];
                gCSelectCommonBatchTexcoords[out][1] = texcoords[src1][1];
                gCSelectCommonBatchColors[out] = colors[src1];
                out++;

                gCSelectCommonBatchPoints[out][0] = points[src2][0];
                gCSelectCommonBatchPoints[out][1] = points[src2][1];
                gCSelectCommonBatchTexcoords[out][0] = texcoords[src2][0];
                gCSelectCommonBatchTexcoords[out][1] = texcoords[src2][1];
                gCSelectCommonBatchColors[out] = colors[src2];
                batchVertexCount = out + 1;
            }
        }

        CSelectCommon_FlushTriangleBatch(
            &batchVertexCount,
            batchTextureHandle,
            batchTextureIndex);
    }
}

void CSelectCommon_DrawHostBackground(int *selectCommon) {
    int *stagePrimary;
    int *stageSecondary;
    int *modelOwner;
    float modelViewMatrix[12];

    if (selectCommon == 0) {
        return;
    }

    if (*(int *)((unsigned char *)selectCommon + 0x36c) != 1) {
        return;
    }

    stagePrimary = (int *)((unsigned char *)selectCommon + 0x48);
    stageSecondary = (int *)((unsigned char *)selectCommon + 0xb8);
    modelOwner = (int *)((unsigned char *)selectCommon + 0x128);

    CzanModelOwner_UpdateCurrentMatrix(modelOwner, 0);
    CzanModelOwner_CopyCurrentModelMatrix(modelOwner, selectCommon + 6);
    CzanModelOwner_ApplyHostProjection(modelOwner);
    memcpy(modelViewMatrix, selectCommon + 6, sizeof(modelViewMatrix));
    CtsStageObj_DrawModelWithExternalMatrix(stagePrimary, 0, modelViewMatrix, 0, 0);
    {
        unsigned int reflectionColor = 0xffffffff;
        int screenWidth = RuntimeVideo_GetFramebufferWidth();
        int screenHalfHeight = RuntimeVideo_GetFramebufferHeight() >> 1;
        int *surface = (int *)(void *)((unsigned char *)selectCommon + 0x234);
        int surfaceWidth = *(unsigned short *)(void *)((unsigned char *)surface + 0x10);
        int surfaceHeight = *(unsigned short *)(void *)((unsigned char *)surface + 0x12);
        /* Host-only reflection pass (not yet matched to the DOL). It used to read
           +0x224 as a width scale, but +0x224 is the camera aspect. */
        float reflectionWidthScale = 1.0f;
        int reflectionDrawWidth;
        void *reflectionTexture;

        if (surface[0] == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) {
            CzanTextureSurface_AllocateHost(surface, screenWidth >> 1, screenHalfHeight >> 1, 5, 1);
        }

        reflectionTexture = CaptureFrameTextureRegion(0, 0, screenWidth, screenHalfHeight, 1);

        if (reflectionTexture != 0) {
            if (reflectionWidthScale <= 0.0f) {
                reflectionWidthScale = 1.0f;
            }
            reflectionDrawWidth = (int)(reflectionWidthScale * (float)screenWidth);
            DrawCapturedTextureQuad(
                reflectionTexture,
                0,
                screenHalfHeight,
                reflectionDrawWidth,
                screenHalfHeight,
                &reflectionColor,
                1);
        }
    }
    CzanModelOwner_UpdateCurrentMatrix(modelOwner, 0);
    CzanModelOwner_CopyCurrentModelMatrix(modelOwner, selectCommon + 6);
    CzanModelOwner_ApplyHostProjection(modelOwner);
    memcpy(modelViewMatrix, selectCommon + 6, sizeof(modelViewMatrix));
    CtsStageObj_DrawModelWithExternalMatrix(stageSecondary, 0, modelViewMatrix, 0, 0);
    CzanModelOwner_ClearHostProjection();
}

void CSelectCommon_LoadResource(int *selectCommon, void *linkData) {
    /* 0x800982A8 is the select_cmn resource loader for the common background
       scene. It uses CzanLinkManager_GetBlockInfo for block pointer+size pairs.

       Confirmed block map:
       - blocks 0/1: primary model/texture pair for the CtsStageObj at +0x48.
       - block 2: continuation block attached to that +0x48 stage object.
       - blocks 3/4: primary model/texture pair for the CtsStageObj at +0xB8.
       - block 5: primary CzanModel block for the owner at +0x128.
       - blocks 6..0xF: ten continuation/ZAB blocks loaded into owner +0x128.
       - block 0x10: shared CSelModeEntry UI object group used by three entries. */
    int i;
    int blockSize;
    int primarySize;
    int secondarySize;
    void *block;
    void *primaryBlock;
    void *secondaryBlock;
    int *stagePrimary;
    int *stageSecondary;
    int *modelOwner;
    CSelModeEntryKnownFields *movieEntry0;
    CSelModeEntryKnownFields *movieEntry1;
    CSelModeEntryKnownFields *surfaceEntry;

    if (selectCommon == 0 || linkData == 0) {
        return;
    }

    CSelectCommon_Init(selectCommon);

    stagePrimary = (int *)((unsigned char *)selectCommon + 0x48);
    stageSecondary = (int *)((unsigned char *)selectCommon + 0xb8);
    modelOwner = (int *)((unsigned char *)selectCommon + 0x128);
    movieEntry0 = (CSelModeEntryKnownFields *)((unsigned char *)selectCommon + 0x254);
    movieEntry1 = (CSelModeEntryKnownFields *)((unsigned char *)selectCommon + 0x2a4);
    surfaceEntry = (CSelModeEntryKnownFields *)((unsigned char *)selectCommon + 0x2f4);
    CzanModelOwner_Reset(modelOwner);

    *(int *)((unsigned char *)selectCommon + 0x250) = (int)(intptr_t)0;
    *(int *)((unsigned char *)selectCommon + 0x344) = -1;
    *(int *)((unsigned char *)selectCommon + 0x36c) = 1;

    primaryBlock = 0;
    secondaryBlock = 0;
    primarySize = 0;
    secondarySize = 0;
    CtsStageObj_InitBase(stagePrimary);
    if (CzanLinkManager_GetBlockInfo((int *)linkData, 0, &primaryBlock, &primarySize) &&
        CzanLinkManager_GetBlockInfo((int *)linkData, 1, &secondaryBlock, &secondarySize)) {
        HostCzan_RegisterLinkSize(primaryBlock, (unsigned int)primarySize);
        HostCzan_RegisterLinkSize(secondaryBlock, (unsigned int)secondarySize);
        CtsStageObj_LoadPrimarySecondaryBlocks(
            stagePrimary,
            (intptr_t)primaryBlock,
            primarySize,
            (intptr_t)secondaryBlock,
            secondarySize,
            1);
    }
    if (CzanLinkManager_GetBlockInfo((int *)linkData, 2, &block, &blockSize)) {
        HostCzan_RegisterLinkSize(block, (unsigned int)blockSize);
        CtsStageObj_LoadContinuationBlock(stagePrimary, 0, (intptr_t)block);
    }
    CtsStageObj_StartAnimation(0.0, 1.0, stagePrimary, 0, 0, 1);

    primaryBlock = 0;
    secondaryBlock = 0;
    primarySize = 0;
    secondarySize = 0;
    CtsStageObj_InitBase(stageSecondary);
    if (CzanLinkManager_GetBlockInfo((int *)linkData, 3, &primaryBlock, &primarySize) &&
        CzanLinkManager_GetBlockInfo((int *)linkData, 4, &secondaryBlock, &secondarySize)) {
        HostCzan_RegisterLinkSize(primaryBlock, (unsigned int)primarySize);
        HostCzan_RegisterLinkSize(secondaryBlock, (unsigned int)secondarySize);
        CtsStageObj_LoadPrimarySecondaryBlocks(
            stageSecondary,
            (intptr_t)primaryBlock,
            primarySize,
            (intptr_t)secondaryBlock,
            secondarySize,
            1);
    }

    if (CzanLinkManager_GetBlockInfo((int *)linkData, 5, &block, &blockSize)) {
        HostCzan_RegisterLinkSize(block, (unsigned int)blockSize);
        CzanModelOwner_CreateModelFromPrimaryBlock(modelOwner, block, blockSize);
        CzanModelOwner_SetContinuationCount(modelOwner, 10);
        CzanModelOwner_BuildRuntimeDataAt80(modelOwner);
    }

    for (i = 0; i < 10; i++) {
        if (CzanLinkManager_GetBlockInfo((int *)linkData, 6 + i, &block, &blockSize)) {
            HostCzan_RegisterLinkSize(block, (unsigned int)blockSize);
            CzanModelOwner_LoadContinuationBlock(modelOwner, block, i);
        }
    }

    CzanModelOwner_SetAnimationSpeed(modelOwner, 0.0);
    /* 0x800983E8: +0x224 is owner +0xFC, the camera projection aspect.
       16:9 when the widescreen flag (global context +0x258 -> +0x4C) is set,
       otherwise 4:3 (FLOAT_802E8840 / FLOAT_802E8844). */
    {
        int *videoSettings = GlobalRuntimeContext_GetPointerAt(0x258);
        *(float *)(void *)((unsigned char *)selectCommon + 0x224) =
            (videoSettings != 0 && videoSettings[0x4c / 4] != 0) ? 1.7777778f : 1.3333334f;
    }
    CzanModelOwner_UpdateCurrentMatrix(modelOwner, 0);
    CzanTextureSurface_AllocateHost(
        (int *)(void *)((unsigned char *)selectCommon + 0x234),
        RuntimeVideo_GetFramebufferWidth() >> 1,
        RuntimeVideo_GetFramebufferHeight() >> 2,
        5,
        1);
    *(int *)((unsigned char *)selectCommon + 0x230) = 0;

    if (CzanLinkManager_GetBlockInfo((int *)linkData, 0x10, &block, &blockSize)) {
        CSelModeEntry_Init(surfaceEntry);
        surfaceEntry->uiManager = 0;
        CSelModeEntry_AddUiObject(surfaceEntry, block);
        CSelModeEntry_SetObjectEnabled(surfaceEntry, 0, 0, 1);

        CSelModeEntry_Init(movieEntry0);
        movieEntry0->uiManager = 0;
        CSelModeEntry_AddUiObject(movieEntry0, block);
        CSelModeEntry_SetObjectEnabled(movieEntry0, 0, 0, 1);
        CSelModeEntry_SetObjectFlags(movieEntry0, 0, 0x20a);
        CSelModeEntry_ActivateObject(movieEntry0, 0);

        CSelModeEntry_Init(movieEntry1);
        movieEntry1->uiManager = 0;
        CSelModeEntry_AddUiObject(movieEntry1, block);
        CSelModeEntry_SetObjectEnabled(movieEntry1, 0, 0, 1);
        CSelModeEntry_SetObjectFlags(movieEntry1, 0, 0x200);
        CSelModeEntry_ActivateObject(movieEntry1, 0);
    }

    *(int *)((unsigned char *)selectCommon + 0x368) = 1;
    CSelectCommon_UpdateMovieBackground(selectCommon, 0, 1, 0);
    *(int *)((unsigned char *)selectCommon + 0x358) = 0;
    *(int *)((unsigned char *)selectCommon + 0x364) = 0;
    *(int *)((unsigned char *)selectCommon + 0x360) = 0;
    *(int *)((unsigned char *)selectCommon + 0x35c) = 0;
}

void CSelectCommon_UpdateMovieBackground(int *selectCommon, int skipInitialUpdate, int allowMovieStart, int forceInitialBind) {
    /* 0x80098810 updates the select-common THP/movie-backed background layer.

       The original is entered through a saved-register helper, so the decompiler
       shows an unused first parameter. The real object is the selectCommon pointer
       recovered from that helper.

       Confirmed responsibilities:
       - pauses/resets the CzanModelOwner at selectCommon +0x128 through
         CzanModelOwner_SetAnimationSpeed-like helpers before rebinding video.
       - resets the two CtsStageObj layers at +0x48 and +0xB8 through their vtables.
       - when allowMovieStart is nonzero and selectCommon +0x358 is 1, selects a
         movie path from /sound/stream/mu_bgm_999/movie/b_* based on
         selectCommon +0x34C.
       - uses movie manager gManager_802E70A8 and handle selectCommon +0x344.
       - once the movie is ready, binds the movie object/texture to the two
         CSelModeEntry objects at +0x254 and +0x2A4 through the Czan UI manager
         stored at selectCommon +0x250.
       - selectCommon +0x350/+0x354 track requested/active movie binding.
       - selectCommon +0x358/+0x35C/+0x360/+0x364/+0x368 are the loader/start/bind
         state flags controlling the movie background and menu-entry reveal path.

       Important follow-up callees from the original:
       - MovieSlotHandle_LoadResource: load/assign THP movie path.
       - MovieSlotHandle_HasPlaybackStarted: poll movie playback-started bit.
       - MovieSlotHandle_StartPlayback: start/fade movie playback.
       - MovieSlotHandle_GetClaimedObject: get active movie object/state.
       - FUN_80025104: release/stop movie binding.
       - CSelectCommon_RevealMovieEntriesPrimary /
         CSelectCommon_RevealMovieEntriesAlternate: reveal/transition the two menu entry objects
         after the movie object has been attached. */
    (void)allowMovieStart;
    if (selectCommon == 0) {
        return;
    }

    if (skipInitialUpdate == 1) {
        return;
    }
    if (forceInitialBind == 1 &&
        *(int *)((unsigned char *)selectCommon + 0x364) == 0 &&
        *(int *)((unsigned char *)selectCommon + 0x358) == 0) {
        return;
    }

    CzanModelOwner_UpdateCurrentMatrix((int *)((unsigned char *)selectCommon + 0x128), 0);
    CzanModelOwner_CopyCurrentModelMatrix((int *)((unsigned char *)selectCommon + 0x128), selectCommon + 6);
    CtsStageObj_UpdateAnimationFrame((int *)((unsigned char *)selectCommon + 0x48), 0);
    CtsStageObj_UpdateAnimationFrame((int *)((unsigned char *)selectCommon + 0xb8), 0);
}

void CSelectCommon_AdvanceBackgroundForward(int *selectCommon, int revealEntries) {
    int *modelOwner;
    int currentIndex;
    int nextIndex;

    if (selectCommon == 0) {
        return;
    }

    modelOwner = (int *)((unsigned char *)selectCommon + 0x128);
    currentIndex = *(int *)((unsigned char *)selectCommon + 0x230);
    CzanModelOwner_SetAnimationSpeed(modelOwner, 1.0);
    CzanModelOwner_StartAnimation(modelOwner, 0.0, currentIndex << 1, 0, 0);
    *(int *)((unsigned char *)selectCommon + 0x368) = 1;

    nextIndex = currentIndex + 1;
    while (nextIndex >= 5) {
        nextIndex -= 5;
    }
    *(int *)((unsigned char *)selectCommon + 0x230) = nextIndex;

    if (revealEntries == 1) {
        CSelectCommon_RevealMovieEntriesAlternate(selectCommon, 1);
    }
}

void CSelectCommon_AdvanceBackgroundBackward(int *selectCommon, int revealEntries) {
    int *modelOwner;
    int currentIndex;

    if (selectCommon == 0) {
        return;
    }

    modelOwner = (int *)((unsigned char *)selectCommon + 0x128);
    currentIndex = *(int *)((unsigned char *)selectCommon + 0x230) + 4;
    while (currentIndex >= 5) {
        currentIndex -= 5;
    }
    *(int *)((unsigned char *)selectCommon + 0x230) = currentIndex;

    CzanModelOwner_SetAnimationSpeed(modelOwner, 1.0);
    CzanModelOwner_StartAnimation(modelOwner, 0.0, currentIndex * 2 + 1, 0, 0);
    *(int *)((unsigned char *)selectCommon + 0x368) = 0;

    if (revealEntries == 1) {
        CSelectCommon_RevealMovieEntriesPrimary(selectCommon, 0);
    }
}

void CSelectCommon_RevealMovieEntriesPrimary(int *selectCommon, int useImmediateTiming) {
    /* 0x80098FA0 reveals/transitions the two movie-backed CSelModeEntry objects
       after CSelectCommon_UpdateMovieBackground has attached the movie object.

       Confirmed behavior:
       - if selectCommon +0x350 is set, disables object 0 in entry +0x254, applies
         timing/layout through CSelModeEntry_ResetObjectAnimation and
         CSelModeEntry_StartObjectAnimation, runs the entry vtable method at +0x14,
         then binds/updates the UI object through the Czan UI manager at +0x250 and
         applies a 10-frame alpha/color transition
       - repeats the same flow for selectCommon +0x354 and entry +0x2A4
       - useImmediateTiming selects between the shorter FLOAT_802E883C timing and
         the alternate FLOAT_802E8850 timing/layout mode */
    (void)selectCommon;
    (void)useImmediateTiming;
}

void CSelectCommon_RevealMovieEntriesAlternate(int *selectCommon, int useImmediateTiming) {
    /* 0x800991C4 is the alternate movie-entry reveal path.

       It mirrors CSelectCommon_RevealMovieEntriesPrimary but uses the alternate
       layout/animation ids for the two entries:
       - entry +0x254 uses animation/layout id 1
       - entry +0x2A4 uses animation/layout id 3

       This is selected by CSelectCommon_UpdateMovieBackground when +0x368 is not
       the primary reveal mode. */
    (void)selectCommon;
    (void)useImmediateTiming;
}

int CSelModeEntry_Init(void *entry) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int *words = (int *)entry;
    int i;

    /* Original calls the shared UI-entry base initializer at 0x801102DC and sets
       the entry vtable at +0x40 to PTR_PTR_802BEA38. */
    if (modeEntry != 0) {
        ClearMemory(modeEntry, 0, CSEL_MODE_ENTRY_SIZE);
        /* PTR_FUN_802BEA78 -> 0x80110BA4. This initializes the shared 4-slot
           entry base: slot type words at +0..+0x0C, handles at +0x14..+0x20,
           animation caches at +0x24..+0x30, and object count at +0x34. */
        words[0] = 6;
        words[1] = 0;
        words[2] = 0;
        words[3] = 0;
        words[4] = 0;
        for (i = 0; i < 4; i++) {
            modeEntry->objectHandles[i] = -1;
            modeEntry->cachedAnimationIds[i] = -1;
        }
        modeEntry->objectHandleCount = 0;
        words[14] = 0;
        words[15] = 0;
        modeEntry->vtable = 0x802BEA38;
    }
    return entry != 0;
}

int CSelModeEntry_Update(void *entry, short activeCountOrFlag) {
    /* Original updates the shared UI-entry base state, then frees/releases the
       entry through MemoryPool_Free when the caller passes a positive flag/count. */
    if (entry != 0 && activeCountOrFlag > 0) {
        return 1;
    }
    return entry != 0;
}

int CSelModeEntry_AddUiObject(void *entry, void *linkBlock) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int slot;
    int groupHandle;

    /* 0x80110524 creates/registers a UI object group from a non-null Czan link block
       via CzanUiManager_CreateObjectGroup, stores the returned handle
       at +0x14+n*4, and increments +0x34. */
    if (modeEntry == 0 || linkBlock == 0 || modeEntry->objectHandleCount >= 4) {
        return -1;
    }

    slot = modeEntry->objectHandleCount;
    groupHandle = CzanUiManager_CreateObjectGroup(0, linkBlock, 0, 0);
    modeEntry->objectHandles[slot] = groupHandle;
    modeEntry->objectHandleCount++;
    return modeEntry->objectHandles[slot];
}

int CSelModeEntry_AddChildUiObject(void *entry, int objectId) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int slot;

    /* 0x8011058C creates/registers a cloned/alternate UI object group through
       CzanUiManager_CloneObjectGroup, then stores the returned handle in the
       same +0x14 handle array. */
    if (modeEntry == 0 || objectId == -1 || modeEntry->objectHandleCount >= 4) {
        return -1;
    }

    slot = modeEntry->objectHandleCount;
    modeEntry->objectHandles[slot] = CzanUiManager_CloneObjectGroup(0, objectId, 0, 0);
    modeEntry->objectHandleCount++;
    return modeEntry->objectHandles[slot];
}

void CSelModeEntry_SetObjectEnabled(void *entry, int objectSlot, int childSlot, unsigned char enabled) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    if (modeEntry == 0 ||
        objectSlot < 0 ||
        objectSlot >= modeEntry->objectHandleCount ||
        modeEntry->objectHandles[objectSlot] < 0) {
        return;
    }

    /* FUN_80110B14 forwards to the child-object +0x173 helper. Nonzero suppresses
       that child in the real draw wrapper, matching CzanUiManager_SetChildObjectEnabled. */
    if (childSlot == -1) {
        CzanUiManager_SetObjectGroupEnabled(0, modeEntry->objectHandles[objectSlot], enabled);
    }
    else {
        CzanUiManager_SetChildObjectEnabled(0, modeEntry->objectHandles[objectSlot], childSlot, enabled);
    }
}

void CSelModeEntry_SetPositionOrLayout(void *entry, int objectSlot, int childObjectIndex, float *layoutData) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int objectHandle;

    /* 0x801106E8 applies position/layout data to one stored object handle.
       childObjectIndex == -1 calls CzanUiManager_ApplyObjectGroupPositionLayout;
       otherwise it calls CzanUiManager_ApplyChildObjectPositionLayout. */
    if (modeEntry == 0 || objectSlot < 0 || objectSlot >= modeEntry->objectHandleCount) {
        return;
    }

    objectHandle = modeEntry->objectHandles[objectSlot];
    if (objectHandle < 0) {
        return;
    }
    if (childObjectIndex == -1) {
        CzanUiManager_ApplyObjectGroupPositionLayout(0, objectHandle, layoutData);
    }
    else {
        CzanUiManager_ApplyChildObjectPositionLayout(0, objectHandle, childObjectIndex, layoutData);
    }
}

void CSelModeEntry_SetAnimationOrLayout(void *entry, int objectSlot, int animationId, float *animationData) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int objectHandle;

    /* 0x80110754 applies animation/layout data to one stored object handle.
       animationId == -1 calls FUN_801751B8(uiManager, handle, animationData);
       otherwise it calls FUN_80175240(uiManager, handle). */
    if (modeEntry == 0 || objectSlot < 0 || objectSlot >= modeEntry->objectHandleCount) {
        return;
    }

    objectHandle = modeEntry->objectHandles[objectSlot];
    if (objectHandle < 0) {
        return;
    }
    if (animationId == -1) {
        CzanUiManager_ApplyObjectGroupAnimationOffset(0, objectHandle, animationData);
    }
    else {
        CzanUiManager_ApplyChildObjectAnimationOffset(0, objectHandle, animationId, animationData);
    }
}

void CSelModeEntry_ResetObjectAnimation(void *entry, int objectSlot) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    /* 0x801106C4 forwards the selected CSelModeEntry object handle to the Czan UI
       manager reset/clear-animation helper.

       Original behavior:
       CzanUiManager_ResetObjectAnimation(entry +0x3C, entry->objectHandles[objectSlot]) */
    if (modeEntry == 0 || objectSlot < 0 || objectSlot >= modeEntry->objectHandleCount) {
        return;
    }
    CzanUiManager_ResetObjectGroupAnimationTime(0.0, 0, modeEntry->objectHandles[objectSlot]);
}

int CSelModeEntry_IsObjectAnimationDone(void *entry, int objectSlot) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    /* 0x80110680 returns CzanUiManager_IsObjectGroupAnimationDone for the stored
       object handle. Title state 6/0x0D uses this to advance the title-call
       objects after the OP movie. */
    if (modeEntry == 0 ||
        objectSlot < 0 ||
        objectSlot >= modeEntry->objectHandleCount ||
        modeEntry->objectHandles[objectSlot] < 0) {
        return 0;
    }

    return CzanUiManager_IsObjectGroupAnimationDone(0, modeEntry->objectHandles[objectSlot]);
}

int CSelModeEntry_GetCachedAnimationId(void *entry, int objectSlot) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    /* 0x801106D8 returns the animation id cached by
       CSelModeEntry_StartObjectAnimation at entry +0x24 + slot*4. */
    if (modeEntry == 0 ||
        objectSlot < 0 ||
        objectSlot >= modeEntry->objectHandleCount) {
        return -1;
    }

    return modeEntry->cachedAnimationIds[objectSlot];
}

int CSelModeEntry_GetObjectHandle(void *entry, int objectSlot) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    /* 0x80110B94 returns entry +0x14 + slot*4. */
    if (modeEntry == 0 ||
        objectSlot < 0 ||
        objectSlot >= modeEntry->objectHandleCount) {
        return -1;
    }

    return modeEntry->objectHandles[objectSlot];
}

void CSelModeEntry_StartObjectAnimation(
    double startFrame,
    void *entry,
    int objectSlot,
    int animationId,
    unsigned char mode,
    int playbackMode) {
    /* 0x801105FC starts/configures one CSelModeEntry object animation when the
       object handle is valid.

       Confirmed behavior:
       - object handle comes from entry +0x14 + objectSlot*4
       - calls UI-manager helpers at 0x80174F60 and 0x80174FE0 with mode/playbackMode
       - calls 0x80174E2C(startFrame, uiManager, objectHandle, animationId)
       - caches animationId at entry +0x24 + objectSlot*4 */
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int objectHandle;

    if (modeEntry == 0 || objectSlot < 0 || objectSlot >= modeEntry->objectHandleCount) {
        return;
    }

    objectHandle = modeEntry->objectHandles[objectSlot];
    if (objectHandle < 0) {
        return;
    }
    CzanUiManager_SetObjectGroupAnimationResetMode(0, objectHandle, mode);
    CzanUiManager_SetObjectGroupAnimationMode(0, objectHandle, (unsigned char)playbackMode);
    CzanUiManager_StartObjectGroupAnimation(startFrame, 0, objectHandle, animationId);
    modeEntry->cachedAnimationIds[objectSlot] = animationId;
}

void CSelModeEntry_SetObjectFlags(void *entry, int objectSlot, int flags) {
    unsigned char *bytes = (unsigned char *)entry;

    /* 0x80110B04 stores a per-object CSelMode entry word at +0x04+n*4.
       The entry vtable consumes it later; keep the raw word rather than mapping
       it onto host visibility/order until that vtable table is recovered. */
    if (bytes == 0 || objectSlot < 0 || objectSlot >= 4) {
        return;
    }
    *(int *)(void *)(bytes + 0x04 + objectSlot * 4) = flags;
}

void CSelModeEntry_ActivateObject(void *entry, int objectSlot) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int *words = (int *)entry;
    int slot;

    /* PTR_PTR_802BEA38 +0x14 -> 0x80110448. The second argument is zero in
       CSelMode_OnEnter, which applies priorities to every populated object slot:
       FUN_8017576C(uiManager, handle, DAT_8027CFE8[slotType] + slotFlag). */
    if (modeEntry == 0 || objectSlot != 0 || modeEntry->objectHandleCount <= 0) {
        return;
    }
    for (slot = 0; slot < modeEntry->objectHandleCount && slot < 4; slot++) {
        int slotType = words[slot];
        int slotFlag = words[slot + 1];
        int priority = slotFlag;
        if (0 <= slotType && slotType < 8) {
            priority += CSelModeEntry_PriorityBaseTableHost[slotType];
        }
        if (modeEntry->objectHandles[slot] >= 0) {
            CzanUiManager_SetObjectGroupPriority(0, modeEntry->objectHandles[slot], priority);
        }
    }
}

void CSelModeEntry_PlayObject(void *entry, int objectSlot, int childObjectIndex, int textureFrame, int updateSpriteDimensions) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int objectHandle;

    /* 0x80110B80 forwards to CzanUiManager_SetObjectTextureFrame(entry +0x3C,
       handle, childObjectIndex, textureFrame, updateSpriteDimensions). */
    if (modeEntry == 0 || objectSlot < 0 || objectSlot >= modeEntry->objectHandleCount) {
        return;
    }
    objectHandle = modeEntry->objectHandles[objectSlot];
    if (objectHandle >= 0) {
        CzanUiManager_SetObjectTextureFrame(0, objectHandle, childObjectIndex, textureFrame, updateSpriteDimensions);
    }
}

void CSelModeEntry_SetTransformTriplet(void *entry, const int *values) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    /* 0x80110BF4 copies three 32-bit values into +0x44, +0x48, +0x4C.
       In CSelMode_OnEnter these values are built from local_48/local_44/local_40
       before applying layout to each mode-entry object. */
    if (modeEntry == 0 || values == 0) {
        return;
    }

    modeEntry->transformOrState0 = values[0];
    modeEntry->transformOrState1 = values[1];
    modeEntry->transformOrState2 = values[2];
}

int CzanUiObjectInstance_Init(void *objectInstance) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x801711DC initializes the 0x1B4-byte UI animation/object instance created
       by CzanUiManager_CreateObjectGroup. The original clears transform/matrix
       state, initializes two color/parameter blocks, sets playback/visibility
       flags, stores default animation fields, and sets the bottom bound from the
       current screen height. */
    if (instance == 0) {
        return 0;
    }

    instance->manager = 0;
    instance->spriteObject = 0;
    instance->animationCounterOrTimer = 0;
    instance->currentAnimationId = -2;
    instance->activeAnimationEntry = 0;
    instance->initialAnimIndex = 0;
    instance->playbackRate = 1.0f;
    instance->unknownHandle188 = -1;
    instance->descriptor = 0;
    instance->currentAnimValue = 0;
    return 1;
}

int CzanSpriteObject_Init(void *spriteObject) {
    CzanSpriteObjectKnownFields *sprite = (CzanSpriteObjectKnownFields *)spriteObject;

    /* 0x8016BA8C initializes the 0x1D8-byte sprite/texture object paired with a
       CzanUiObjectInstance. The original clears texture references, sets anchor
       modes, initializes transform floats/colors/render defaults, and clears the
       draw callbacks later filled by CzanUiManager_CreateObjectGroup. */
    if (sprite == 0) {
        return 0;
    }

    sprite->enabled = 0;
    sprite->textureSlot = 0;
    sprite->textureHeader = 0;
    sprite->textureResourceHandle = 0;
    sprite->textureIndex = 0;
    sprite->uvOrFrameIndex = 0;
    sprite->xAnchorMode = 4;
    sprite->yAnchorMode = 0;
    sprite->ownsTexture = 1;
    sprite->textureReady = 0;
    sprite->color0[0] = 0xFF;
    sprite->color0[1] = 0xFF;
    sprite->color0[2] = 0xFF;
    sprite->color0[3] = 0xFF;
    sprite->color1[0] = 0xFF;
    sprite->color1[1] = 0xFF;
    sprite->color1[2] = 0xFF;
    sprite->color1[3] = 0xFF;
    sprite->color2[0] = 0xFF;
    sprite->color2[1] = 0xFF;
    sprite->color2[2] = 0xFF;
    sprite->color2[3] = 0xFF;
    sprite->color3[0] = 0xFF;
    sprite->color3[1] = 0xFF;
    sprite->color3[2] = 0xFF;
    sprite->color3[3] = 0xFF;
    sprite->visibleFlag = 0;
    sprite->renderMode174 = 1;
    return 1;
}

int *GlobalUiFrameState_CreateOnce(void) {
    int *state;

    /* FUN_80188284 lazily allocates DAT_802E71F8, a 0xF4-byte global UI/frame
       state object with a vtable at +0xF0 and cleared runtime fields. */
    if (gGlobalUiFrameState802e71f8 != 0) {
        return gGlobalUiFrameState802e71f8;
    }

    state = (int *)MemoryPool_AllocateAligned(0, 0xf4, 0x20);
    if (state != 0) {
        ClearMemory(state, 0, 0xe4);
        state[0xe4 / 4] = 0;
        state[0xe8 / 4] = 0;
        state[0xec / 4] = 0;
        state[0xf0 / 4] = 0;
    }
    gGlobalUiFrameState802e71f8 = state;
    return state;
}

int CzanUiManager_AllocateObjectGroupStorage(int *uiManager, int groupCapacity, int pointerCapacity) {
    int groupIndex;
    unsigned char *groups;

    /* FUN_801731E4 allocates the UI manager object-group tables once. */
    UiScreenProjection_UpdateGlobals();
    if (uiManager == 0 || uiManager[1] != 0) {
        return 0;
    }

    uiManager[0] = groupCapacity;
    uiManager[1] = UiManagerHostPointerBits(MemoryPool_AllocateAligned(0, groupCapacity * 0x28, 0x20));
    uiManager[2] = UiManagerHostPointerBits(MemoryPool_AllocateAligned(0, 4, 0x20));
    if (uiManager[1] == 0 || uiManager[2] == 0) {
        return 0;
    }

    ClearMemory(UiManagerHostPointerFromBits(uiManager[1]), 0, groupCapacity * 0x28);
    ClearMemory(UiManagerHostPointerFromBits(uiManager[2]), 0, 4);
    groups = (unsigned char *)UiManagerHostPointerFromBits(uiManager[1]);
    for (groupIndex = 0; groupIndex < groupCapacity; groupIndex++) {
        unsigned char *group = groups + groupIndex * 0x28;
        *(int *)(group + 0x00) = -1;
        *(int *)(group + 0x04) = 0;
        *(unsigned char *)(group + 0x08) = 0;
        ClearMemory(group + 0x0c, 0, 0x10);
        *(int *)(group + 0x1c) = 0;
        *(int *)(group + 0x20) = 0;
        *(unsigned char *)(group + 0x24) = 0;
    }

    uiManager[5] = 0;
    uiManager[3] = pointerCapacity;
    uiManager[7] = UiManagerHostPointerBits(MemoryPool_AllocateAligned(0, pointerCapacity << 2, 0x20));
    if (uiManager[7] != 0) {
        ClearMemory(UiManagerHostPointerFromBits(uiManager[7]), 0, pointerCapacity << 2);
    }
    return uiManager[7] != 0 ? 1 : 0;
}

int CzanUiManager_CreateObjectGroup(
    int uiManager,
    void *linkData,
    unsigned int flags,
    int initialAnimIndex
) {
    unsigned int linkSize;
    CzanLinkBlock textureContainer;
    CzanLinkBlock metadataBlock;
    unsigned int descriptorCount;
    unsigned int descriptorOffset;
    int groupHandle;
    HostCzanGroup *group;
    int i;
    int activeCount;

    /* 0x80173414 creates a new Czan object group from a WII/Czan link resource.
       It finds a free group slot, links the resource, validates/relocates block 1 as
       the group metadata, uses block 0 as a nested texture/TPL resource container,
       then creates one CzanUiObjectInstance and CzanSpriteObject per 0x20-byte child
       descriptor.

       Descriptor type 0 loads a TPL block and creates a texture slot. Type 2 creates
       an 8x8 dummy sprite. Other descriptor types copy/reuse texture state from a
       previously-created child named by descriptor +0x12. The low byte of flags and
       descriptor +0x1A flags drive the same initial animation/visibility setup used
       by CzanUiManager_CloneObjectGroup. Flags 3/4 preplay the selected animation
       through CzanUiObjectInstance_PreplayInitialAnimation.

       After every child is created, the original sets uiManager +0x18 and +0x19 to 1,
       sets group +0x24 bit 0, and computes group +0x24 bit 3 from child +0x17D
       activity. Missing those manager/group flags means the later draw traversal has
       no drawable object list even if textures loaded correctly. Returns the new
       group handle, -1 when no group slot is free, or -2 when metadata validation fails. */
    (void)uiManager;

    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return -2;
    }

    if (!CzanLinkResource_GetBlock(linkData, linkSize, 1, &metadataBlock) ||
        !CzanLinkResource_GetBlock(linkData, linkSize, 0, &textureContainer) ||
        !CzanLinkResource_IsValid(textureContainer.data, textureContainer.size)) {
        return -2;
    }

    if (!CzanUiManager_ValidateAndRelocateObjectGroupMetadata(0, (char *)metadataBlock.data)) {
        return -2;
    }

    descriptorCount = ReadBe16(metadataBlock.data + 8);
    descriptorOffset = ReadBe32(metadataBlock.data + 0x0C);
    if (descriptorOffset >= metadataBlock.size ||
        descriptorCount > HOST_CZAN_MAX_OBJECTS ||
        descriptorOffset + descriptorCount * 0x20u > metadataBlock.size) {
        return -2;
    }

    groupHandle = HostCzan_FindFreeGroup();
    if (groupHandle < 0) {
        return -1;
    }

    group = &gHostCzanGroups[groupHandle];
    memset(group, 0, sizeof(*group));
    group->used = 1;
    group->state = -1;
    group->childCount = (int)descriptorCount;
    group->firstObjectIndex = -1;
    group->byte25 = 0;

    activeCount = 0;
    for (i = 0; i < (int)descriptorCount; i++) {
        const unsigned char *descriptor = metadataBlock.data + descriptorOffset + (unsigned int)i * 0x20u;
        unsigned int descriptorFlags = ReadBe16(descriptor + 0x1A);
        unsigned int descriptorType = descriptor[0x14];
        int objectIndex = HostCzan_FindFreeObject();
        HostCzanObject *object;

        if (objectIndex < 0) {
            break;
        }

        if (group->firstObjectIndex < 0) {
            group->firstObjectIndex = objectIndex;
        }

        object = &gHostCzanObjects[objectIndex];
        memset(object, 0, sizeof(*object));
        object->used = 1;
        object->groupHandle = groupHandle;
        object->childIndex = i;
        object->descriptorType = (int)descriptorType;
        object->suppressDraw173 = (descriptorFlags & 0x001u) != 0;
        object->drawEnabled = 1;
        object->blockColor[0] = object->blockColor[1] = object->blockColor[2] = object->blockColor[3] = 0xff;
        object->activeByte17d = (descriptorFlags & 0x040u) == 0;
        object->textureSlot = -1;
        object->textureIndex = 0;
        object->baseScaleX = 1.0f;
        object->baseScaleY = 1.0f;
        object->baseScaleZ = 1.0f;
        object->scaleX = 1.0f;
        object->scaleY = 1.0f;
        object->scaleZ = 1.0f;
        object->uvSpanX = 1.0f;
        object->uvSpanY = 1.0f;
        object->width = 8;
        object->height = 8;
        object->color[0] = 0xFF;
        object->color[1] = 0xFF;
        object->color[2] = 0xFF;
        object->color[3] = 0xFF;
        object->metadata = metadataBlock.data;
        object->metadataSize = metadataBlock.size;
        object->descriptorOffset = descriptorOffset + (unsigned int)i * 0x20u;
        memcpy(object->name, descriptor, 16);
        object->name[16] = '\0';
        HostCzan_DefaultObjectPlacement(object, groupHandle, i);

        if (descriptorType == 0) {
            unsigned int textureBlockIndex = ReadBe16(descriptor + 0x10);
            CzanLinkBlock textureBlock;

            if (CzanLinkResource_GetBlock(textureContainer.data, textureContainer.size, textureBlockIndex, &textureBlock)) {
                unsigned int slot = CreateTextureFromTplResource(
                    &gHostTextureManager,
                    (void *)textureBlock.data,
                    (int)textureBlock.size,
                    0xFFFFFFFFu);
                object->textureSlot = (int)slot;
                GetTextureDimensions((void *)(long)object->textureSlot, 0, &object->width, &object->height);
                HostCzan_SetDefaultSpriteCenter(object);
                if (strncmp(object->name, "white_window", 12) == 0) {
                    object->color[3] = 0x78;
                }
            }
        }
        else if (descriptorType != 2) {
            unsigned int sourceIndex = ReadBe16(descriptor + 0x12);
            int j;

            for (j = 0; j < HOST_CZAN_MAX_OBJECTS; j++) {
                if (gHostCzanObjects[j].used &&
                    gHostCzanObjects[j].groupHandle == groupHandle &&
                    gHostCzanObjects[j].childIndex == (int)sourceIndex) {
                    object->textureSlot = gHostCzanObjects[j].textureSlot;
                    object->textureIndex = gHostCzanObjects[j].textureIndex;
                    object->width = gHostCzanObjects[j].width;
                    object->height = gHostCzanObjects[j].height;
                    object->color[3] = gHostCzanObjects[j].color[3];
                    /* CzanUiManager_CreateObjectGroup gives type-0 texture
                       descriptors half-size +0x78/+0x7C, but cloned texture
                       descriptors use full width/height there. */
                    HostCzan_SetDefaultSpriteFullSizeBase(object);
                    break;
                }
            }
        }
        else {
            HostCzan_SetDefaultSpriteCenter(object);
        }

        if ((descriptorFlags & 0x080u) != 0 && group->groupByte08 == 0) {
            group->groupByte08 = 0xFF;
        }
        else if (group->groupByte08 == 0) {
            if ((descriptorFlags & 0x100u) != 0) {
                group->groupByte08 |= 1;
            }
            if ((descriptorFlags & 0x200u) != 0) {
                group->groupByte08 |= 2;
            }
        }

        object->currentAnimation = initialAnimIndex;
        object->animationCommandOffset = HostCzan_GetAnimationCommandOffset(object, initialAnimIndex);
        HostCzan_ApplyCreateFlags(object, flags & 0xffu, initialAnimIndex, descriptorFlags);
        activeCount += object->activeByte17d != 0;
    }

    group->statusFlags24 |= 1;
    if (activeCount == 0 && group->state == -1) {
        group->statusFlags24 |= 8;
    }
    else {
        group->statusFlags24 &= (unsigned char)~8u;
    }

    printf("CzanUiManager: created group %d children=%d firstObject=%d flags=0x%02X\n",
           groupHandle,
           group->childCount,
           group->firstObjectIndex,
           group->statusFlags24);
    return groupHandle;
}

int CzanUiManager_ValidateAndRelocateObjectGroupMetadata(int uiManager, char *metadataBlock) {
    /* 0x80174D14 validates that metadataBlock starts with "CAE_WII\0". If the
       relocation byte at +0x0B is already nonzero, the block is accepted as already
       relocated. Otherwise it sets +0x0B to 1, converts the descriptor-table offset
       at +0x0C into an absolute pointer, then walks every 0x20-byte descriptor and
       converts each descriptor animation-table offset at +0x1C into a pointer.

       For every 0x10-byte animation entry, an entry with +0x04 == 0 has its +0x0C
       pointer/value cleared to 0; otherwise +0x0C is relocated relative to the
       metadata block base. The uiManager argument is present in the signature but
       is not used by the decompiled body. */
    (void)uiManager;

    if (metadataBlock == 0) {
        return 0;
    }

    return metadataBlock[0] == 'C' &&
           metadataBlock[1] == 'A' &&
           metadataBlock[2] == 'E' &&
           metadataBlock[3] == '_' &&
           metadataBlock[4] == 'W' &&
           metadataBlock[5] == 'I' &&
           metadataBlock[6] == 'I' &&
           metadataBlock[7] == '\0';
}

int CzanUiManager_CloneObjectGroup(
    int uiManager,
    int sourceObjectGroupHandle,
    unsigned int cloneFlags,
    int initialAnimIndex
) {
    /* 0x80173D18 finds a free object-group slot, copies the source group's
       descriptor pointer and child count, allocates a new child-object handle array,
       then creates a fresh CzanUiObjectInstance and CzanSpriteObject for every source
       child. Real sprite descriptors copy texture information from the source child;
       descriptor type 2 creates an 8x8 dummy sprite.

       The low byte of cloneFlags controls initial animation behavior:
       1/2 start and run the selected animation, 3/4 preplay the selected animation
       through CzanUiObjectInstance_PreplayInitialAnimation.
       Descriptor flags also set enabled/draw/animation fields on the cloned children.
       The function returns the new group handle, or -1 if no free group slot exists. */
    int groupHandle;
    int i;
    HostCzanGroup *sourceGroup;
    HostCzanGroup *cloneGroup;

    (void)uiManager;

    if (sourceObjectGroupHandle < 0 ||
        sourceObjectGroupHandle >= HOST_CZAN_MAX_GROUPS ||
        !gHostCzanGroups[sourceObjectGroupHandle].used) {
        return -1;
    }

    groupHandle = HostCzan_FindFreeGroup();
    if (groupHandle < 0) {
        return -1;
    }

    sourceGroup = &gHostCzanGroups[sourceObjectGroupHandle];
    cloneGroup = &gHostCzanGroups[groupHandle];
    *cloneGroup = *sourceGroup;
    cloneGroup->used = 1;
    cloneGroup->firstObjectIndex = -1;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        int objectIndex;
        HostCzanObject *cloneObject;

        if (!gHostCzanObjects[i].used ||
            gHostCzanObjects[i].groupHandle != sourceObjectGroupHandle) {
            continue;
        }

        objectIndex = HostCzan_FindFreeObject();
        if (objectIndex < 0) {
            break;
        }

        cloneObject = &gHostCzanObjects[objectIndex];
        *cloneObject = gHostCzanObjects[i];
        cloneObject->groupHandle = groupHandle;
        cloneObject->textureFrameSet148 = 0;
        if (initialAnimIndex >= 0) {
            cloneObject->currentAnimation = initialAnimIndex;
            cloneObject->animationCommandOffset = HostCzan_GetAnimationCommandOffset(cloneObject, initialAnimIndex);
            /* 0x80173B9C clone path mirrors the create flag switch. */
            /* 0x80174158..: the clone reads the descriptor flags like the create path
               (0x8017399C), including 0x04 = start animation 0. */
            HostCzan_ApplyCreateFlags(cloneObject, cloneFlags & 0xffu, initialAnimIndex,
                                      cloneObject->metadata != 0 &&
                                      cloneObject->descriptorOffset + 0x20u <= cloneObject->metadataSize ?
                                      ReadBe16(cloneObject->metadata + cloneObject->descriptorOffset + 0x1A) : 0u);
        }

        if (cloneGroup->firstObjectIndex < 0) {
            cloneGroup->firstObjectIndex = objectIndex;
        }
    }

    printf("CzanUiManager: cloned group %d from %d children=%d\n",
           groupHandle,
           sourceObjectGroupHandle,
           cloneGroup->childCount);
    return groupHandle;
}

void CzanUiObjectInstance_StartAnimation(double startFrame, int objectInstance, int animationIndex) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80172CC8 selects/starts an animation entry on a Czan UI object instance.
       startFrame must be >= 0.0. objectInstance is the 0x1B4 Czan UI object instance.
       animationIndex is stored at +0x16C and used to index descriptor +0x1C. */
    if (instance == 0 || startFrame < 0.0) {
        return;
    }

    instance->initialAnimIndex = animationIndex;
    instance->currentAnimValue = 0;
}

void CzanUiManager_SetObjectGroupAnimationMode(int uiManager, int objectGroupHandle, unsigned char mode) {
    int i;

    /* 0x80174FE0 writes object +0x175 for every child object in a group. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].animationMode175 = mode;
        }
    }
}

void CzanUiManager_StartObjectGroupAnimation(double startFrame, int uiManager, int objectGroupHandle, int animationIndex) {
    int i;

    /* 0x80174E2C calls CzanUiObjectInstance_StartAnimation(startFrame, child,
       animationIndex) for every child object in the group, then marks uiManager
       +0x18 dirty when uiManager +0x1A is zero. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            int sameActiveAnimation =
                gHostCzanObjects[i].currentAnimation == animationIndex &&
                gHostCzanObjects[i].animationStartFrame == (float)startFrame &&
                gHostCzanObjects[i].animationCommandOffset != 0 &&
                gHostCzanObjects[i].animationPlaying171 != 0 &&
                gHostCzanObjects[i].animationDoneB1 == 0 &&
                gHostCzanObjects[i].animationDoneB2 == 0;

            if (sameActiveAnimation) {
                continue;
            }

            HostCzan_StartAnimationState(&gHostCzanObjects[i], animationIndex);
            gHostCzanObjects[i].animationStartFrame = (float)startFrame;
        }
    }
}

void CzanUiManager_SetObjectGroupAnimationResetMode(int uiManager, int objectGroupHandle, unsigned char resetMode) {
    int i;

    /* 0x80174F60 writes object +0x174 and clears object +0xB1 for every child in
       a group. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].animationReset174 = resetMode;
            gHostCzanObjects[i].animationDoneB1 = 0;
        }
    }
}

int CzanUiManager_IsObjectGroupAnimationDone(int uiManager, int objectGroupHandle) {
    int i;
    int found = 0;
    int active = 0;

    /* FUN_80175E00 returns nonzero when any child in the group has the selected
       animation-done byte set. Animation scripts advance from draw/update paths,
       not from the predicate itself. Advancing here made boot/title CAE timelines
       finish too early whenever state code polled them. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            found = 1;
            if ((gHostCzanObjects[i].animationReset174 == 0 && gHostCzanObjects[i].animationDoneB1) ||
                (gHostCzanObjects[i].animationReset174 != 0 && gHostCzanObjects[i].animationDoneB2)) {
                return 1;
            }
            if (gHostCzanObjects[i].animationCommandOffset != 0) {
                active = 1;
            }
        }
    }
    if (found != 0 && active == 0) {
        return 1;
    }
    return 0;
}

double CzanUiManager_GetObjectAnimationDuration(double fallbackDuration, int uiManager, int objectGroupHandle, int childObjectIndex, int animationIndex) {
    /* 0x80175F58 returns the duration/tick count for one Czan UI object's animation.

       Original flow:
       - object = *(uiManager +4 + objectGroupHandle * 0x28 +0x20)[childObjectIndex]
       - if animationIndex == -1, use object +0x16C
       - return animation entry duration at *(object +0x198 +0x1C) + animationIndex * 0x10 +8

       The fallbackDuration argument is the decompiler-visible FPR argument used by
       callers when the UI object/group is unavailable. */
    int i;

    (void)uiManager;
    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            return HostCzan_GetObjectAnimationDuration(&gHostCzanObjects[i], animationIndex, fallbackDuration);
        }
    }
    return fallbackDuration;
}

void CzanUiManager_ResetObjectGroupAnimationTime(double frame, int uiManager, int objectGroupHandle) {
    /* 0x80174FA4 writes frame to +0x178 on every child CzanUiObjectInstance in an
       object group. CSelModeEntry_ResetObjectAnimation uses this before starting
       the movie-backed entry reveal animation. */
    (void)frame;
    (void)uiManager;
    (void)objectGroupHandle;
}

int CzanUiManager_GetChildObjectInstance(int uiManager, int objectGroupHandle, int childObjectIndex) {
    /* 0x801761C4 resolves one child CzanUiObjectInstance from an object group.

       Original behavior:
       return *( *( *(uiManager +4) + objectGroupHandle*0x28 +0x20 ) + childObjectIndex*4 )

       It is used by the select-common movie reveal path after getting a
       CSelModeEntry object handle, then the returned child instance receives color
       transition data. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
    return 0;
}

void CzanUiObjectInstance_PreplayInitialAnimation(int objectInstance) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x801728E4 temporarily forces object +0x175 to mode 3, starts the selected
       animation at frame 0, sets +0xB4 to 0, then runs the animation script the old
       +0xB4 value number of times. It restores +0x175 and resets the script pointer
       and playback state back to the selected animation entry. */
    if (instance == 0) {
        return;
    }
}

void CzanSpriteObject_SetRenderMode(int spriteObject, int mode) {
    CzanSpriteObjectKnownFields *sprite = (CzanSpriteObjectKnownFields *)spriteObject;

    /* 0x80170DEC maps an animation-entry mode byte to sprite render/blend state.
       spriteObject is the 0x1D8 Czan sprite object. mode is the byte copied from
       animation entry +0x03. The original writes render parameters around
       +0x178..+0x19C, then stores the raw mode byte at +0x1A4. */
    if (sprite == 0) {
        return;
    }

    (void)mode;
}

void CzanUiObjectInstance_RunAnimationScript(int objectInstance, int allowUnknownOpcode) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80171B98 interprets the Czan animation command stream at object +0x19C.
       objectInstance is the 0x1B4 Czan UI object instance, passed through the compiler's
       context helper in the decompile. allowUnknownOpcode controls the invalid-opcode assert
       path: nonzero tolerates unknown/default opcodes, zero asserts.

       Confirmed opcodes update playback/end state, texture frames, sprite position,
       size, scale, render mode, color bytes, dynamic value lists, and wait timers.
       The interpreter loops until it reaches a wait/end condition. */
    if (instance == 0) {
        return;
    }

    (void)allowUnknownOpcode;
}

void CzanUiObjectInstance_ApplyColorBlocks(int objectInstance) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80172FC0 copies four color blocks from the UI object instance into the
       attached sprite object's vertex/RGBA color blocks. objectInstance is the 0x1B4
       Czan UI object instance. Flags at +0x182/+0x183 select alternate RGB and
       scaled alpha behavior. */
    if (instance == 0) {
        return;
    }
}

void CzanUiObjectInstance_SetColorBlocks(int objectInstance, int colorSlot, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    /* 0x80172F30 writes RGBA color bytes to one or all four color blocks on a
       CzanUiObjectInstance, marks color state dirty at +0x182/+0x183, then calls
       CzanUiObjectInstance_ApplyColorBlocks.

       colorSlot == -1 writes the same RGBA to all four blocks at +0x134..+0x143.
       Otherwise it writes only the selected 4-byte block. */
    (void)colorSlot;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    if (objectInstance == 0) {
        return;
    }
}

void CzanUiManager_SetObjectTextureFrame(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int textureFrameOrAuto,
    int updateSpriteDimensions) {
    /* 0x80175448 selects/rebinds a texture frame on one child object.
       It stores the requested frame at object +0x148, resolves -2 through
       object +0xA8/+0x14C, updates sprite +0x34, and optionally refreshes
       sprite dimensions at +0x100/+0x108 and half sizes at +0x78/+0x7C. */
    int i;

    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].textureFrameSet148 = textureFrameOrAuto != -2;
            gHostCzanObjects[i].textureFrame148 = textureFrameOrAuto;
            if (textureFrameOrAuto == -2) {
                return;
            }
            gHostCzanObjects[i].textureIndex = textureFrameOrAuto < 0 ? 0 : textureFrameOrAuto;
            if (gHostCzanObjects[i].textureSlot >= 0) {
                GetTextureDimensions(
                    (void *)(long)gHostCzanObjects[i].textureSlot,
                    gHostCzanObjects[i].textureIndex,
                    &gHostCzanObjects[i].width,
                    &gHostCzanObjects[i].height);
                if (updateSpriteDimensions != 0) {
                    HostCzan_SetDefaultSpriteCenter(&gHostCzanObjects[i]);
                }
            }
            return;
        }
    }
}

void CzanUiManager_GetChildObjectDimensions(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *outWidth,
    float *outHeight) {
    int i;

    /* 0x801760EC returns a child object's dimensions. The caller in 0x800DA01C
       stores them as floats and normalizes negative values. */
    (void)uiManager;

    if (outWidth != 0) {
        *outWidth = 0.0f;
    }
    if (outHeight != 0) {
        *outHeight = 0.0f;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            if (outWidth != 0) {
                *outWidth = (float)gHostCzanObjects[i].width;
            }
            if (outHeight != 0) {
                *outHeight = (float)gHostCzanObjects[i].height;
            }
            return;
        }
    }
}

void CzanUiManager_ApplyObjectGroupPositionLayout(int uiManager, int objectGroupHandle, float *xyOffset) {
    /* 0x801750E4 applies xyOffset to every child in a group:
       object +0xD0/+0xD4 = offset, sprite +0x3C/+0x40 = object +0x30/+0x34 + offset. */
    int i;

    (void)uiManager;
    if (xyOffset == 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].positionOffsetX = xyOffset[0];
            gHostCzanObjects[i].positionOffsetY = xyOffset[1];
            HostCzan_UpdateObjectPosition(&gHostCzanObjects[i]);
        }
    }
}

void CzanUiManager_ApplyChildObjectPositionLayout(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *xyOffset) {
    /* 0x8017515C is the single-child version of CzanUiManager_ApplyObjectGroupPositionLayout. */
    int i;

    (void)uiManager;
    if (xyOffset == 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].positionOffsetX = xyOffset[0];
            gHostCzanObjects[i].positionOffsetY = xyOffset[1];
            HostCzan_UpdateObjectPosition(&gHostCzanObjects[i]);
            return;
        }
    }
}

void CzanUiManager_ApplyObjectGroupAnimationOffset(int uiManager, int objectGroupHandle, float *xyOffset) {
    /* 0x801751B8 applies xyOffset to every child in a group:
       object +0xE8/+0xEC = offset, sprite +0x90/+0x94 =
       object scale +0xF4/+0xF8 * (object +0x48/+0x4C + offset). */
    int i;

    (void)uiManager;
    if (xyOffset == 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].animationOffsetX = xyOffset[0];
            gHostCzanObjects[i].animationOffsetY = xyOffset[1];
            HostCzan_UpdateObjectScale(&gHostCzanObjects[i]);
        }
    }
}

void CzanUiManager_ApplyChildObjectAnimationOffset(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *xyOffset) {
    /* 0x80175240 is the single-child version of CzanUiManager_ApplyObjectGroupAnimationOffset. */
    int i;

    (void)uiManager;
    if (xyOffset == 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].animationOffsetX = xyOffset[0];
            gHostCzanObjects[i].animationOffsetY = xyOffset[1];
            HostCzan_UpdateObjectScale(&gHostCzanObjects[i]);
            return;
        }
    }
}

void CzanUiManager_SetObjectGroupEnabled(int uiManager, int objectGroupHandle, unsigned char enabled) {
    int i;

    /* 0x80174F04 writes object +0x173 for every child in the group. Despite the
       old host name, this is not a normal visible=true flag: CzanUiObjectInstance_Draw
       skips the draw path when +0x173 is nonzero. The game passes 0 here when it
       wants a group to become drawable for an animation. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].suppressDraw173 = enabled != 0;
        }
    }
}

void CzanUiManager_SetObjectGroupDisplayFlags(
    int uiManager,
    int objectGroupHandle,
    signed char suppressDraw,
    unsigned char drawState) {
    int i;

    /* 0x80174EB8 writes object +0x172 for every child, and writes +0x173 when
       suppressDraw is not -1. FUN_8012953C uses this as the final hide step for
       the select-bin warning/timeline object. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].drawState172 = drawState;
            if (suppressDraw != -1) {
                gHostCzanObjects[i].suppressDraw173 = suppressDraw != 0;
            }
        }
    }
}

void CzanUiManager_SetChildObjectEnabled(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    unsigned char enabled) {
    int i;

    /* 0x80174F40 writes object +0x173 for one child in the group. Nonzero means
       the real draw wrapper suppresses the object. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].suppressDraw173 = enabled != 0;
            return;
        }
    }
}

void CzanUiManager_SetChildObjectLinkedHandle(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int linkedHandle) {
    int i;

    /* 0x80176E34 writes object +0x188 for one child object in a group. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].linkedHandle188 = linkedHandle;
            return;
        }
    }
}

void CzanUiManager_SetChildObjectExtensionPointer(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int extensionPointer,
    int releaseExisting,
    int extensionSlot) {
    int i;

    /* 0x80175CC4 attaches a helper object to a Czan child-object extension slot
       and stores the small ordering/release flag at object +0x1A0. The host keeps
       the pointer bits beside the cached object so the boot prompt/effect helpers
       can be driven from the same child group and child index as the retail UI. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            if (extensionPointer == 0) {
                gHostCzanObjects[i].extensionHelperBits = 0;
                gHostCzanObjects[i].extensionHelperReleaseFlag = 0;
                gHostCzanObjects[i].extensionHelperSlot = extensionSlot;
            }
            else {
                gHostCzanObjects[i].extensionHelperBits = extensionPointer;
                gHostCzanObjects[i].extensionHelperReleaseFlag = releaseExisting;
                gHostCzanObjects[i].extensionHelperSlot = extensionSlot;
            }
            return;
        }
    }
}

void CzanUiManager_SetObjectGroupPriority(int uiManager, int objectGroupHandle, int priority) {
    int i;

    /* FUN_8017576C writes object +0x168 for every child in the group. The real UI
       list uses this as a sort/order key. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].drawPriority168 = priority;
        }
    }
}

static int HostCzan_GetObjectReferenceEdge(const HostCzanObject *object) {
    if (object == 0) {
        return 0;
    }
    return (object->referenceEdgeActive184 ? object->referenceEdge164 : object->referenceEdge160) +
           object->drawPriority168;
}

void CzanUiManager_AlignObjectGroupByReferenceEdge(int uiManager, int objectGroupHandle, int referenceEdge, int alignToMax) {
    int i;
    int found = 0;
    int selectedEdge = 0;

    /* FUN_8017559C first scans the group using each child object's active
       reference edge (+0x164 when +0x184 is set, otherwise +0x160) plus group
       priority/sort offset (+0x168). It then writes object +0x164 for every child
       and marks +0x184 active. param_4 selects min-edge or max-edge alignment. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            int edge = HostCzan_GetObjectReferenceEdge(&gHostCzanObjects[i]);
            if (!found || (alignToMax == 0 ? edge < selectedEdge : edge > selectedEdge)) {
                selectedEdge = edge;
            }
            found = 1;
        }
    }
    if (!found) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            int edge = HostCzan_GetObjectReferenceEdge(&gHostCzanObjects[i]);
            if (alignToMax == 0) {
                gHostCzanObjects[i].referenceEdge164 = referenceEdge + (edge - selectedEdge);
            }
            else {
                gHostCzanObjects[i].referenceEdge164 = referenceEdge - (selectedEdge - edge);
            }
            gHostCzanObjects[i].referenceEdgeActive184 = 1;
        }
    }
}

void CzanUiManager_SetChildObjectReferenceEdge(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int referenceEdge) {
    int i;

    /* FUN_80175744 writes object +0x164 and marks +0x184 active for one child. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].referenceEdge164 = referenceEdge;
            gHostCzanObjects[i].referenceEdgeActive184 = 1;
            return;
        }
    }
}

void CzanUiManager_SetObjectGroupReferenceEdgeActive(int uiManager, int objectGroupHandle, unsigned char active) {
    int i;

    /* FUN_801757A8 writes object +0x184 for every child in the group. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].referenceEdgeActive184 = active != 0;
        }
    }
}

void CzanUiManager_SetChildObjectReferenceEdgeActive(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    unsigned char active) {
    int i;

    /* FUN_801757E4 writes object +0x184 for one child. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].referenceEdgeActive184 = active != 0;
            return;
        }
    }
}

void CzanUiManager_ApplyChildObjectQuadUv(int uiManager, int objectGroupHandle, int childObjectIndex, const float *quadUv) {
    int i;

    /* FUN_80175998 writes a four-word per-child sprite block:
       sprite +0xC8/+0xCC/+0xD0/+0xD4 = quadUv[0..3]. The select/common and
       UiRoot layout helpers use it for per-child normalized quad/UV data. */
    (void)uiManager;

    if (quadUv == 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].uvBaseX = quadUv[0];
            gHostCzanObjects[i].uvBaseY = quadUv[1];
            gHostCzanObjects[i].uvSpanX = quadUv[2] - quadUv[0];
            gHostCzanObjects[i].uvSpanY = quadUv[3] - quadUv[1];
            return;
        }
    }
}

int CzanUiManager_GetChildObjectReferenceEdge(int uiManager, int objectGroupHandle, int childObjectIndex) {
    int i;

    /* FUN_80175804 returns the active child reference edge plus object +0x168. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            return HostCzan_GetObjectReferenceEdge(&gHostCzanObjects[i]);
        }
    }
    return 0;
}

void CzanUiManager_SetObjectGroupDrawEnabled(int uiManager, int objectGroupHandle, unsigned char drawEnabled) {
    int i;

    /* 0x801750A8 sets object +0x181 for every child in the group. This is the
       draw-list participation flag checked by CzanUiManager_DrawObjectListReverse. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].drawEnabled = drawEnabled != 0;
        }
    }
}

void CzanUiManager_LinkObjectGroupToReferenceObject(
    int uiManager,
    int targetObjectGroupHandle,
    int referenceObjectGroupHandle,
    int referenceChildIndex,
    unsigned char linkMode) {
    int i;

    /* 0x80176D68 links every target child to one reference object:
       target object +0x190 = reference object, target sprite +0x1B0 =
       reference sprite, target sprite +0x1B4 = linkMode. The draw path resolves
       the referenced object each frame rather than baking its current transform. */
    (void)uiManager;

    if (HostCzan_FindObjectIndexByGroupChild(referenceObjectGroupHandle, referenceChildIndex) < 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == targetObjectGroupHandle) {
            gHostCzanObjects[i].linkedHandle188 = ((referenceObjectGroupHandle & 0xffff) << 16) |
                                                  ((referenceChildIndex & 0xff) << 8) |
                                                  linkMode;
        }
    }
}

void CzanSpriteObject_Draw(int spriteObject, int parentTransform, int externalTransform, int drawMode) {
    /* 0x80170354 is the high-level Czan sprite draw dispatcher. It checks sprite
       active/texture/dimension state, applies render state, selects a draw mode,
       then calls a low-level quad emitter such as CzanDrawTexturedOrColoredQuad. */
    (void)spriteObject;
    (void)parentTransform;
    (void)externalTransform;
    (void)drawMode;
}

void CzanUiObjectInstance_Draw(int objectInstance) {
    /* 0x801729B4 is the per-object Czan draw wrapper. It can run up to eight
       child/pre-post callbacks, resolves linked sprite state at object +0x28,
       writes sprite +0x1A8, and calls CzanSpriteObject_Draw(object +0x24, ...).
       It uses object +0x173 as enabled/visibility, +0x198 as descriptor,
       +0x16C as animation index, +0x148 as texture-frame override, and +0x1A4
       as the owning object-group handle. */
    (void)objectInstance;
}

void CzanUiManager_DrawObjectListReverse(int objectList, int drawLayerFilter) {
    int i;
    int objectIndexes[HOST_CZAN_MAX_OBJECTS];
    int objectCount = 0;

    /* 0x80174BA8 draws an object list from last to first. It skips when list
       +0x19 is set or +0x1C is null, draws only objects with +0x181 == 1, and
       filters by object +0x18C unless drawLayerFilter is -1. */
    (void)objectList;
    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].listedForDraw &&
            (drawLayerFilter == -1 || gHostCzanObjects[i].drawLayer18c == drawLayerFilter)) {
            objectIndexes[objectCount++] = i;
        }
    }

    HostCzan_SortObjectIndexesForRetailList(objectIndexes, objectCount);
    for (i = objectCount - 1; i >= 0; i--) {
        HostCzan_DrawObject(&gHostCzanObjects[objectIndexes[i]]);
    }
}

void CzanUiManager_UpdateObjectList(int uiManager) {
    int i;

    /* 0x801745B8 updates/sorts the global Czan UI object list before draw. The
       host has a flat object cache instead of the original list nodes, but the
       important recovered behavior for boot/select is that CAE scripts advance in
       the UI-root update phase, not in the draw traversal. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used) {
            gHostCzanObjects[i].listedForDraw = 0;
            if (gHostCzanObjects[i].animationPlaying171 && gHostCzanObjects[i].animationDoneB1 == 0) {
                /* 0x80171678: run one script tick per whole frame the clock crosses. */
                HostCzanObject *object = &gHostCzanObjects[i];
                float previous = object->animationClockB4;
                int ticks;

                object->animationClockB4 += HostCzan_GetAnimationTickStep(object);
                ticks = (int)object->animationClockB4 - (int)previous;
                while (ticks-- > 0 && object->animationDoneB1 == 0) {
                    HostCzan_RunAnimationScript(object, 1);
                }
            }
            if (HostCzan_IsObjectDrawableForRetailList(&gHostCzanObjects[i])) {
                gHostCzanObjects[i].listedForDraw = 1;
            }
        }
    }
}

void CzanUiManager_SetObjectGroupColorBlocks(int uiManager, int objectGroupHandle, const unsigned char *rgba) {
    /* 0x80175B00 -> 0x80172F30(child, -1 = all corners, r, g, b, a) for each child. */
    int i;

    (void)uiManager;
    if (rgba == 0) {
        return;
    }
    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            memcpy(gHostCzanObjects[i].blockColor, rgba, 4);
        }
    }
}

void CzanUiManager_SeekObjectGroupAnimationToEnd(int uiManager, int objectGroupHandle) {
    /* 0x80175F58 (animation length) + 0x8017501C(length) -> 0x80172E44 for each child:
       jump the running animation to its last frame. */
    int i;

    (void)uiManager;
    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        HostCzanObject *object = &gHostCzanObjects[i];
        int frames;
        int guard;

        if (!object->used || object->groupHandle != objectGroupHandle || !object->animationPlaying171) {
            continue;
        }
        frames = (int)HostCzan_GetObjectAnimationDuration(object, object->currentAnimation, 0.0);
        for (guard = 0; guard <= frames + 1 && object->animationDoneB1 == 0; guard++) {
            HostCzan_RunAnimationScript(object, 1);
        }
    }
}

void CzanUiManager_DebugDumpGroup(int objectGroupHandle, const char *tag) {
    /* Host debug aid: one line per child object of a group. */
    int i;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        const HostCzanObject *o = &gHostCzanObjects[i];

        if (!o->used || o->groupHandle != objectGroupHandle) {
            continue;
        }
        RuntimeDebugReport(
            "  [%s] g%d c%d '%s' listed=%d play=%d b0=%d en=%d s172=%d s173=%d anim=%d mode=%d wait=%d doneB1=%d doneB2=%d "
            "dur=%.0f rgba=%02x%02x%02x%02x xy=(%.0f,%.0f) wh=%dx%d tex=%d prio=%d z=%.1f+%.1f sc=%.2f,%.2f "
            "uv=%.2f,%.2f+%.2f,%.2f rmode=%d dtype=%d dflags=0x%x key=%d\n",
            tag, objectGroupHandle, o->childIndex, o->name, o->listedForDraw, o->animationPlaying171,
            o->presented0b0, o->drawEnabled,
            o->drawState172, o->suppressDraw173, o->currentAnimation, o->animationMode175,
            o->animationWaitTicks, o->animationDoneB1, o->animationDoneB2,
            HostCzan_GetObjectAnimationDuration(o, o->currentAnimation, 0.0),
            o->color[0], o->color[1], o->color[2], o->color[3], o->x, o->y, o->width, o->height,
            o->textureSlot, o->drawPriority168, o->depth, o->localOffsetZ, o->scaleX, o->scaleY,
            o->uvBaseX + o->uvOffsetX, o->uvBaseY + o->uvOffsetY, o->uvSpanX, o->uvSpanY, o->spriteRenderMode, o->descriptorType,
            o->metadata != 0 ? (unsigned int)ReadBe16(o->metadata + o->descriptorOffset + 0x1a) : 0u,
            HostCzan_GetObjectReferenceEdge(o));
    }
}

void CzanUiManager_SetObjectGroupVertexAlpha(int uiManager, int objectGroupHandle, unsigned char alpha) {
    /* 0x8017539C writes the four corner alpha bytes (+0x137/+0x13B/+0x13F/+0x143)
       of every child; the host keeps one multiplier in blockColor[3]. */
    int i;

    (void)uiManager;
    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].blockColor[3] = alpha;
        }
    }
}

int CzanUiManager_IsChildObjectHidden(int uiManager, int objectGroupHandle, int childObjectIndex) {
    /* 0x80175DE0 returns child +0x173. */
    int index = HostCzan_FindObjectIndexByGroupChild(objectGroupHandle, childObjectIndex);

    (void)uiManager;
    return index >= 0 ? gHostCzanObjects[index].suppressDraw173 : 1;
}

int CzanUiManager_HitTestChildObject(int uiManager, int objectGroupHandle, int childObjectIndex, float x, float y) {
    /* 0x8017620C -> 0x801709D0: point inside the child's screen quad. The host quad
       matches HostCzan_DrawObject (linked position, base, scale, rotation z). */
    const HostCzanObject *object;
    float drawX;
    float drawY;
    float lx;
    float ly;
    float radians;
    float c;
    float sn;
    float rx;
    float ry;
    float w;
    float h;
    int index = HostCzan_FindObjectIndexByGroupChild(objectGroupHandle, childObjectIndex);

    (void)uiManager;
    if (index < 0) {
        return 0;
    }
    object = &gHostCzanObjects[index];
    HostCzan_GetObjectLinkedPosition(object, &drawX, &drawY);
    lx = x - drawX;
    ly = y - drawY;
    radians = -object->rotationZ * (3.14159265358979323846f / 180.0f);
    c = cosf(radians);
    sn = sinf(radians);
    rx = lx * c - ly * sn;
    ry = lx * sn + ly * c;
    w = (float)object->width * object->scaleX;
    h = (float)object->height * object->scaleY;
    rx += object->baseX * object->scaleX;
    ry += object->baseY * object->scaleY;
    if (w < 0.0f) {
        rx = -rx;
        w = -w;
    }
    if (h < 0.0f) {
        ry = -ry;
        h = -h;
    }
    return rx >= 0.0f && ry >= 0.0f && rx <= w && ry <= h;
}

int CzanUiManager_GetChildObjectScreenTransform(
    int uiManager, int objectGroupHandle, int childObjectIndex,
    float *x, float *y, float *scaleX, float *scaleY, unsigned char *rgba) {
    /* Host stand-in for the sprite world matrix the text callback uses (0x801708F0):
       linked position, inherited x/y scale and colour with inherited alpha. */
    const HostCzanObject *object;
    float inheritedAlpha;
    int index = HostCzan_FindObjectIndexByGroupChild(objectGroupHandle, childObjectIndex);

    (void)uiManager;
    if (index < 0) {
        return 0;
    }
    object = &gHostCzanObjects[index];
    HostCzan_ResolveLinkedTransform(object, x, y, scaleX, scaleY, &inheritedAlpha);
    *scaleX *= object->scaleX;
    *scaleY *= object->scaleY;
    rgba[0] = object->color[0];
    rgba[1] = object->color[1];
    rgba[2] = object->color[2];
    rgba[3] = (unsigned char)((float)object->color[3] * inheritedAlpha + 0.5f);
    return 1;
}

static void HostCzan_DrawExtensionHelpers(const int *objectGroupHandles, int objectGroupCount) {
    /* Object extension slots (text helpers) run from the object update in the DOL
       (0x80171B30), even for unpresented anchor dummies; the host draws them after
       the scoped object pass so the text sits on top of its panel. */
    int i;
    int j;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (!gHostCzanObjects[i].used ||
            gHostCzanObjects[i].extensionHelperBits == 0) {
            continue;
        }
        for (j = 0; j < objectGroupCount; j++) {
            if (objectGroupHandles[j] >= 0 && gHostCzanObjects[i].groupHandle == objectGroupHandles[j]) {
                UiPromptEffectHelper_DrawByBits(gHostCzanObjects[i].extensionHelperBits);
                break;
            }
        }
    }
}

void CzanUiManager_DrawObjectGroupsReverse(int uiManager, const int *objectGroupHandles, int objectGroupCount) {
    int i;
    int objectIndexes[HOST_CZAN_MAX_OBJECTS];
    int objectCount = 0;

    /* Host-scoped equivalent of the global reverse object-list draw. The retail
       list contains only the objects admitted by the current UI state. The host
       keeps all decoded groups in one flat cache, so title/select bridges pass the
       groups owned by the recovered state constructor to avoid drawing resident
       select/music/comAF groups out of order. */
    (void)uiManager;

    if (objectGroupHandles == 0 || objectGroupCount <= 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        int j;
        int allowed = 0;

        if (!gHostCzanObjects[i].used ||
            !gHostCzanObjects[i].listedForDraw) {
            continue;
        }

        for (j = 0; j < objectGroupCount; j++) {
            if (objectGroupHandles[j] >= 0 &&
                gHostCzanObjects[i].groupHandle == objectGroupHandles[j]) {
                allowed = 1;
                break;
            }
        }

        if (allowed && objectCount < HOST_CZAN_MAX_OBJECTS) {
            objectIndexes[objectCount++] = i;
        }
    }

    HostCzan_SortObjectIndexesForRetailList(objectIndexes, objectCount);
    {
        /* Debug aid: DDRII_TRACE_DRAW=n lists the objects of the n-th scoped draw. */
        static int drawCall;
        static int traceAt = -2;
        const char *value;

        if (traceAt == -2) {
            value = getenv("DDRII_TRACE_DRAW");
            traceAt = value != 0 ? atoi(value) : -1;
        }
        if (++drawCall == traceAt) {
            for (i = objectCount - 1; i >= 0; i--) {
                const HostCzanObject *o = &gHostCzanObjects[objectIndexes[i]];

                RuntimeDebugReport("DRAW g%d c%d '%s' xy=(%.0f,%.0f) wh=%dx%d rgba=%02x%02x%02x%02x key=%d tex=%d ext=%d\n",
                                   o->groupHandle, o->childIndex, o->name, o->x, o->y, o->width, o->height,
                                   o->color[0], o->color[1], o->color[2], o->color[3],
                                   HostCzan_GetObjectReferenceEdge(o), o->textureSlot, o->extensionHelperBits);
            }
        }
    }
    for (i = objectCount - 1; i >= 0; i--) {
        HostCzan_DrawObject(&gHostCzanObjects[objectIndexes[i]]);
    }
    HostCzan_DrawExtensionHelpers(objectGroupHandles, objectGroupCount);
}

void CSelMode_DrawHostUi(void) {
    int entryIndex;
    int slot;
    int objectIndexes[HOST_CZAN_MAX_OBJECTS];
    int objectCount = 0;
    int i;

    /* Host bridge for the current CSelMode renderer. The real setup stores object
       group handles in fourteen CSelModeEntry records; drawing the global list here
       made unrelated resident select/comAF groups visible. */
    if (gHostCSelModeActiveEntries == 0) {
        return;
    }

    for (entryIndex = 0; entryIndex < CSEL_MODE_ENTRY_COUNT; entryIndex++) {
        CSelModeEntryKnownFields *entry = &gHostCSelModeActiveEntries[entryIndex];
        for (slot = 0; slot < entry->objectHandleCount && slot < 4; slot++) {
            int groupHandle = entry->objectHandles[slot];
            int objectIndex;
            if (groupHandle < 0) {
                continue;
            }
            for (objectIndex = 0; objectIndex < HOST_CZAN_MAX_OBJECTS; objectIndex++) {
                if (gHostCzanObjects[objectIndex].used &&
                    gHostCzanObjects[objectIndex].groupHandle == groupHandle &&
                    objectCount < HOST_CZAN_MAX_OBJECTS) {
                    objectIndexes[objectCount++] = objectIndex;
                }
            }
        }
    }

    HostCzan_SortObjectIndexesForRetailList(objectIndexes, objectCount);

    for (i = objectCount - 1; i >= 0; i--) {
        HostCzanObject *object = &gHostCzanObjects[objectIndexes[i]];
        if (object->listedForDraw) {
            HostCzan_DrawObject(object);
        }
    }
}

void CzanUiManager_DrawObjectGroupInListOrder(int uiManager, int objectGroupHandle) {
    int i;
    int objectIndexes[HOST_CZAN_MAX_OBJECTS];
    int objectCount = 0;

    /* 0x80174C58 draws only children belonging to one object group, while
       preserving the global object-list reverse order from uiManager +0x1C.
       The group is uiManager +0x04 + objectGroupHandle * 0x28. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].listedForDraw) {
            objectIndexes[objectCount++] = i;
        }
    }

    HostCzan_SortObjectIndexesForRetailList(objectIndexes, objectCount);
    for (i = objectCount - 1; i >= 0; i--) {
        HostCzan_DrawObject(&gHostCzanObjects[objectIndexes[i]]);
    }
}

void CzanUiManager_DrawChildObject(int uiManager, int objectGroupHandle, int childObjectIndex) {
    int i;

    /* 0x80174CF8 directly draws one child:
       uiManager->groups[objectGroupHandle].children[childObjectIndex]. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex &&
            gHostCzanObjects[i].listedForDraw) {
            HostCzan_DrawObject(&gHostCzanObjects[i]);
            return;
        }
    }
}

void CzanDrawTexturedOrColoredQuad(
    double u0,
    double v0,
    double u1,
    double v1,
    int spriteObject,
    void *quadData,
    int width,
    unsigned int height,
    unsigned char *vertexColors,
    int textureObject,
    int unknownArg11,
    int unknownArg12) {
    /* 0x8016BE18 is the low-level GX quad emitter. textureObject == 0 emits a
       colored quad; nonzero loads a GX texture object and emits textured vertices. */
    (void)u0;
    (void)v0;
    (void)u1;
    (void)v1;
    (void)spriteObject;
    (void)quadData;
    (void)width;
    (void)height;
    (void)vertexColors;
    (void)textureObject;
    (void)unknownArg11;
    (void)unknownArg12;
}
