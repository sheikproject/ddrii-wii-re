#include "model/czan_model.h"

#include "render/render_engine.h"
#include "resource/czan_link.h"
#include "runtime/math.h"
#include "runtime/module_system.h"
#include "ui/czan_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CZAN_MODEL_HOST_STATE_CAP 32
#define CZAN_MODEL_HOST_CONTINUATION_CAP 16
#define CZAN_MODEL_HOST_DRAW_CACHE_CAP 4
#define CZAN_MODEL_HOST_DRAW_VERTEX_CAP 32768
#define CZAN_MODEL_HOST_DRAW_PRIMITIVE_CAP 8192
#define CZAN_MODEL_HOST_DRAW_BATCH_VERTEX_CAP (CZAN_MODEL_HOST_DRAW_VERTEX_CAP * 3)

typedef struct CzanModelHostState {
    int *model;
    void *primaryBlock;
    unsigned int primaryBlockSize;
    void *continuations[CZAN_MODEL_HOST_CONTINUATION_CAP];
    int continuationCount;
} CzanModelHostState;

typedef struct CzanModelOwnerHostState {
    int *owner;
    int *model;
    int lastAnimationUpdateFrameToken;
} CzanModelOwnerHostState;

typedef struct CzanModelHostDrawCache {
    int *model;
    void *primaryBlock;
    unsigned int primaryBlockSize;
    void *zabBlock;
    unsigned int zabBlockSize;
    int textureSlot;
    int drawMode;
    float animationTick;
    unsigned int vertexCount;
    unsigned int primitiveCount;
    float vertices[CZAN_MODEL_HOST_DRAW_VERTEX_CAP][3];
    float texcoords[CZAN_MODEL_HOST_DRAW_VERTEX_CAP][2];
    unsigned int colors[CZAN_MODEL_HOST_DRAW_VERTEX_CAP];
    unsigned int vertexObjectIndex[CZAN_MODEL_HOST_DRAW_VERTEX_CAP];
    unsigned int primitiveStart[CZAN_MODEL_HOST_DRAW_PRIMITIVE_CAP];
    unsigned int primitiveVertexCount[CZAN_MODEL_HOST_DRAW_PRIMITIVE_CAP];
    unsigned int primitiveTextureIndex[CZAN_MODEL_HOST_DRAW_PRIMITIVE_CAP];
    unsigned char primitiveMaterialMode[CZAN_MODEL_HOST_DRAW_PRIMITIVE_CAP];
} CzanModelHostDrawCache;

static CzanModelHostState gCzanModelHostStates[CZAN_MODEL_HOST_STATE_CAP];
static CzanModelOwnerHostState gCzanModelOwnerHostStates[CZAN_MODEL_HOST_STATE_CAP];
static CzanModelHostDrawCache gCzanModelHostDrawCaches[CZAN_MODEL_HOST_DRAW_CACHE_CAP];
static unsigned int gCzanModelHostDrawCacheCursor;
static int gCzanModelHostBatchPoints[CZAN_MODEL_HOST_DRAW_BATCH_VERTEX_CAP][2];
static float gCzanModelHostBatchTexcoords[CZAN_MODEL_HOST_DRAW_BATCH_VERTEX_CAP][2];
static unsigned int gCzanModelHostBatchColors[CZAN_MODEL_HOST_DRAW_BATCH_VERTEX_CAP];
static int gCzanModelHostProjectionActive;
static float gCzanModelHostProjectionFovDeg = 45.0f;
static float gCzanModelHostProjectionAspect = 1.333333373f;
static float gCzanModelHostProjectionNear = 1.0f;
static float gCzanModelHostProjectionFar = 10000.0f;

static float CzanModelProjection_ConvertHorizontalFov(float fovDegrees) {
    /* 0x8015F3F4 stores the projection object's matrix FOV at owner +0xE4.
       The model data supplies a horizontal FOV in degrees; the game converts it
       with tan/atan before FUN_801B0C30 builds the perspective matrix. */
    const float gamePi = 3.141499996f;
    const float videoScale = 0.75f;
    float halfRadians;
    float correctedHalfRadians;

    if (fovDegrees <= 0.00001f) {
        return 45.0f;
    }

    halfRadians = (gamePi * (fovDegrees * 0.5f)) / 180.0f;
    correctedHalfRadians = atanf(tanf(halfRadians) * videoScale);
    return 2.0f * ((180.0f * correctedHalfRadians) / gamePi);
}

static int CzanModel_FloatBits(float value) {
    int bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void CzanModel_SubmitAnimatedZmbPrimitiveStreamsWithDrawMode(
    const void *zmbData,
    unsigned int zmbSize,
    const void *zabData,
    unsigned int zabSize,
    float animationTick,
    int drawMode,
    const float *runtimeWorldMatrices,
    CzanModelSubmittedPrimitiveBuffer *outBuffer);

static CzanModelHostState *CzanModel_GetHostState(int *model, int create) {
    unsigned int i;
    CzanModelHostState *freeSlot = 0;

    if (model == 0) {
        return 0;
    }
    for (i = 0; i < CZAN_MODEL_HOST_STATE_CAP; i++) {
        if (gCzanModelHostStates[i].model == model) {
            return &gCzanModelHostStates[i];
        }
        if (freeSlot == 0 && gCzanModelHostStates[i].model == 0) {
            freeSlot = &gCzanModelHostStates[i];
        }
    }
    if (!create || freeSlot == 0) {
        return 0;
    }
    memset(freeSlot, 0, sizeof(*freeSlot));
    freeSlot->model = model;
    return freeSlot;
}

static CzanModelOwnerHostState *CzanModelOwner_GetHostState(int *owner, int create) {
    unsigned int i;
    CzanModelOwnerHostState *freeSlot = 0;

    if (owner == 0) {
        return 0;
    }
    for (i = 0; i < CZAN_MODEL_HOST_STATE_CAP; i++) {
        if (gCzanModelOwnerHostStates[i].owner == owner) {
            return &gCzanModelOwnerHostStates[i];
        }
        if (freeSlot == 0 && gCzanModelOwnerHostStates[i].owner == 0) {
            freeSlot = &gCzanModelOwnerHostStates[i];
        }
    }
    if (!create || freeSlot == 0) {
        return 0;
    }
    memset(freeSlot, 0, sizeof(*freeSlot));
    freeSlot->owner = owner;
    return freeSlot;
}

static void CzanModelOwner_StoreVec3(void *dest, float x, float y, float z) {
    float *vec = (float *)dest;

    if (vec == 0) {
        return;
    }

    vec[0] = x;
    vec[1] = y;
    vec[2] = z;
}

static void CzanModelOwner_CopyVec3(void *dest, const void *src) {
    const float *srcVec = (const float *)src;

    if (dest == 0 || src == 0) {
        return;
    }

    CzanModelOwner_StoreVec3(dest, srcVec[0], srcVec[1], srcVec[2]);
}

static void CzanModelOwner_NormalizeVec3(float *vec) {
    float lengthSq;
    float invLength;

    if (vec == 0) {
        return;
    }

    lengthSq = vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2];
    if (lengthSq <= 0.000001f) {
        return;
    }

    invLength = 1.0f / sqrtf(lengthSq);
    vec[0] *= invLength;
    vec[1] *= invLength;
    vec[2] *= invLength;
}

static void CzanModelOwner_CrossVec3(const float *a, const float *b, float *out) {
    float x;
    float y;
    float z;

    if (a == 0 || b == 0 || out == 0) {
        return;
    }

    x = a[1] * b[2] - b[1] * a[2];
    y = -(a[0] * b[2] - b[0] * a[2]);
    z = -(a[1] * b[0] - b[1] * a[0]);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static void CzanModelOwner_BuildLookAtMatrix(float *matrix34, const float *position, const float *up, const float *target) {
    float zAxis[3];
    float xAxis[3];
    float yAxis[3];

    if (matrix34 == 0 || position == 0 || up == 0 || target == 0) {
        return;
    }

    zAxis[0] = position[0] - target[0];
    zAxis[1] = position[1] - target[1];
    zAxis[2] = position[2] - target[2];
    CzanModelOwner_NormalizeVec3(zAxis);

    CzanModelOwner_CrossVec3(up, zAxis, xAxis);
    CzanModelOwner_NormalizeVec3(xAxis);
    CzanModelOwner_CrossVec3(zAxis, xAxis, yAxis);

    matrix34[0] = xAxis[0];
    matrix34[1] = xAxis[1];
    matrix34[2] = xAxis[2];
    matrix34[3] = -(position[2] * xAxis[2] + position[0] * xAxis[0] + position[1] * xAxis[1]);
    matrix34[4] = yAxis[0];
    matrix34[5] = yAxis[1];
    matrix34[6] = yAxis[2];
    matrix34[7] = -(position[2] * yAxis[2] + position[0] * yAxis[0] + position[1] * yAxis[1]);
    matrix34[8] = zAxis[0];
    matrix34[9] = zAxis[1];
    matrix34[10] = zAxis[2];
    matrix34[11] = -(position[2] * zAxis[2] + position[0] * zAxis[0] + position[1] * zAxis[1]);
}

static void CzanModelOwner_ResetBaseTransform(int *owner) {
    unsigned char *bytes;

    if (owner == 0) {
        return;
    }

    bytes = (unsigned char *)owner;
    Matrix34_SetIdentity((float *)(bytes + 0x4c));
    CzanModelOwner_StoreVec3(bytes + 0x10, 0.0f, 0.0f, 1.0f);
    CzanModelOwner_StoreVec3(bytes + 0x04, 0.0f, 0.0f, 0.0f);
    CzanModelOwner_StoreVec3(bytes + 0x1c, 0.0f, 1.0f, 0.0f);
    *(int *)(void *)(bytes + 0x7c) = 0;
}

static void CzanModelOwner_SetDefaultProjectionState(int *owner) {
    unsigned char *bytes;

    if (owner == 0) {
        return;
    }

    bytes = (unsigned char *)owner;
    *(float *)(void *)(bytes + 0xfc) = 1.333333373f;
    *(float *)(void *)(bytes + 0xf8) = 45.0f;
    *(float *)(void *)(bytes + 0x100) = 1.0f;
    *(float *)(void *)(bytes + 0x104) = 10000.0f;
    *(float *)(void *)(bytes + 0xe4) = 45.0f;
    *(float *)(void *)(bytes + 0xe8) = 1.333333373f;
    *(int *)(void *)(bytes + 0xec) = 1;
    *(int *)(void *)(bytes + 0xf4) = 0;
}

static void CzanModel_GetObjectProjectionAux(int *model, float *outValues, int objectIndex) {
    const float *record;

    if (outValues == 0) {
        return;
    }

    outValues[0] = 0.0f;
    outValues[1] = 0.0f;
    outValues[2] = 0.0f;
    if (model == 0 || objectIndex < 0 || model[6] == 0 || objectIndex >= model[0x26]) {
        return;
    }

    record = (const float *)(uintptr_t)(model[6] + objectIndex * 0x10);
    outValues[0] = record[1];
    outValues[1] = record[2];
    outValues[2] = record[3];
}

static void CzanModelOwner_DestroyHostModel(int *owner) {
    CzanModelOwnerHostState *ownerState;
    int *model;

    if (owner == 0) {
        return;
    }

    ownerState = CzanModelOwner_GetHostState(owner, 0);
    model = ownerState != 0 ? ownerState->model : 0;
    if (model != 0) {
        CzanModel_Destroy(model, 1);
        free(model);
        ownerState->model = 0;
    }
    owner[0x20] = 0;
}

int *CzanModelOwner_GetHostModel(int *owner) {
    CzanModelOwnerHostState *state = CzanModelOwner_GetHostState(owner, 0);
    return state != 0 ? state->model : 0;
}

void *CzanModel_GetHostPrimaryBlock(int *model) {
    CzanModelHostState *state = CzanModel_GetHostState(model, 0);
    return state != 0 ? state->primaryBlock : 0;
}

unsigned int CzanModel_GetHostPrimaryBlockSize(int *model) {
    CzanModelHostState *state = CzanModel_GetHostState(model, 0);
    return state != 0 ? state->primaryBlockSize : 0;
}

void *CzanModel_GetHostContinuationBlock(int *model, int continuationIndex) {
    CzanModelHostState *state = CzanModel_GetHostState(model, 0);
    if (state == 0 || continuationIndex < 0 || continuationIndex >= state->continuationCount ||
        continuationIndex >= CZAN_MODEL_HOST_CONTINUATION_CAP) {
        return 0;
    }
    return state->continuations[continuationIndex];
}

static unsigned int CzanModel_ReadBe32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) |
           (unsigned int)p[3];
}

static unsigned int CzanModel_ReadBe16(const unsigned char *p) {
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static float CzanModel_ReadBeFloat(const unsigned char *p) {
    union {
        unsigned int u;
        float f;
    } value;

    value.u = CzanModel_ReadBe32(p);
    return value.f;
}

static int CzanModel_IsLikelyNameChar(unsigned char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '_' ||
           c == '-' ||
           c == '@';
}

static void CzanModel_CopyName(char *outName, unsigned int outNameSize, const unsigned char *data, unsigned int maxSize) {
    unsigned int i;

    if (outNameSize == 0) {
        return;
    }

    for (i = 0; i + 1 < outNameSize && i < maxSize; i++) {
        if (data[i] == 0 || !CzanModel_IsLikelyNameChar(data[i])) {
            break;
        }
        outName[i] = (char)data[i];
    }
    outName[i] = '\0';
}

static int CzanModel_FindZmbObjectIndexByName(
    const unsigned char *zmb,
    unsigned int zmbSize,
    unsigned int objectEntryOffset,
    unsigned int objectCount,
    const char *name) {
    unsigned int i;

    if (zmb == 0 || name == 0 || name[0] == '\0') {
        return -1;
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        char objectName[32];

        if (entryOffset >= zmbSize) {
            break;
        }

        CzanModel_CopyName(objectName, sizeof(objectName), zmb + entryOffset, zmbSize - entryOffset);
        if (strcmp(objectName, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static float CzanModel_ClampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static void CzanModel_NormalizeQuat(float *x, float *y, float *z, float *w) {
    float lengthSquared = (*x * *x) + (*y * *y) + (*z * *z) + (*w * *w);
    float invLength;

    if (lengthSquared <= 0.000001f) {
        *x = 0.0f;
        *y = 0.0f;
        *z = 0.0f;
        *w = 1.0f;
        return;
    }

    invLength = 1.0f / sqrtf(lengthSquared);
    *x *= invLength;
    *y *= invLength;
    *z *= invLength;
    *w *= invLength;
}

static void CzanModel_QuatToMatrix34(float x, float y, float z, float w, float *matrix34) {
    float xx;
    float yy;
    float zz;
    float xy;
    float xz;
    float yz;
    float wx;
    float wy;
    float wz;

    CzanModel_NormalizeQuat(&x, &y, &z, &w);

    xx = x * x;
    yy = y * y;
    zz = z * z;
    xy = x * y;
    xz = x * z;
    yz = y * z;
    wx = w * x;
    wy = w * y;
    wz = w * z;

    Matrix34_SetIdentity(matrix34);
    matrix34[0] = 1.0f - 2.0f * (yy + zz);
    matrix34[1] = 2.0f * (xy - wz);
    matrix34[2] = 2.0f * (xz + wy);
    matrix34[4] = 2.0f * (xy + wz);
    matrix34[5] = 1.0f - 2.0f * (xx + zz);
    matrix34[6] = 2.0f * (yz - wx);
    matrix34[8] = 2.0f * (xz - wy);
    matrix34[9] = 2.0f * (yz + wx);
    matrix34[10] = 1.0f - 2.0f * (xx + yy);
}

static int CzanModel_FindKeySegment(
    const unsigned char *zab,
    unsigned int zabSize,
    unsigned int keyOffset,
    unsigned int keyCount,
    unsigned int keyStride,
    float animationTick,
    unsigned int *outKey0,
    unsigned int *outKey1,
    float *outT) {
    unsigned int i;

    if (zab == 0 || keyCount == 0 || keyOffset >= zabSize ||
        outKey0 == 0 || outKey1 == 0 || outT == 0) {
        return 0;
    }

    if (keyOffset + keyStride > zabSize || keyCount == 1) {
        *outKey0 = 0;
        *outKey1 = 0;
        *outT = 0.0f;
        return keyOffset + keyStride <= zabSize;
    }

    for (i = 0; i + 1 < keyCount; i++) {
        unsigned int currentOffset = keyOffset + i * keyStride;
        unsigned int nextOffset = currentOffset + keyStride;
        float currentTick;
        float nextTick;

        if (nextOffset + keyStride > zabSize) {
            break;
        }

        currentTick = (float)CzanModel_ReadBe32(zab + currentOffset);
        nextTick = (float)CzanModel_ReadBe32(zab + nextOffset);
        if (animationTick <= nextTick) {
            float span = nextTick - currentTick;
            *outKey0 = i;
            *outKey1 = i + 1;
            *outT = span > 0.0f ? CzanModel_ClampFloat((animationTick - currentTick) / span, 0.0f, 1.0f) : 0.0f;
            return 1;
        }
    }

    *outKey0 = keyCount - 1;
    *outKey1 = keyCount - 1;
    *outT = 0.0f;
    return keyOffset + (*outKey0 * keyStride) + keyStride <= zabSize;
}

static float CzanModel_LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

static void CzanModel_ApplyZabToLocalMatrices(
    const unsigned char *zmb,
    unsigned int zmbSize,
    unsigned int objectEntryOffset,
    unsigned int objectCount,
    const unsigned char *zab,
    unsigned int zabSize,
    float animationTick,
    float (*localMatrices)[12]) {
    unsigned int channelCount;
    unsigned int durationTicks;
    unsigned int channelIndex;

    if (zmb == 0 || zab == 0 || localMatrices == 0 ||
        zabSize < 0x30 || memcmp(zab, "ZAB ", 4) != 0) {
        return;
    }

    channelCount = CzanModel_ReadBe32(zab + 0x0c);
    durationTicks = CzanModel_ReadBe32(zab + 0x10);
    if (durationTicks != 0) {
        while (animationTick >= (float)durationTicks) {
            animationTick -= (float)durationTicks;
        }
        while (animationTick < 0.0f) {
            animationTick += (float)durationTicks;
        }
    }

    for (channelIndex = 0; channelIndex < channelCount; channelIndex++) {
        unsigned int channelOffset = 0x30 + channelIndex * 0x40;
        unsigned int keyGroupCount;
        unsigned int keyGroupOffset;
        unsigned int groupIndex;
        int objectIndex;
        char channelName[32];
        float animatedTranslation[3] = {0.0f, 0.0f, 0.0f};
        float animatedScale[3] = {1.0f, 1.0f, 1.0f};
        float animatedRotation[12];
        int hasTranslation = 0;
        int hasScale = 0;
        int hasRotation = 0;

        if (channelOffset + 0x40 > zabSize) {
            break;
        }

        CzanModel_CopyName(channelName, sizeof(channelName), zab + channelOffset, zabSize - channelOffset);
        objectIndex = CzanModel_FindZmbObjectIndexByName(zmb, zmbSize, objectEntryOffset, objectCount, channelName);
        if (objectIndex < 0) {
            continue;
        }

        Matrix34_SetIdentity(animatedRotation);
        keyGroupCount = CzanModel_ReadBe32(zab + channelOffset + 0x34);
        keyGroupOffset = CzanModel_ReadBe32(zab + channelOffset + 0x3c);
        for (groupIndex = 0; groupIndex < keyGroupCount; groupIndex++) {
            unsigned int groupOffset = keyGroupOffset + groupIndex * 0x10;
            unsigned int keyType;
            unsigned int keyCount;
            unsigned int keyOffset;
            unsigned int key0;
            unsigned int key1;
            float t;

            if (groupOffset + 0x10 > zabSize) {
                break;
            }

            keyType = CzanModel_ReadBe32(zab + groupOffset);
            keyCount = CzanModel_ReadBe32(zab + groupOffset + 8);
            keyOffset = CzanModel_ReadBe32(zab + groupOffset + 0x0c);
            if (keyType == 0 &&
                CzanModel_FindKeySegment(zab, zabSize, keyOffset, keyCount, 0x10, animationTick, &key0, &key1, &t)) {
                unsigned int offset0 = keyOffset + key0 * 0x10;
                unsigned int offset1 = keyOffset + key1 * 0x10;
                animatedTranslation[0] = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 4), CzanModel_ReadBeFloat(zab + offset1 + 4), t);
                animatedTranslation[1] = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 8), CzanModel_ReadBeFloat(zab + offset1 + 8), t);
                animatedTranslation[2] = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 0x0c), CzanModel_ReadBeFloat(zab + offset1 + 0x0c), t);
                hasTranslation = 1;
            }
            else if (keyType == 1 &&
                     CzanModel_FindKeySegment(zab, zabSize, keyOffset, keyCount, 0x14, animationTick, &key0, &key1, &t)) {
                unsigned int offset0 = keyOffset + key0 * 0x14;
                unsigned int offset1 = keyOffset + key1 * 0x14;
                float x = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 4), CzanModel_ReadBeFloat(zab + offset1 + 4), t);
                float y = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 8), CzanModel_ReadBeFloat(zab + offset1 + 8), t);
                float z = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 0x0c), CzanModel_ReadBeFloat(zab + offset1 + 0x0c), t);
                float w = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 0x10), CzanModel_ReadBeFloat(zab + offset1 + 0x10), t);
                CzanModel_QuatToMatrix34(x, y, z, w, animatedRotation);
                hasRotation = 1;
            }
            else if (keyType == 2 &&
                     CzanModel_FindKeySegment(zab, zabSize, keyOffset, keyCount, 0x10, animationTick, &key0, &key1, &t)) {
                unsigned int offset0 = keyOffset + key0 * 0x10;
                unsigned int offset1 = keyOffset + key1 * 0x10;
                animatedScale[0] = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 4), CzanModel_ReadBeFloat(zab + offset1 + 4), t);
                animatedScale[1] = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 8), CzanModel_ReadBeFloat(zab + offset1 + 8), t);
                animatedScale[2] = CzanModel_LerpFloat(CzanModel_ReadBeFloat(zab + offset0 + 0x0c), CzanModel_ReadBeFloat(zab + offset1 + 0x0c), t);
                hasScale = 1;
            }
        }

        if (hasRotation || hasScale) {
            float tx = localMatrices[objectIndex][3];
            float ty = localMatrices[objectIndex][7];
            float tz = localMatrices[objectIndex][11];
            Matrix34_Copy(localMatrices[objectIndex], animatedRotation);
            localMatrices[objectIndex][0] *= animatedScale[0];
            localMatrices[objectIndex][4] *= animatedScale[0];
            localMatrices[objectIndex][8] *= animatedScale[0];
            localMatrices[objectIndex][1] *= animatedScale[1];
            localMatrices[objectIndex][5] *= animatedScale[1];
            localMatrices[objectIndex][9] *= animatedScale[1];
            localMatrices[objectIndex][2] *= animatedScale[2];
            localMatrices[objectIndex][6] *= animatedScale[2];
            localMatrices[objectIndex][10] *= animatedScale[2];
            localMatrices[objectIndex][3] = tx;
            localMatrices[objectIndex][7] = ty;
            localMatrices[objectIndex][11] = tz;
        }
        if (hasTranslation) {
            localMatrices[objectIndex][3] = animatedTranslation[0];
            localMatrices[objectIndex][7] = animatedTranslation[1];
            localMatrices[objectIndex][11] = animatedTranslation[2];
        }
    }
}

int *CzanModel_Init(int *model) {
    /* 0x8014BE78 initializes the 0x2D0-byte model object allocated by
       CtsStageObj_LoadModelBlocks. Its vtable is PTR_PTR_802C0720. The original
       sets many transform/material/render defaults, clears several state blocks,
       initializes color fields to 0xFF, and uses a display-mode check to choose
       one scale/aspect-related float at model[0x67]. */
    if (model == 0) {
        return 0;
    }

    memset(model, 0, 0x2d0);
    model[0] = 0x802C0720;
    model[0x27] = 1;
    model[0x30] = -1;
    model[0x4c] = -1;
    model[0x51] = 0x111;
    model[0x55] = 1;
    model[0x5a] = 1;
    model[0x67] = CzanModel_FloatBits(1.0f);
    model[0x68] = 1;
    model[0x6b] = 1;
    model[0xa0] = -1;
    model[0x8d] = -1;
    memset(model + 0xf, 0xff, 4);
    memset(model + 0x10, 0xff, 4);
    return model;
}

int *CzanModel_Destroy(int *model, short releaseMode) {
    /* 0x8014C0B0 is CzanModel vtable +0x08. It restores the CzanModel vtable,
       frees all runtime arrays allocated by CzanModel_BuildRuntimeData and related
       animation setup paths, then optionally frees the CzanModel object itself. */
    if (model == 0) {
        return 0;
    }

    if (model[6] != 0) {
        free((void *)(uintptr_t)model[6]);
        model[6] = 0;
    }
    if (model[7] != 0) {
        free((void *)(uintptr_t)model[7]);
        model[7] = 0;
    }
    if (model[8] != 0) {
        free((void *)(uintptr_t)model[8]);
        model[8] = 0;
    }
    model[0] = 0x802C0720;
    model[0xd] = 0;
    model[0x2d] = 0;
    model[0x6f] = 0;
    model[0x70] = 0;
    model[0x71] = 0;
    model[0x72] = 0;
    model[0x73] = 0;
    model[0x17] = 0;
    model[0x18] = 0;
    (void)releaseMode;
    return model;
}

int CzanModel_SetPrimaryBlock(int *model, int primaryModelBlock, int primaryModelBlockSize) {
    /* 0x8014C67C stores the primary model resource pointer and size on the
       0x2D0-byte CzanModel instance. */
    if (model == 0) {
        return 0;
    }

    model[1] = primaryModelBlock;
    model[2] = primaryModelBlockSize;
    {
        CzanModelHostState *state = CzanModel_GetHostState(model, 1);
        if (state != 0) {
            state->primaryBlock = (void *)(uintptr_t)primaryModelBlock;
            state->primaryBlockSize = (unsigned int)primaryModelBlockSize;
        }
    }
    return 1;
}

void CzanModel_SetHostPrimaryBlock(int *model, void *primaryModelBlock, unsigned int primaryModelBlockSize) {
    CzanModelHostState *state;

    if (model == 0) {
        return;
    }
    state = CzanModel_GetHostState(model, 1);
    if (state != 0) {
        state->primaryBlock = primaryModelBlock;
        state->primaryBlockSize = primaryModelBlockSize;
    }
}

void CzanModel_AttachTextureSet(int *model, int textureSet) {
    /* 0x8014E828 stores the texture set at model +0x54. If the primary ZMB/ZAB
       block has a texture/frame-count table at +0x18, the original validates that
       the attached texture set has enough texture frames. */
    if (model == 0) {
        return;
    }

    model[0x15] = textureSet;
}

void CzanModel_SetContinuationCount(int *model, int continuationCount) {
    /* 0x8015C540 stores the continuation/animation block count at model +0x9C.
       CtsStageObj_LoadModelBlocks calls this before allocating the continuation
       handle array when a ZMB/ZAB entry has extra following blocks. */
    if (model == 0) {
        return;
    }

    model[0x27] = continuationCount;
    {
        CzanModelHostState *state = CzanModel_GetHostState(model, 1);
        if (state != 0) {
            if (continuationCount > CZAN_MODEL_HOST_CONTINUATION_CAP) {
                continuationCount = CZAN_MODEL_HOST_CONTINUATION_CAP;
            }
            state->continuationCount = continuationCount;
            memset(state->continuations, 0, sizeof(state->continuations));
        }
    }
}

int CzanModel_LoadContinuationBlock(int *model, void *continuationBlock, int continuationIndex) {
    /* 0x8014C68C attaches one continuation/ZAB block to a model. It validates
       continuationIndex against model +0x9C, stores the block pointer at model +0x0C,
       then calls the real continuation parser/builder at 0x8014E464(model, index). */
    if (model == 0 || continuationIndex >= model[0x27]) {
        return 0;
    }

    {
        CzanModelHostState *state = CzanModel_GetHostState(model, 1);
        if (state != 0 && continuationIndex >= 0 && continuationIndex < CZAN_MODEL_HOST_CONTINUATION_CAP) {
            state->continuations[continuationIndex] = continuationBlock;
        }
    }
    model[3] = (int)(uintptr_t)continuationBlock;
    if (continuationBlock != 0) {
        unsigned int continuationSize = HostCzan_GetRegisteredLinkSize(continuationBlock);
        const unsigned char *zab = (const unsigned char *)continuationBlock;
        if (continuationSize >= 0x14 && memcmp(zab, "ZAB ", 4) == 0) {
            *(float *)(void *)((unsigned char *)model + 0x240) = (float)CzanModel_ReadBe32(zab + 0x10);
        }
    }
    CzanModel_ParseContinuationAnimationBlock(model, continuationIndex);
    return 1;
}

void CzanModel_ParseContinuationAnimationBlock(int *model, int continuationIndex) {
    /* 0x8014E464 parses the current continuation block pointer at model +0x0C.
       It treats the block as a ZAB animation resource, converts duration/key times
       from ticks through FLOAT_802E9E38, matches each ZAB object/channel name against
       the model object table, and fills the animation record array at model +0x14:

       record base = model[0x14] + (objectIndex + continuationIndex * model[0x98]) * 0x74

       Per matched object:
       +0x00/+0x04 -> translation key count and key pointer
       +0x08/+0x0C -> rotation key count and key pointer
       +0x10/+0x14 -> scale key count and key pointer
       +0x34       -> converted duration

       The original finally marks continuationBlock +0x28 as relocated/parsed. */
    (void)model;
    (void)continuationIndex;
}

void CzanModel_SetFallbackRenderSlot(int *model, int textureSet, int renderMode, unsigned char enabledFlag, unsigned char alpha) {
    unsigned char *bytes;

    /* 0x8015C3CC initializes the fallback/synthetic render slot at model +0x280.
       CtsStageObj_LoadModelBlocks calls it when a fallback texture slot exists:

       CzanModel_SetFallbackRenderSlot(model, textureSet, 6, 1, 0xFF)

       Confirmed fields:
       +0x280 -> renderMode
       +0x288 -> textureSet
       +0x28C..+0x293 -> default/fallback colors
       +0x29C..+0x2A4/+0x2B6/+0x2BC -> control flags and counters. */
    if (model == 0) {
        return;
    }

    bytes = (unsigned char *)model;
    model[0xa0] = renderMode;
    model[0xa2] = textureSet;
    bytes[0x29c] = 1;
    bytes[0x29d] = 0;
    bytes[0x29e] = enabledFlag;
    bytes[0x29f] = (unsigned char)renderMode;
    bytes[0x2a3] = 1;
    model[0xa9] = 0;
    *(unsigned short *)(void *)(bytes + 0x2b6) = 0;
    model[0xaf] = 0;
    bytes[0x2a0] = 0;
    bytes[0x2ab] = 0;
    bytes[0x290] = 0xff;
    bytes[0x291] = 0xff;
    bytes[0x292] = 0xff;
    bytes[0x293] = alpha;
    bytes[0x28c] = 0xff;
    bytes[0x28d] = 0xff;
    bytes[0x28e] = 0xff;
    bytes[0x28f] = 0xff;
}

void CzanModel_ReadZmbObjectLocalMatrix(const void *objectEntry, float *outMatrix34) {
    const unsigned char *entry = (const unsigned char *)objectEntry;

    if (outMatrix34 == 0) {
        return;
    }

    Matrix34_SetIdentity(outMatrix34);
    if (entry == 0) {
        return;
    }

    /* DOL 0x8014C6CC seeds model +0x1C by taking the three basis rows from
       object +0x30/+0x40/+0x50 as columns in the serialized object entry, then
       writing translation from +0x60/+0x64/+0x68. Reading the 12 floats
       sequentially transposes the 3x3 basis and produces the inverted
       select_cmn camera/background. */
    outMatrix34[0] = CzanModel_ReadBeFloat(entry + 0x30);
    outMatrix34[1] = CzanModel_ReadBeFloat(entry + 0x40);
    outMatrix34[2] = CzanModel_ReadBeFloat(entry + 0x50);
    outMatrix34[3] = CzanModel_ReadBeFloat(entry + 0x60);
    outMatrix34[4] = CzanModel_ReadBeFloat(entry + 0x34);
    outMatrix34[5] = CzanModel_ReadBeFloat(entry + 0x44);
    outMatrix34[6] = CzanModel_ReadBeFloat(entry + 0x54);
    outMatrix34[7] = CzanModel_ReadBeFloat(entry + 0x64);
    outMatrix34[8] = CzanModel_ReadBeFloat(entry + 0x38);
    outMatrix34[9] = CzanModel_ReadBeFloat(entry + 0x48);
    outMatrix34[10] = CzanModel_ReadBeFloat(entry + 0x58);
    outMatrix34[11] = CzanModel_ReadBeFloat(entry + 0x68);
}

void CzanModel_BuildZmbObjectWorldMatrices(
    const void *zmbData,
    unsigned int zmbSize,
    unsigned int objectEntryOffset,
    unsigned int objectCount,
    float (*outWorldMatrices34)[12],
    unsigned int maxWorldMatrices) {
    const unsigned char *zmb = (const unsigned char *)zmbData;
    float localMatrices[512][12];
    unsigned int i;

    if (zmb == 0 || outWorldMatrices34 == 0 || objectEntryOffset > zmbSize) {
        return;
    }

    if (objectCount > maxWorldMatrices) {
        objectCount = maxWorldMatrices;
    }
    if (objectCount > 512) {
        objectCount = 512;
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        if (entryOffset + 0xa0 <= zmbSize) {
            CzanModel_ReadZmbObjectLocalMatrix(zmb + entryOffset, localMatrices[i]);
        }
        else {
            Matrix34_SetIdentity(localMatrices[i]);
        }
        Matrix34_Copy(outWorldMatrices34[i], localMatrices[i]);
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        unsigned int parentIndex;

        if (entryOffset + 0x98 > zmbSize) {
            continue;
        }

        parentIndex = CzanModel_ReadBe32(zmb + entryOffset + 0x94);
        if (parentIndex < i && parentIndex < objectCount) {
            Matrix34_Multiply(outWorldMatrices34[i], outWorldMatrices34[parentIndex], localMatrices[i]);
        }
    }
}

static void CzanModel_BuildAnimatedZmbObjectWorldMatrices(
    const void *zmbData,
    unsigned int zmbSize,
    unsigned int objectEntryOffset,
    unsigned int objectCount,
    const void *zabData,
    unsigned int zabSize,
    float animationTick,
    float (*outWorldMatrices34)[12],
    unsigned int maxWorldMatrices) {
    const unsigned char *zmb = (const unsigned char *)zmbData;
    float localMatrices[512][12];
    unsigned int i;

    if (zmb == 0 || outWorldMatrices34 == 0 || objectEntryOffset > zmbSize) {
        return;
    }

    if (objectCount > maxWorldMatrices) {
        objectCount = maxWorldMatrices;
    }
    if (objectCount > 512) {
        objectCount = 512;
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        if (entryOffset + 0xa0 <= zmbSize) {
            CzanModel_ReadZmbObjectLocalMatrix(zmb + entryOffset, localMatrices[i]);
        }
        else {
            Matrix34_SetIdentity(localMatrices[i]);
        }
    }

    CzanModel_ApplyZabToLocalMatrices(
        zmb,
        zmbSize,
        objectEntryOffset,
        objectCount,
        (const unsigned char *)zabData,
        zabSize,
        animationTick,
        localMatrices);

    for (i = 0; i < objectCount; i++) {
        Matrix34_Copy(outWorldMatrices34[i], localMatrices[i]);
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        unsigned int parentIndex;

        if (entryOffset + 0x98 > zmbSize) {
            continue;
        }

        parentIndex = CzanModel_ReadBe32(zmb + entryOffset + 0x94);
        if (parentIndex < i && parentIndex < objectCount) {
            Matrix34_Multiply(outWorldMatrices34[i], outWorldMatrices34[parentIndex], localMatrices[i]);
        }
    }
}

static int CzanModel_UpdateHostRuntimeObjectTransformsFromBlocks(int *model, float animationTick) {
    const unsigned char *zmb;
    unsigned int zmbSize;
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    void *zabBlock;
    unsigned int zabSize;
    int activeAnimationIndex;
    unsigned char *projectionAux;
    float (*localMatrices)[12];
    unsigned int i;

    if (model == 0) {
        return 0;
    }

    zmb = (const unsigned char *)CzanModel_GetHostPrimaryBlock(model);
    zmbSize = CzanModel_GetHostPrimaryBlockSize(model);
    if (zmb == 0 || zmbSize < 0x30 || memcmp(zmb, "ZMB ", 4) != 0) {
        return 0;
    }

    objectTableOffset = CzanModel_ReadBe32(zmb + 0x20);
    if (objectTableOffset > zmbSize || zmbSize - objectTableOffset < 0x0c) {
        return 0;
    }

    objectCount = CzanModel_ReadBe32(zmb + objectTableOffset);
    objectEntryOffset = CzanModel_ReadBe32(zmb + objectTableOffset + 8);
    if (objectCount == 0 || objectCount > 512 || objectEntryOffset > zmbSize) {
        return 0;
    }

    if (model[6] != 0 && model[0x26] != (int)objectCount) {
        free((void *)(uintptr_t)model[6]);
        model[6] = 0;
    }
    if (model[7] != 0 && model[0x26] != (int)objectCount) {
        free((void *)(uintptr_t)model[7]);
        model[7] = 0;
    }
    if (model[8] != 0 && model[0x26] != (int)objectCount) {
        free((void *)(uintptr_t)model[8]);
        model[8] = 0;
    }
    if (model[6] == 0) {
        void *runtimeProjectionAux = calloc(objectCount, 0x10);
        if (runtimeProjectionAux == 0) {
            return 0;
        }
        model[6] = (int)(uintptr_t)runtimeProjectionAux;
    }
    if (model[7] == 0) {
        void *runtimeLocalMatrices = calloc(objectCount, 0x30);
        if (runtimeLocalMatrices == 0) {
            return 0;
        }
        model[7] = (int)(uintptr_t)runtimeLocalMatrices;
    }
    if (model[8] == 0) {
        void *runtimeWorldMatrices = calloc(objectCount, 0x30);
        if (runtimeWorldMatrices == 0) {
            return 0;
        }
        model[8] = (int)(uintptr_t)runtimeWorldMatrices;
    }
    model[0x26] = (int)objectCount;

    localMatrices = (float (*)[12])malloc(sizeof(float) * 12u * objectCount);
    if (localMatrices == 0) {
        return 0;
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0u;
        if (entryOffset + 0xa0u <= zmbSize) {
            CzanModel_ReadZmbObjectLocalMatrix(zmb + entryOffset, localMatrices[i]);
        }
        else {
            Matrix34_SetIdentity(localMatrices[i]);
        }
    }

    activeAnimationIndex = *(int *)(void *)((unsigned char *)model + 0x234);
    zabBlock = activeAnimationIndex >= 0 ? CzanModel_GetHostContinuationBlock(model, activeAnimationIndex) : 0;
    zabSize = zabBlock != 0 ? HostCzan_GetRegisteredLinkSize(zabBlock) : 0;
    if (activeAnimationIndex >= 0 && zabBlock != 0 && zabSize != 0) {
        CzanModel_ApplyZabToLocalMatrices(
            zmb,
            zmbSize,
            objectEntryOffset,
            objectCount,
            (const unsigned char *)zabBlock,
            zabSize,
            animationTick,
            localMatrices);
    }

    for (i = 0; i < objectCount; i++) {
        Matrix34_Copy((float *)(uintptr_t)(model[7] + (int)(i * 0x30u)), localMatrices[i]);
        Matrix34_Copy((float *)(uintptr_t)(model[8] + (int)(i * 0x30u)), localMatrices[i]);
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0u;
        unsigned int parentIndex;

        if (entryOffset + 0x98u > zmbSize) {
            continue;
        }

        parentIndex = CzanModel_ReadBe32(zmb + entryOffset + 0x94);
        if (parentIndex < i && parentIndex < objectCount) {
            Matrix34_Multiply(
                (float *)(uintptr_t)(model[8] + (int)(i * 0x30u)),
                (const float *)(uintptr_t)(model[8] + (int)(parentIndex * 0x30u)),
                localMatrices[i]);
        }
    }

    projectionAux = (unsigned char *)(uintptr_t)model[6];
    if (projectionAux != 0) {
        memset(projectionAux, 0, objectCount * 0x10u);
        for (i = 0; i < objectCount; i++) {
            unsigned int entryOffset = objectEntryOffset + i * 0xa0u;
            unsigned int objectType;
            unsigned char *record;

            if (entryOffset + 0x7c > zmbSize) {
                continue;
            }

            objectType = CzanModel_ReadBe32(zmb + entryOffset + 0x2c);
            if (objectType != 4) {
                continue;
            }

            record = projectionAux + i * 0x10u;
            *(unsigned int *)(void *)(record + 4) = CzanModel_ReadBe32(zmb + entryOffset + 0x70);
            *(unsigned int *)(void *)(record + 8) = CzanModel_ReadBe32(zmb + entryOffset + 0x74);
            *(unsigned int *)(void *)(record + 0x0c) = CzanModel_ReadBe32(zmb + entryOffset + 0x78);
        }
    }
    free(localMatrices);
    return 1;
}

void CzanModel_TransformPoint(const float *matrix34, const float *point3, float *outPoint3) {
    float x;
    float y;
    float z;

    if (matrix34 == 0 || point3 == 0 || outPoint3 == 0) {
        return;
    }

    x = point3[0];
    y = point3[1];
    z = point3[2];
    outPoint3[0] = matrix34[0] * x + matrix34[1] * y + matrix34[2] * z + matrix34[3];
    outPoint3[1] = matrix34[4] * x + matrix34[5] * y + matrix34[6] * z + matrix34[7];
    outPoint3[2] = matrix34[8] * x + matrix34[9] * y + matrix34[10] * z + matrix34[11];
}

static void CzanModel_ResetSubmittedPrimitiveBuffer(CzanModelSubmittedPrimitiveBuffer *buffer) {
    if (buffer == 0) {
        return;
    }

    buffer->vertexCount = 0;
    buffer->primitiveCount = 0;
    buffer->submittedObjectCount = 0;
    buffer->boundsMin[0] = 0.0f;
    buffer->boundsMin[1] = 0.0f;
    buffer->boundsMin[2] = 0.0f;
    buffer->boundsMax[0] = 0.0f;
    buffer->boundsMax[1] = 0.0f;
    buffer->boundsMax[2] = 0.0f;
}

static int CzanModel_GetMaterialEntryOffset(
    const unsigned char *zmb,
    unsigned int zmbSize,
    unsigned int materialIndex,
    unsigned int *outMaterialOffset) {
    unsigned int materialTableOffset;
    unsigned int materialCount;
    unsigned int materialVersion;
    unsigned int materialStride;
    unsigned int materialEntryOffset;
    unsigned int materialOffset;

    if (zmb == 0 || zmbSize < 0x20) {
        return 0;
    }

    materialTableOffset = CzanModel_ReadBe32(zmb + 0x1c);
    if (materialTableOffset + 0x0c > zmbSize) {
        return 0;
    }

    materialCount = CzanModel_ReadBe32(zmb + materialTableOffset);
    materialVersion = CzanModel_ReadBe32(zmb + materialTableOffset + 4);
    materialEntryOffset = CzanModel_ReadBe32(zmb + materialTableOffset + 8);
    if (materialIndex >= materialCount) {
        return 0;
    }

    materialStride = ((materialVersion >> 16) == 1) ? 0x38 : 0x50;
    materialOffset = materialEntryOffset + materialIndex * materialStride;
    if (materialOffset + materialStride > zmbSize) {
        return 0;
    }

    if (outMaterialOffset != 0) {
        *outMaterialOffset = materialOffset;
    }
    return 1;
}

static unsigned int CzanModel_GetMaterialTextureIndex(
    const unsigned char *zmb,
    unsigned int zmbSize,
    unsigned int materialIndex) {
    unsigned int materialOffset;
    unsigned int textureRecordOffset;

    if (!CzanModel_GetMaterialEntryOffset(zmb, zmbSize, materialIndex, &materialOffset)) {
        return 0;
    }

    textureRecordOffset = CzanModel_ReadBe32(zmb + materialOffset + 0x18);
    if (textureRecordOffset + 4 > zmbSize) {
        return 0;
    }

    return CzanModel_ReadBe32(zmb + textureRecordOffset);
}

static unsigned char CzanModel_GetMaterialModeByte(
    const unsigned char *zmb,
    unsigned int zmbSize,
    unsigned int materialIndex) {
    unsigned int materialOffset;

    if (!CzanModel_GetMaterialEntryOffset(zmb, zmbSize, materialIndex, &materialOffset) ||
        materialOffset + 0x13 > zmbSize) {
        return 0;
    }

    return zmb[materialOffset + 0x12];
}

static void CzanModel_AddSubmittedVertex(
    CzanModelSubmittedPrimitiveBuffer *buffer,
    const float *point,
    const float *texcoord,
    unsigned int color,
    unsigned int objectIndex) {
    unsigned int axis;

    if (buffer == 0 || point == 0 || buffer->vertices == 0 ||
        buffer->vertexCount >= buffer->vertexCapacity) {
        return;
    }

    if (buffer->vertexCount == 0) {
        for (axis = 0; axis < 3; axis++) {
            buffer->boundsMin[axis] = point[axis];
            buffer->boundsMax[axis] = point[axis];
        }
    }
    else {
        for (axis = 0; axis < 3; axis++) {
            if (point[axis] < buffer->boundsMin[axis]) {
                buffer->boundsMin[axis] = point[axis];
            }
            if (point[axis] > buffer->boundsMax[axis]) {
                buffer->boundsMax[axis] = point[axis];
            }
        }
    }

    buffer->vertices[buffer->vertexCount][0] = point[0];
    buffer->vertices[buffer->vertexCount][1] = point[1];
    buffer->vertices[buffer->vertexCount][2] = point[2];
    if (buffer->texcoords != 0 && texcoord != 0) {
        buffer->texcoords[buffer->vertexCount][0] = texcoord[0];
        buffer->texcoords[buffer->vertexCount][1] = texcoord[1];
    }
    else if (buffer->texcoords != 0) {
        buffer->texcoords[buffer->vertexCount][0] = 0.0f;
        buffer->texcoords[buffer->vertexCount][1] = 0.0f;
    }
    if (buffer->colors != 0) {
        buffer->colors[buffer->vertexCount] = color;
    }
    if (buffer->vertexObjectIndex != 0) {
        buffer->vertexObjectIndex[buffer->vertexCount] = objectIndex;
    }
    buffer->vertexCount++;
}

void CzanModel_SubmitVisibleZmbPrimitiveStreams(
    const void *zmbData,
    unsigned int zmbSize,
    CzanModelSubmittedPrimitiveBuffer *outBuffer) {
    CzanModel_SubmitAnimatedZmbPrimitiveStreamsWithDrawMode(zmbData, zmbSize, 0, 0, 0.0f, 0, 0, outBuffer);
}

void CzanModel_SubmitAnimatedZmbPrimitiveStreams(
    const void *zmbData,
    unsigned int zmbSize,
    const void *zabData,
    unsigned int zabSize,
    float animationTick,
    CzanModelSubmittedPrimitiveBuffer *outBuffer) {
    CzanModel_SubmitAnimatedZmbPrimitiveStreamsWithDrawMode(
        zmbData,
        zmbSize,
        zabData,
        zabSize,
        animationTick,
        0,
        0,
        outBuffer);
}

static void CzanModel_SubmitAnimatedZmbPrimitiveStreamsWithDrawMode(
    const void *zmbData,
    unsigned int zmbSize,
    const void *zabData,
    unsigned int zabSize,
    float animationTick,
    int drawMode,
    const float *runtimeWorldMatrices,
    CzanModelSubmittedPrimitiveBuffer *outBuffer) {
    const unsigned char *zmb = (const unsigned char *)zmbData;
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    unsigned int objectIndex;
    float worldMatrices[512][12];

    CzanModel_ResetSubmittedPrimitiveBuffer(outBuffer);
    if (zmb == 0 || outBuffer == 0 || zmbSize < 0x30 ||
        memcmp(zmb, "ZMB ", 4) != 0) {
        return;
    }

    objectTableOffset = CzanModel_ReadBe32(zmb + 0x20);
    if (objectTableOffset > zmbSize || zmbSize - objectTableOffset < 0x0c) {
        return;
    }

    objectCount = CzanModel_ReadBe32(zmb + objectTableOffset);
    objectEntryOffset = CzanModel_ReadBe32(zmb + objectTableOffset + 8);
    if (objectEntryOffset > zmbSize) {
        return;
    }
    if (objectCount > 512) {
        objectCount = 512;
    }

    if (runtimeWorldMatrices != 0) {
        for (objectIndex = 0; objectIndex < objectCount; objectIndex++) {
            Matrix34_Copy(worldMatrices[objectIndex], runtimeWorldMatrices + objectIndex * 12u);
        }
    }
    else {
        CzanModel_BuildAnimatedZmbObjectWorldMatrices(
            zmb,
            zmbSize,
            objectEntryOffset,
            objectCount,
            zabData,
            zabSize,
            animationTick,
            worldMatrices,
            512);
    }

    for (objectIndex = 0; objectIndex < objectCount; objectIndex++) {
        unsigned int entryOffset = objectEntryOffset + objectIndex * 0xa0;
        const unsigned char *entry;
        unsigned int objectType;
        unsigned int submeshCount;
        unsigned int submeshTable;
        unsigned int submeshIndex;
        unsigned char zdrawTag;
        int submittedObject = 0;

        if (entryOffset + 0xa0 > zmbSize ||
            outBuffer->vertexCount >= outBuffer->vertexCapacity ||
            outBuffer->primitiveCount >= outBuffer->primitiveCapacity) {
            break;
        }

        entry = zmb + entryOffset;
        objectType = CzanModel_ReadBe32(entry + 0x2c);
        submeshCount = CzanModel_ReadBe16(entry + 0x9a);
        submeshTable = CzanModel_ReadBe32(entry + 0x9c);
        zdrawTag = entry[0x29];

        if (entry[0x28] != 0 || entry[0x2a] != 0 || submeshCount == 0 ||
            submeshTable >= zmbSize || objectType == 2) {
            continue;
        }
        if ((drawMode == 1 && zdrawTag == 1) ||
            (drawMode != 0 && drawMode != 1 && zdrawTag == 0)) {
            continue;
        }

        for (submeshIndex = 0; submeshIndex < submeshCount; submeshIndex++) {
            unsigned int submeshOffset = submeshTable + submeshIndex * 0x40;
            const unsigned char *submesh;
            unsigned int materialIndex;
            unsigned int textureIndex;
            unsigned char materialMode;
            unsigned int primitiveStride;
            unsigned int primitiveCount;
            unsigned int primitiveTable;
            unsigned int positionArray;
            unsigned int texcoordArray;
            unsigned int colorArray;
            unsigned int primitiveIndex;

            if (submeshOffset + 0x40 > zmbSize) {
                break;
            }

            submesh = zmb + submeshOffset;
            materialIndex = CzanModel_ReadBe16(submesh + 2);
            textureIndex = CzanModel_GetMaterialTextureIndex(zmb, zmbSize, materialIndex);
            materialMode = CzanModel_GetMaterialModeByte(zmb, zmbSize, materialIndex);
            primitiveStride = CzanModel_ReadBe16(submesh + 4) != 0 ? 0x20 : 0x14;
            primitiveCount = CzanModel_ReadBe16(submesh + 0x0a);
            primitiveTable = CzanModel_ReadBe32(submesh + 0x20);
            positionArray = CzanModel_ReadBe32(submesh + 0x24);
            texcoordArray = CzanModel_ReadBe32(submesh + 0x30);
            colorArray = CzanModel_ReadBe32(submesh + 0x34);
            if (primitiveCount == 0 || primitiveTable >= zmbSize || positionArray >= zmbSize) {
                continue;
            }

            for (primitiveIndex = 0; primitiveIndex < primitiveCount; primitiveIndex++) {
                unsigned int primitiveOffset = primitiveTable + primitiveIndex * primitiveStride;
                unsigned int vertexCount;
                unsigned int positionIndexStream;
                unsigned int colorIndexStream;
                unsigned int texcoordIndexStream;
                unsigned int vertexIndex;
                unsigned int primitiveStart;

                if (primitiveOffset + 0x10 > zmbSize ||
                    outBuffer->primitiveCount >= outBuffer->primitiveCapacity) {
                    break;
                }

                vertexCount = CzanModel_ReadBe16(zmb + primitiveOffset + 2);
                positionIndexStream = CzanModel_ReadBe32(zmb + primitiveOffset + 4);
                colorIndexStream = primitiveOffset + 0x10 <= zmbSize ? CzanModel_ReadBe32(zmb + primitiveOffset + 0x0c) : 0;
                texcoordIndexStream = primitiveOffset + 0x14 <= zmbSize ? CzanModel_ReadBe32(zmb + primitiveOffset + 0x10) : 0;
                if (positionIndexStream >= zmbSize) {
                    continue;
                }

                primitiveStart = outBuffer->vertexCount;
                for (vertexIndex = 0;
                     vertexIndex < vertexCount && outBuffer->vertexCount < outBuffer->vertexCapacity;
                     vertexIndex++) {
                    unsigned int indexOffset = positionIndexStream + vertexIndex * 4;
                    unsigned int positionIndex;
                    unsigned int positionOffset;
                    unsigned int texcoordOffset;
                    float localPoint[3];
                    float worldPoint[3];
                    float texcoord[2] = {0.0f, 0.0f};
                    unsigned int color = 0xFFFFFFFFu;

                    if (indexOffset + 4 > zmbSize) {
                        break;
                    }

                    positionIndex = CzanModel_ReadBe32(zmb + indexOffset) & 0xffffu;
                    positionOffset = positionArray + positionIndex * 0x0c;
                    if (positionOffset + 0x0c > zmbSize) {
                        continue;
                    }

                    localPoint[0] = CzanModel_ReadBeFloat(zmb + positionOffset);
                    localPoint[1] = CzanModel_ReadBeFloat(zmb + positionOffset + 4);
                    localPoint[2] = CzanModel_ReadBeFloat(zmb + positionOffset + 8);
                    if (texcoordArray < zmbSize && texcoordIndexStream + vertexIndex * 4 + 4 <= zmbSize) {
                        unsigned int texcoordIndex = CzanModel_ReadBe32(zmb + texcoordIndexStream + vertexIndex * 4) & 0xffffu;
                        texcoordOffset = texcoordArray + texcoordIndex * 8;
                        if (texcoordOffset + 8 <= zmbSize) {
                            texcoord[0] = CzanModel_ReadBeFloat(zmb + texcoordOffset);
                            texcoord[1] = CzanModel_ReadBeFloat(zmb + texcoordOffset + 4);
                        }
                    }
                    if (colorArray < zmbSize && colorIndexStream + vertexIndex * 4 + 4 <= zmbSize) {
                        unsigned int colorIndex = CzanModel_ReadBe32(zmb + colorIndexStream + vertexIndex * 4) & 0xffffu;
                        unsigned int colorOffset = colorArray + colorIndex * 4;
                        if (colorOffset + 4 <= zmbSize) {
                            color = CzanModel_ReadBe32(zmb + colorOffset);
                        }
                    }
                    if (outBuffer->vertexObjectIndex != 0) {
                        CzanModel_AddSubmittedVertex(outBuffer, localPoint, texcoord, color, objectIndex);
                    }
                    else {
                        CzanModel_TransformPoint(worldMatrices[objectIndex], localPoint, worldPoint);
                        CzanModel_AddSubmittedVertex(outBuffer, worldPoint, texcoord, color, objectIndex);
                    }
                }

                if (outBuffer->vertexCount > primitiveStart) {
                    unsigned int primitiveOut = outBuffer->primitiveCount;
                    outBuffer->primitiveStart[primitiveOut] = primitiveStart;
                    outBuffer->primitiveVertexCount[primitiveOut] = outBuffer->vertexCount - primitiveStart;
                    if (outBuffer->primitiveTextureIndex != 0) {
                        outBuffer->primitiveTextureIndex[primitiveOut] = textureIndex;
                    }
                    if (outBuffer->primitiveMaterialMode != 0) {
                        outBuffer->primitiveMaterialMode[primitiveOut] = materialMode;
                    }
                    outBuffer->primitiveCount++;
                    submittedObject = 1;
                }
            }
        }

        if (submittedObject) {
            outBuffer->submittedObjectCount++;
        }
    }
}

int CzanModel_BuildRuntimeData(int *model, int enabled) {
    /* 0x8014C6CC is the large CzanModel primary-block relocation/runtime-build
       function. It is called by CzanModel_SetEnabled after normalizing enabled to
       0/1. This is the ZMB runtime builder, not a draw call.

       Confirmed ZMB build phases:
       - validates model[1] / primary block exists.
       - if primaryBlock +0x2C exists, marks model byte +0x6D.
       - relocates primary-block offsets in-place while primaryBlock +0x24 is 0.
       - from primaryBlock +0x18, allocates texture/frame helper arrays:
         model +0x5C and +0x60, each frameCount * 4 bytes.
       - from primaryBlock +0x1C, stores the material/part table at model +0x4C,
         picks part stride model +0xB8 as 0x38 or 0x50 from the table version,
         relocates child pointers, UV/keyframe tables, and nested part records.
       - counts visible/animated parts, allocates model +0x44 as count * 0x50,
         stores count at model +0x48, writes source part pointers at runtime +0x30,
         initializes each runtime part matrix, and calls
         CzanModel_InitVisiblePartUvRuntime.
       - builds per-part runtime animation/cache records at model +0x2C, stride 0xDC.
       - from primaryBlock +0x20, stores object count at model +0x98, object entries
         are 0xA0 bytes, and allocates model +0x1C/+0x20/+0x24/+0x28 as
         objectCount * 0x30 transform arrays.
       - if model +0x9C is nonzero, allocates model +0x14 as
         objectCount * model[0x9C] * 0x74 animation records.
       - allocates model +0x18 as objectCount * 0x10 skip/aux records.
       - scans object names for tags such as trans, ZDRAW, COLLINE, and the object
         name prefix table; writes object bytes +0x27/+0x28/+0x29/+0x2A/+0x2B.
       - initializes local transforms in model +0x1C from object +0x30/+0x34/+0x38
         and translation offsets at object +0x60.
       - for type-4 projection objects, copies object +0x70/+0x74/+0x78 into
         model +0x18 + objectIndex * 0x10 + 4/8/0xC.
       - relocates object submesh records at object +0x9C and submesh arrays
         +0x20/+0x24/+0x28/+0x2C/+0x30/+0x34.
       - if type-2 objects exist, allocates model +0x34 as objectCount * 0x1C,
         sets model +0x158, and builds per-submesh remap/output buffers used by
         CzanModel_UpdateType2WeightedVectors and type-2 draw submitters.
       - flushes/prepares the relocated primary block and marks primaryBlock +0x24 = 1. */
    if (model == 0 || model[1] == 0) {
        return 0;
    }

    (void)enabled;
    return CzanModel_UpdateHostRuntimeObjectTransformsFromBlocks(
        model,
        *(float *)(void *)((unsigned char *)model + 0x230));
}

int CzanModel_SetEnabled(int *model, unsigned int enabled) {
    /* 0x8014E7D0 normalizes any nonzero enabled value to 1, forwards it through
       FUN_8014C6CC, and if that succeeds calls FUN_8015759C(1.0, model). */
    int built;

    if (model == 0) {
        return 0;
    }

    built = CzanModel_BuildRuntimeData(model, enabled != 0);
    if (built != 0) {
        CzanModel_UpdateObjectTransforms(1.0, model);
    }
    return built;
}

int CzanModel_BuildRuntimeDataAndUpdateTransforms(int *model) {
    /* 0x8014E780 builds runtime data with enabled=0, then updates object
       transforms with a zero/default delta when the build succeeds. */
    if (CzanModel_BuildRuntimeData(model, 0) == 0) {
        return 0;
    }

    CzanModel_UpdateObjectTransforms(0.0, model);
    return 1;
}

int *CzanModelOwner_Init(int *owner) {
    /* 0x8015EC08 constructs a CzanModel owner:
       - clears owner +0x80
       - initializes owner +0x4C current matrix
       - initializes the position/up/target vectors used by FUN_8015E7FC
       - initializes the projection/fade state block at +0x84..+0x104. */
    if (owner == 0) {
        return 0;
    }

    CzanModelOwner_GetHostState(owner, 1);
    {
        CzanModelOwnerHostState *ownerState = CzanModelOwner_GetHostState(owner, 0);
        if (ownerState != 0) {
            ownerState->lastAnimationUpdateFrameToken = -1;
        }
    }
    owner[0x20] = 0;
    CzanModelOwner_ResetBaseTransform(owner);
    CzanModelOwner_SetDefaultProjectionState(owner);
    return owner;
}

void CzanModelOwner_Reset(int *owner) {
    /* 0x8015EDF0 destroys the current owner model and restores the base matrix,
       vectors, and projection state. */
    if (owner == 0) {
        return;
    }

    CzanModelOwner_DestroyHostModel(owner);
    {
        CzanModelOwnerHostState *ownerState = CzanModelOwner_GetHostState(owner, 0);
        if (ownerState != 0) {
            ownerState->lastAnimationUpdateFrameToken = -1;
        }
    }
    CzanModelOwner_ResetBaseTransform(owner);
    CzanModelOwner_SetDefaultProjectionState(owner);
}

void CzanModelOwner_SetBaseTransformVectors(int *owner, const float *translation, const float *upVector, const float *forwardVector) {
    /* 0x8015E9DC copies caller-supplied vectors into the owner, unless animation
       data is active and the matching lock bit in owner +0x7C allows the model to
       drive that vector instead. */
    unsigned char *bytes;
    int hasModel;
    int flags;

    if (owner == 0) {
        return;
    }

    bytes = (unsigned char *)owner;
    hasModel = CzanModelOwner_GetHostModel(owner) != 0;
    flags = *(int *)(void *)(bytes + 0x7c);
    if ((!hasModel || (flags & 1) != 0) && translation != 0) {
        CzanModelOwner_CopyVec3(bytes + 0x04, translation);
    }
    if ((!hasModel || (flags & 4) != 0) && upVector != 0) {
        CzanModelOwner_CopyVec3(bytes + 0x1c, upVector);
    }
    if ((!hasModel || (flags & 2) != 0) && forwardVector != 0) {
        CzanModelOwner_CopyVec3(bytes + 0x10, forwardVector);
    }
}

void CzanModelOwner_SetProjectionParams(int *owner, double fovDegrees, double aspect, double nearPlane, double farPlane) {
    unsigned char *bytes;
    float matrixFovDegrees;

    /* 0x8015F080 updates the owner projection defaults before
       CzanModelOwner_UpdateCurrentMatrix. The original writes owner +0xF8/+0xFC
       and the near/far defaults at +0x100/+0x104, then lets the projection object
       at +0x84 rebuild when needed. */
    if (owner == 0) {
        return;
    }

    bytes = (unsigned char *)owner;
    matrixFovDegrees = CzanModelProjection_ConvertHorizontalFov((float)fovDegrees);
    *(float *)(void *)(bytes + 0xfc) = (float)aspect;
    *(float *)(void *)(bytes + 0xf8) = (float)fovDegrees;
    *(float *)(void *)(bytes + 0xe8) = (float)aspect;
    *(float *)(void *)(bytes + 0xe4) = matrixFovDegrees;
    *(int *)(void *)(bytes + 0xec) = 1;
    *(float *)(void *)(bytes + 0x100) = (float)nearPlane;
    *(float *)(void *)(bytes + 0x104) = (float)farPlane;
    *(int *)(void *)(bytes + 0xf4) = 0;
}

void CzanModelOwner_UpdateModelDrivenMatrix(int *owner, int holdFrame) {
    /* 0x8015E7FC updates owner +0x4C from the current model animation state.

       The host still has partial CzanModel_UpdateAnimationChannel coverage, but
       the owner-side vector extraction and look-at matrix rebuild follow the DOL:
       object 1 supplies position/up/target, and object 2 supplies the target when
       the model has at least three objects. */
    unsigned char *bytes;
    int *model;
    int objectCount;
    int flags;
    float *position;
    float *up;
    float *target;
    const float *object1Matrix;
    const float *object2Matrix;
    int object1Transform;
    int object2Transform;

    if (owner == 0) {
        return;
    }

    bytes = (unsigned char *)owner;
    model = CzanModelOwner_GetHostModel(owner);
    if (model != 0) {
        CzanModelOwnerHostState *ownerState = CzanModelOwner_GetHostState(owner, 0);
        int shouldHoldFrame = holdFrame != 0;

        if (!shouldHoldFrame) {
            int frameToken = Runtime_GetMainLoopFrameCounter();
            if (ownerState != 0 && ownerState->lastAnimationUpdateFrameToken == frameToken) {
                shouldHoldFrame = 1;
            } else if (ownerState != 0) {
                ownerState->lastAnimationUpdateFrameToken = frameToken;
            }
        }
        CzanModel_UpdateAnimationChannel(model, shouldHoldFrame, 0);
    }

    position = (float *)(bytes + 0x04);
    up = (float *)(bytes + 0x1c);
    target = (float *)(bytes + 0x10);
    flags = *(int *)(void *)(bytes + 0x7c);

    objectCount = model != 0 ? model[0x26] : 0;
    if (objectCount > 1) {
        object1Transform = CzanModel_GetObjectTransform(model, 1);
        object1Matrix = (const float *)(uintptr_t)object1Transform;
        if (object1Matrix != 0) {
            if ((flags & 1) == 0) {
                CzanModelOwner_StoreVec3(position, object1Matrix[3], object1Matrix[7], object1Matrix[11]);
            }
            if ((flags & 4) == 0) {
                CzanModelOwner_StoreVec3(up, -object1Matrix[2], -object1Matrix[6], -object1Matrix[10]);
                CzanModelOwner_NormalizeVec3(up);
            }
            if ((flags & 2) == 0) {
                if (objectCount < 3) {
                    CzanModelOwner_StoreVec3(
                        target,
                        position[0] + object1Matrix[1],
                        position[1] + object1Matrix[5],
                        position[2] + object1Matrix[9]);
                } else {
                    object2Transform = CzanModel_GetObjectTransform(model, 2);
                    object2Matrix = (const float *)(uintptr_t)object2Transform;
                    if (object2Matrix != 0) {
                        CzanModelOwner_StoreVec3(target, object2Matrix[3], object2Matrix[7], object2Matrix[11]);
                    }
                }
            }
        }
    }

    CzanModelOwner_BuildLookAtMatrix((float *)(bytes + 0x4c), position, up, target);
}

void CzanModelOwner_UpdateCurrentMatrix(int *owner, int holdFrame) {
    /* 0x8015EEB4 is the public owner update used by select_cmn. Its first step is
       FUN_8015E7FC, which refreshes owner +0x4C. It then reads
       FUN_8015D59C(model, object 1) from model +0x18 and updates the projection
       fields at owner +0xF8/+0xFC/+0x100/+0x104 before marking owner +0xF4 valid. */
    unsigned char *bytes;
    int *model;
    float projectionAux[3];
    float nearPlane;
    float farPlane;
    float fovRadians;
    float fovDegrees;
    float matrixFovDegrees;

    if (owner == 0) {
        return;
    }

    CzanModelOwner_UpdateModelDrivenMatrix(owner, holdFrame);
    bytes = (unsigned char *)owner;
    model = CzanModelOwner_GetHostModel(owner);
    if (model != 0) {
        CzanModel_GetObjectProjectionAux(model, projectionAux, 1);
        nearPlane = projectionAux[1] > 0.00001f ? projectionAux[1] : *(float *)(void *)(bytes + 0x100);
        farPlane = projectionAux[2] > 0.00001f ? projectionAux[2] : *(float *)(void *)(bytes + 0x104);

        *(int *)(void *)(bytes + 0xf4) = 0;
        fovRadians = projectionAux[0];
        if (fovRadians > 0.00001f) {
            fovDegrees = (180.0f * fovRadians) / 3.141592741f;
            matrixFovDegrees = CzanModelProjection_ConvertHorizontalFov(fovDegrees);
            *(float *)(void *)(bytes + 0xe4) = matrixFovDegrees;
            *(float *)(void *)(bytes + 0xe8) = *(float *)(void *)(bytes + 0xfc);
            *(int *)(void *)(bytes + 0xec) = 1;
            *(int *)(void *)(bytes + 0x84) = 0;
            *(float *)(void *)(bytes + 0xf8) = fovDegrees;
        } else {
            *(float *)(void *)(bytes + 0xe4) = *(float *)(void *)(bytes + 0xf8);
            *(float *)(void *)(bytes + 0xe8) = *(float *)(void *)(bytes + 0xfc);
            *(int *)(void *)(bytes + 0xec) = 1;
            *(int *)(void *)(bytes + 0x84) = 0;
        }
        *(float *)(void *)(bytes + 0x100) = nearPlane;
        *(float *)(void *)(bytes + 0x104) = farPlane;
        *(int *)(void *)(bytes + 0xf4) = 1;
    }
}

void CzanModelOwner_ApplyHostProjection(int *owner) {
    unsigned char *bytes;
    float fovDeg;
    float aspect;

    if (owner == 0) {
        gCzanModelHostProjectionActive = 0;
        return;
    }

    bytes = (unsigned char *)owner;
    fovDeg = *(float *)(void *)(bytes + 0xe4);
    aspect = *(float *)(void *)(bytes + 0xe8);
    if (fovDeg <= 0.00001f) {
        fovDeg = *(float *)(void *)(bytes + 0xf8);
    }
    if (aspect <= 0.00001f) {
        aspect = *(float *)(void *)(bytes + 0xfc);
    }
    if (fovDeg <= 0.00001f) {
        fovDeg = 45.0f;
    }
    if (aspect <= 0.00001f) {
        aspect = 1.333333373f;
    }
    gCzanModelHostProjectionActive = 1;
    gCzanModelHostProjectionFovDeg = fovDeg;
    gCzanModelHostProjectionAspect = aspect;
    gCzanModelHostProjectionNear = *(float *)(void *)(bytes + 0x100);
    gCzanModelHostProjectionFar = *(float *)(void *)(bytes + 0x104);
}

void CzanModelOwner_ClearHostProjection(void) {
    gCzanModelHostProjectionActive = 0;
}

void CzanModelOwner_CreateModelFromPrimaryBlock(int *owner, void *primaryBlock, int primaryBlockSize) {
    /* 0x8015EAE0 is the owner-side CzanModel constructor used by select_cmn.
       It destroys any existing model at owner +0x80, allocates a fresh 0x2D0-byte
       CzanModel, initializes it, stores it back at owner +0x80, then calls
       CzanModel_SetPrimaryBlock(model, primaryBlock, primaryBlockSize). */
    int *model;
    CzanModelOwnerHostState *ownerState;

    if (owner == 0) {
        return;
    }

    ownerState = CzanModelOwner_GetHostState(owner, 1);
    if (ownerState == 0) {
        return;
    }
    CzanModelOwner_DestroyHostModel(owner);

    model = (int *)calloc(1, 0x2d0);
    if (model == 0) {
        return;
    }

    CzanModel_Init(model);
    CzanModel_SetPrimaryBlock(model, (int)(uintptr_t)primaryBlock, primaryBlockSize);
    {
        CzanModelHostState *modelState = CzanModel_GetHostState(model, 1);
        if (modelState != 0) {
            modelState->primaryBlock = primaryBlock;
            modelState->primaryBlockSize = (unsigned int)primaryBlockSize;
        }
    }
    ownerState->model = model;
    owner[0x20] = 1;
    CzanModel_GetHostState(model, 1);
}

void CzanModelOwner_BuildRuntimeDataAt80(int *owner) {
    /* 0x8015EBAC builds runtime data for the CzanModel pointer stored at owner
       +0x80. This is a tiny owner-side wrapper around
       CzanModel_BuildRuntimeDataAndUpdateTransforms. */
    int *model = CzanModelOwner_GetHostModel(owner);
    if (model == 0) {
        return;
    }

    CzanModel_BuildRuntimeDataAndUpdateTransforms(model);
}

void CzanModelOwner_SetContinuationCount(int *owner, int continuationCount) {
    /* 0x8015EB98 checks owner +0x80 and forwards to CzanModel_SetContinuationCount.
       In select_cmn the caller passes 10 before attaching blocks 6..0xF. */
    int *model = CzanModelOwner_GetHostModel(owner);
    if (model == 0) {
        return;
    }

    CzanModel_SetContinuationCount(model, continuationCount);
}

void CzanModelOwner_LoadContinuationBlock(int *owner, void *continuationBlock, int continuationIndex) {
    /* 0x8015EBC8 checks owner +0x80 and forwards to CzanModel_LoadContinuationBlock.
       The select_cmn loader calls this for blocks 6..0xF with continuation indices
       0..9, which are the ZAB/animation-side blocks paired with the primary model
       block loaded by CzanModelOwner_CreateModelFromPrimaryBlock. */
    int *model = CzanModelOwner_GetHostModel(owner);
    if (model == 0) {
        return;
    }

    CzanModel_LoadContinuationBlock(model, continuationBlock, continuationIndex);
}

void CzanModelOwner_SetAnimationSpeed(int *owner, double speed) {
    /* 0x8015EBF4 checks owner +0x80 and writes channel 0 playback speed at
       model +0x250. The active animation frame lives separately at model +0x230. */
    int *model;

    model = CzanModelOwner_GetHostModel(owner);
    if (model == 0) {
        return;
    }
    *(float *)(void *)((unsigned char *)model + 0x250) = (float)speed;
}

void CzanModel_StartAnimationChannel(double startFrame, int *model, int animationIndex, int blend, int loop, int channelIndex) {
    unsigned char *channel;
    void *zabBlock;
    unsigned int zabSize;
    int animationRecord;
    int objectCount;
    float duration;

    if (model == 0 || channelIndex < 0 || channelIndex >= 2) {
        return;
    }

    channel = (unsigned char *)model + 0x230 + channelIndex * 0x24;
    duration = 0.0f;
    animationRecord = 0;
    objectCount = model[0x98 / 4];
    if (model[0x14 / 4] != 0 && animationIndex >= 0 && objectCount > 0) {
        animationRecord = model[0x14 / 4] + animationIndex * objectCount * 0x74;
        duration = *(float *)(void *)((unsigned char *)(uintptr_t)animationRecord + 0x34);
    }
    zabBlock = animationIndex >= 0 ? CzanModel_GetHostContinuationBlock(model, animationIndex) : 0;
    zabSize = zabBlock != 0 ? HostCzan_GetRegisteredLinkSize(zabBlock) : 0;
    if (duration <= 0.0f && zabBlock != 0 && zabSize >= 0x14 && memcmp(zabBlock, "ZAB ", 4) == 0) {
        duration = (float)CzanModel_ReadBe32((const unsigned char *)zabBlock + 0x10);
    }

    *(float *)(void *)(channel + 0x00) = (float)startFrame;
    *(int *)(void *)(channel + 0x04) = animationIndex;
    *(int *)(void *)(channel + 0x0c) = animationRecord;
    *(float *)(void *)(channel + 0x10) = duration;
    channel[0x08] = loop != 0;
    channel[0x14] = 0;
    channel[0x15] = 0;
    *(int *)(void *)(channel + 0x18) = blend != 0;
    *(float *)(void *)(channel + 0x1c) = 0.0f;

    CzanModel_UpdateHostRuntimeObjectTransformsFromBlocks(model, (float)startFrame);
    CzanModel_UpdateObjectTransforms(0.0, model);
}

void CzanModelOwner_StartAnimation(int *owner, double startFrame, int animationIndex, int blend, int loop) {
    int *model;

    model = CzanModelOwner_GetHostModel(owner);
    if (model == 0) {
        return;
    }

    {
        CzanModelOwnerHostState *ownerState = CzanModelOwner_GetHostState(owner, 0);
        if (ownerState != 0) {
            ownerState->lastAnimationUpdateFrameToken = -1;
        }
    }
    CzanModel_StartAnimationChannel(startFrame, model, animationIndex, blend, loop, 0);
}

int CzanModelCollection_LoadFromLinkBlocks(int *collection, void *linkData, int modelCount, unsigned int collectionIndex) {
    /* 0x80177CA8 loads a collection/bank of CzanModels from a WII link resource.
       Blocks are consumed in pairs: even block index is the model/ZMB block, odd
       block index is its texture/TPL block. The original allocates modelCount
       CzanModel objects, creates texture slots for each odd block, attaches the
       matching texture set to each model, then builds runtime data immediately. */
    unsigned int linkSize;
    unsigned int blockCount;
    int modelIndex;

    if (collection == 0) {
        return -1;
    }

    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return -1;
    }

    blockCount = CzanLinkResource_GetBlockCount(linkData, linkSize);
    if (modelCount < 0) {
        modelCount = 0;
    }
    if ((unsigned int)(modelCount * 2) > blockCount) {
        modelCount = (int)(blockCount / 2);
    }

    collection[0] = modelCount;
    collection[1] = (int)collectionIndex;
    for (modelIndex = 0; modelIndex < modelCount && modelIndex < 0x20; modelIndex++) {
        CzanLinkBlock modelBlock;
        CzanLinkBlock textureBlock;
        int *model = (int *)calloc(1, 0x2d0);
        int textureSlot = -1;

        if (model == 0) {
            break;
        }
        CzanModel_Init(model);

        if (CzanLinkResource_GetBlock(linkData, linkSize, (unsigned int)(modelIndex * 2), &modelBlock)) {
            HostCzan_RegisterLinkSize(modelBlock.data, modelBlock.size);
            CzanModel_SetPrimaryBlock(model, (int)(uintptr_t)modelBlock.data, (int)modelBlock.size);
            CzanModel_SetHostPrimaryBlock(model, (void *)modelBlock.data, modelBlock.size);
        }
        if (CzanLinkResource_GetBlock(linkData, linkSize, (unsigned int)(modelIndex * 2 + 1), &textureBlock)) {
            HostCzan_RegisterLinkSize(textureBlock.data, textureBlock.size);
            textureSlot = (int)CreateTextureFromTplResource(
                (TextureManagerKnownFields *)GlobalRuntimeContext_GetPointerAt(0x26c),
                (void *)textureBlock.data,
                (int)textureBlock.size,
                0xffffffffu);
            CzanModel_AttachTextureSet(model, textureSlot);
        }

        CzanModel_BuildRuntimeDataAndUpdateTransforms(model);
        collection[2 + modelIndex] = (int)(uintptr_t)model;
    }
    return modelIndex;
}

int CzanModelManager_LoadResource(int *manager, unsigned int bankIndex, void *linkData) {
    /* 0x8017872C loads one manager bank. Block 0 is metadata/header, block 1 is an
       optional TPL texture resource, and block 2 is an optional CzanModel
       collection loaded through CzanModelCollection_LoadFromLinkBlocks. */
    unsigned int linkSize;
    CzanLinkBlock block;
    int *bank;
    int modelCount = 0;

    if (manager == 0) {
        return 0;
    }

    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return 0;
    }

    bankIndex &= 0xffu;
    bank = (int *)((unsigned char *)manager + bankIndex * 0x10);

    if (CzanLinkResource_GetBlock(linkData, linkSize, 0, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        *(int *)((unsigned char *)bank + 0x124) = (int)(uintptr_t)block.data;
        *(int *)((unsigned char *)bank + 0x128) = (int)(uintptr_t)(block.data + 0x10);
        if (block.size >= 0x18) {
            modelCount = (int)CzanModel_ReadBe16(block.data + 0x16);
        }
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 1, &block)) {
        int textureSlot;
        HostCzan_RegisterLinkSize(block.data, block.size);
        textureSlot = (int)CreateTextureFromTplResource(
            (TextureManagerKnownFields *)GlobalRuntimeContext_GetPointerAt(0x26c),
            (void *)block.data,
            (int)block.size,
            0xffffffffu);
        *(int *)((unsigned char *)bank + 0x12c) = textureSlot;
        manager[0x18 / 4] = textureSlot;
    }
    else {
        manager[0x18 / 4] = -1;
    }

    if (CzanLinkResource_GetBlock(linkData, linkSize, 2, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CzanModelCollection_LoadFromLinkBlocks(
            (int *)((unsigned char *)manager + 0xac),
            (void *)block.data,
            modelCount,
            bankIndex);
    }
    return 1;
}

int CzanModelManager_UnloadBank(int *manager, unsigned int bankIndex) {
    /* 0x80178B8C releases one model-manager bank. It destroys live objects whose
       owner byte at +0x12C matches the bank index, releases the bank texture slot,
       clears the loaded flag and slot, then refreshes manager state. */
    (void)bankIndex;
    if (manager == 0) {
        return 0;
    }

    return 1;
}

void CzanModelManager_Clear(int *manager) {
    /* 0x80178A68 destroys every live model object, unloads all eight banks, frees
       the live object pointer array, then resets the manager's internal lists. */
    if (manager == 0) {
        return;
    }
}

int CzanModelManager_ClearLiveObjects(int *manager) {
    /* 0x80178C8C destroys every live object in manager[0x69] without unloading
       model banks or resetting the manager's other lists. */
    if (manager == 0) {
        return 0;
    }

    return 1;
}

void CzanModelManager_UpdateVisibleGroup(void *context, void *unused, int removeFinished, char groupId) {
    /* 0x80178DE4 updates live objects whose object byte +0x12D matches groupId.
       It updates manager camera/view matrices, toggles per-object visibility,
       advances matching objects through FUN_8017E720 when they are not finished,
       and optionally destroys finished objects when removeFinished is nonzero. */
    (void)context;
    (void)unused;
    (void)removeFinished;
    (void)groupId;
}

void CzanModelObject_UnregisterManagerEntries(int *object) {
    /* 0x8017E5F8 unregisters an object's manager entries before the object is
       destroyed. Each registration record is 0x10 bytes at object +0x118, and the
       record type byte at *(record[0]) +2 selects which manager/list owns record[3]. */
    if (object == 0) {
        return;
    }
}

void CzanModelObject_Update(double delta, unsigned int *object) {
    /* 0x8017E720 updates one live model-manager object. It waits for registered
       dependencies when flag 4 is set, updates attached registrations while flag 1
       is set, handles fade-in/fade-out timers through flags 0x10/0x20, and marks
       the object finished with flag 8. */
    (void)delta;
    if (object == 0) {
        return;
    }
}

void CzanModelObject_UpdateRegistrationTarget(double delta, unsigned int *object, int *registration) {
    /* 0x8017F7F8 pushes one live object's current fade/value state into a
       registered target, calls the target's vtable +0x0C update method, logs bad
       linked-list pointers, then recursively processes linked child registrations. */
    (void)delta;
    (void)registration;
    if (object == 0) {
        return;
    }
}

void CzanModelManager_LoadBank1Resource(void *unused, void *linkData) {
    /* 0x8004EBA8 is a tiny wrapper that loads bank 1 into gManager_802E70B8. */
    (void)unused;
    (void)linkData;
}

void CzanModelManager_SetupBank3AndStageObjects(
    void *context,
    void *unused,
    unsigned char flagA,
    unsigned char flagB,
    void *bank3LinkData,
    void *stageObjectLinkData) {
    /* 0x8004EC4C clears five existing CtsStageObj pointers, clears manager-local
       state, optionally loads bank 3 into gManager_802E70B8, stores two flags, then
       optionally builds five CtsStageObj instances from grouped model/texture/
       continuation blocks in stageObjectLinkData. */
    (void)context;
    (void)unused;
    (void)flagA;
    (void)flagB;
    (void)bank3LinkData;
    (void)stageObjectLinkData;
}

void CzanModelManager_SwitchBank5ForMode(int *owner) {
    /* 0x80053B90 switches model manager bank 5 when the owner mode byte changes.

       Confirmed owner fields:
       owner +0x002D -> current mode/index byte
       owner +0x002E -> previous mode/index byte
       owner +0xB0B8 -> cached value copied from +0xB378 during switch
       owner +0xB0D4 + mode*4 -> per-mode link/resource pointer
       owner +0xB340 + mode*4 -> per-mode gManager_802E70A8 handle/id
       owner +0xB34C -> alternate handle/id when mode == 3
       owner +0xB36C -> boolean flag passed to MovieSlotHandle_SetObjectEnabled after reload
       owner +0xB370 -> pending/transition flag
       owner +0xB374 -> requested mode/index byte
       owner +0xB378 -> cached value copied to +0xB0B8
       owner +0xB37C -> transition/lock flag

       Original behavior:
       - if +0xB370 is set but +0xB37C is clear, clear +0xB370
       - when requested mode differs from current mode and no transition is pending:
         unload CzanModelManager bank 5
         disable/clear the old mode handle through MovieSlotHandle_SetObjectEnabled when present
         copy current mode to previous mode, requested mode to current mode
         if the new mode has a link/resource pointer, load bank 5 and call FUN_80053124(owner)
         enable/update the new mode handle through MovieSlotHandle_SetObjectEnabled */
    if (owner == 0) {
        return;
    }
}

void CzanModelManager_StopBank5ModeEffects(double stopTime, int *owner) {
    /* 0x800534F0 stops/clears bank-5 live effects for the owner's current mode.

       Original flow:
       - walks 0x104 twelve-byte live-effect records in the current mode block at
         owner +0x10000 + currentMode*0xC30 -0x75F8
       - when both the source id and live effect handle are valid, calls
         CzanEffectManager_SetStopTime(stopTime, gManager_802E70B8, handle, 1),
         clears the live handle to -1, and clears the small state byte to 0xFF
       - walks the smaller per-mode effect list counted by owner +0xB0E0[currentMode],
         base owner +0x10000 + currentMode*0xC0 -0x4F1C
       - stops each valid live effect handle and clears it to -1

       ActiveGameplayControllerBase_ApplyRuntimeEventChannels calls this when its
       selected event group changes. */
    (void)stopTime;
    (void)owner;
}

void CzanModelManager_UpdateCurrentModeMatrices(int *owner, const void *modelMatrix, const void *objectMatrix) {
    /* 0x80052D98 updates the live-object matrices for the current bank-5 mode.

       Confirmed behavior:
       - uses owner byte +0x2D as the current mode index
       - if the current mode's count/handle at +0x20 is positive, resolves the active
         model transform through CtsStageObjDescriptor_GetModelTransform
       - writes matrices into the per-mode live object record at
         owner + currentMode*0x328 +0xE6F0 through CzanModelLiveObject_SetModelMatrices */
    (void)owner;
    (void)modelMatrix;
    (void)objectMatrix;
}

void CzanModelManager_RequestBank5ModeTransition(double duration, int *owner, unsigned int modeIndex, int transitionAnimIndex) {
    /* 0x8005386C requests a bank-5/model-owner mode transition.

       It validates modeIndex against owner +0x2C, records requested mode/kind at
       +0xB374/+0xB378, chooses transition state +0xB0C4, sets transition flags
       +0xB370/+0xB37C/+0xE6E8, stops existing live/effect objects for the current
       mode, resets +0xB0BC/+0xB0C0 timing, then calls
       CzanModelManager_SwitchBank5ForMode(owner). */
    (void)duration;
    (void)owner;
    (void)modeIndex;
    (void)transitionAnimIndex;
}

void CzanModelManager_InitBank5LiveObjectsForMode(int *owner) {
    /* 0x80053124 is called immediately after CzanModelManager_SwitchBank5ForMode
       loads bank 5 for the requested/current mode.

       Ghidra may show void(void) because RuntimeContext_SpillSavedRegisters recovers
       the owner pointer.

       Confirmed owner fields:
       owner +0x002D -> current mode/index byte
       owner +0x8A04 -> group/category byte passed to FUN_8017911C
       owner +0xB0E0 + mode -> count of 0x0C-byte entries for that mode
       owner +0xB0E4 + mode*0xC0 + entry*0x0C -> per-mode entry table
       entry +0x00 -> transform/position id, must not be -1
       entry +0x04 -> live model object handle, created when -1
       entry +0x08 -> signed model/resource id byte, must not be -1
       entry +0x0A -> signed bank/model slot byte, passed to FUN_8017911C

       Original flow for each valid entry:
       - create a live model object with FUN_8017911C(1.0, gManager_802E70B8,
         entry[0x0A], bank 5, owner +0x8A04)
       - store the returned handle at entry +0x04
       - get an owner slot/index through FUN_80054F88(owner)
       - if the selected owner slot has transform data, fetch transform/position data
         through FUN_8005941C(slot +0xAC, stackTransform, entry[0])
       - apply that transform to the live model object with
         FUN_801791F0(gManager_802E70B8, handle, stackTransform) */
    if (owner == 0) {
        return;
    }
}

void CzanModelOwner_CopyCurrentModelMatrix(int *owner, void *outMatrix) {
    /* 0x8015EAD0 copies the current model-owner matrix from owner +0x4C.

       Select-common updates copy this into selectCommon +0x18 before drawing the
       two CtsStageObj layers. */
    if (owner == 0 || outMatrix == 0) {
        return;
    }

    Matrix34_Copy((float *)outMatrix, (const float *)((const unsigned char *)owner + 0x4c));
}

int CtsStageObjDescriptor_GetModelTransform(int descriptor) {
    /* 0x80059294 returns descriptor +0x1C. The active controller uses this as the
       transform source passed to CzanModelLiveObject_SetModelMatrices. */
    if (descriptor == 0) {
        return 0;
    }
    return descriptor + 0x1c;
}

void CzanModelLiveObject_SetModelMatrices(int *object, int transformSource, const void *modelMatrix, const void *objectMatrix) {
    /* 0x8011F8A8 writes matrix state into a bank-5/current-mode live object record.

       Confirmed behavior:
       - if transformSource is nonzero, copies source matrix data into object +0x284
       - if modelMatrix is nonzero, copies a 3x4 matrix into object +0x2B4
       - if objectMatrix is nonzero, copies a 4x4 matrix into object +0x2E4 */
    (void)object;
    (void)transformSource;
    (void)modelMatrix;
    (void)objectMatrix;
}

void CzanEffectManager_SetStopTime(int *manager, int effectHandle, int stopTime) {
    /* 0x80179178 sets stop time for one Czan effect-manager live object.

       Original flow:
       - return when effectHandle < 0
       - object = *(manager +0x1A4)[effectHandle]
       - if object is null, log "CzanEffMng::set_stop_t() : NULL!"
       - otherwise call FUN_8017F0B0(object, stopTime) */
    (void)manager;
    (void)effectHandle;
    (void)stopTime;
}

int CzanEffectManager_StartEffect(
    int *manager,
    int effectId,
    int effectParam,
    int enabled,
    const void *transform,
    int ownerOrArg,
    int stopTime) {
    static int nextHostEffectHandle;
    int handle;

    /* 0x8016B270 starts/spawns an effect through the global manager at
       DAT_802E71B8 +0x268. The known callers pass an effect id, an auxiliary
       parameter resolved by FUN_800F3158/FUN_800F3068, an enabled/immediate flag,
       a stack transform initialized by FUN_80166D54, an owner/arg, and -1 stop time.

       The real effect-pool allocator is still unmapped; the host returns stable
       synthetic handles so higher-level state machines can preserve ownership flow. */
    (void)manager;
    (void)effectParam;
    (void)enabled;
    (void)transform;
    (void)ownerOrArg;
    (void)stopTime;

    if (effectId < 0) {
        return -1;
    }
    handle = nextHostEffectHandle++;
    printf("CzanEffectManager: start effect id=%d handle=%d\n", effectId, handle);
    return handle;
}

void CzanModelOwner_LoadStageResourceGroup(int *owner, void *linkData) {
    /* 0x80054CF4 loads one owner-side stage/model resource group from a WII link.

       Confirmed flow:
       - root block 0, when present, is passed to LoadZmbZabModelEntryList(owner, block0)
       - root block 2 is a candidate model-manager bank 5 resource block
       - root block 3, when present, is a nested WII link; nested block 0 carries
         a small metadata/name record, and nested block 1 is forwarded with block 2
         through FUN_80054C00(owner, owner +0x8A04, block2, nestedBlock1)
       - when nested metadata exists, the first byte is stored at
         owner +0xB324 + groupIndex and the metadata string at +4 is copied to a
         newly allocated buffer stored at owner +0xB328 + groupIndex*4
       - if block2 and nested block1 exist for group 0, bank 5 is loaded from block2
       - FUN_800554B0(owner, groupIndex) finalizes the group, then owner +0x2C is
         incremented when any useful block existed

       This is not the draw path; it is the resource grouping path that prepares
       ZMB/ZAB model entries and the bank-5 resources later used by live objects. */
    (void)linkData;
    if (owner == 0) {
        return;
    }
}

void CzanModelOwner_SetCategoryAndLoadStageResourceGroup(int *owner, unsigned char groupCategory, void *linkData) {
    /* 0x8004EB54 stores the active resource-group category byte at owner +0x8A04,
       ensures group handle 3 exists, then loads the supplied stage resource group
       through CzanModelOwner_LoadStageResourceGroup.

       This is the small wrapper used by stage/menu logic when switching which bank-5
       model/effect resource group is active. */
    (void)linkData;
    if (owner == 0) {
        return;
    }
    *((unsigned char *)owner + 0x8a04) = groupCategory;
    CzanModelOwner_EnsureResourceGroupHandle(owner, 3);
}

void CzanModelOwner_SetupResourceGroupEntries(int *owner, unsigned char groupCategory, void *bank5LinkData, void *entryListLinkData) {
    /* 0x80054C00 stores metadata for the current owner resource group.

       Parameters:
       - owner: model/stage owner object
       - groupCategory: byte copied to owner +0xB0D0 + groupIndex
       - bank5LinkData: pointer copied to owner +0xB0D4 + groupIndex*4
       - entryListLinkData: optional list parsed by FUN_8018CD38/FUN_8018CDBC

       Confirmed fields:
       owner +0x002C -> current group index
       owner +0xB0D0 + group -> group category byte
       owner +0xB0D4 + group*4 -> bank/resource link pointer
       owner +0xB0EC + group*0xC0 + entry*0x0C +0 -> source byte 1
       owner +0xB0EC + group*0xC0 + entry*0x0C +1 -> source byte 2
       owner +0xB0EC + group*0xC0 + entry*0x0C +2 -> source byte 0

       This prepares the compact per-group live-object/resource entry table that
       later code reads at owner +0xB0E4/+0xB0EC ranges. */
    (void)groupCategory;
    (void)bank5LinkData;
    (void)entryListLinkData;
    if (owner == 0) {
        return;
    }
}

int CzanModelOwner_EnsureResourceGroupHandle(int *owner, unsigned int groupIndex) {
    /* 0x800554B0 creates the per-resource-group handle through
       ResourceSlotManager_AllocateSlot / FUN_80024EA8 when the owner is not locked
       by +0xB36C.

       Confirmed fields:
       owner +0xB324 + group -> metadata byte set by CzanModelOwner_LoadStageResourceGroup
       owner +0xB340 + group*4 -> per-group handle/id, initialized to -1
       owner +0xB34C -> special handle/id used for group 3
       owner +0xB354 + group*4 -> per-group resource/link presence test
       owner +0xB36C -> lock/disable flag; when nonzero no handle is created

       Return value is 1 when a new handle is created, otherwise 0. Group 3 stores
       its handle in +0xB34C instead of the normal +0xB340 table. */
    (void)groupIndex;
    if (owner == 0) {
        return 0;
    }

    return 0;
}

void CzanModelLiveObject_Init(int *object) {
    /* 0x8017911C is reached by the live-object creation path used by
       CzanModelManager_InitBank5LiveObjectsForMode. The caller-facing decompile
       shows extra arguments and a return handle; this tiny decompile is likely the
       constructor/init body after allocation.

       Confirmed body:
       - object +0x120 = -1
       - calls FUN_80179330(object/remaining recovered args)

       Keep the name conservative until the allocator/registration wrapper around
       this init body is fully mapped. */
    if (object == 0) {
        return;
    }
    object[0x48] = -1;
}

void CzanModelManager_SetLiveObjectMatrix(int *manager, int liveObjectHandle, const void *matrix) {
    /* 0x801791F0 applies matrix/transform data to one live object handle.

       Original flow:
       - if liveObjectHandle < 0, return
       - object = *(manager +0x1A4)[liveObjectHandle]
       - if object is null, log "CzanEffMng::set_mtx(): NULL!!!!"
       - otherwise call FUN_8017DA24(object +4, matrix). The pasted decompile of
         FUN_8017DA24 only recovered one parameter, but this call site passes the
         matrix/transform argument as well. */
    (void)matrix;
    if (manager == 0 || liveObjectHandle < 0) {
        return;
    }
}

void CzanModelLiveObject_ApplyGlobalScaleToMatrix(int *liveObjectTransform) {
    /* 0x8017DA24 normalizes/copies the live object's matrix at transform +0xDC,
       then multiplies the 3x3 basis values by the CzanModelManager global scale
       at manager +0x2A4, retrieved through FUN_80178270().

       Confirmed scaled float offsets relative to liveObjectTransform:
       +0xDC, +0xE0, +0xE4,
       +0xEC, +0xF0, +0xF4,
       +0xFC, +0x100, +0x104. */
    if (liveObjectTransform == 0) {
        return;
    }
}

unsigned char CzanModelOwner_SelectModeSlot(int *owner) {
    unsigned char currentMode;

    /* 0x80054F88 chooses which owner mode slot should supply transform data.

       It normally returns owner +0x2D (current mode). During mode-switch/blend
       state, when owner +0xB0C4 == 2, it can return owner +0x2E (previous mode)
       based on the blend timer fields at +0xB0BC/+0xB0C0, active flag +0xE6E8,
       and lock flag +0xB37C. */
    if (owner == 0) {
        return 0;
    }

    currentMode = *(unsigned char *)((unsigned char *)owner + 0x2d);
    return currentMode;
}

void CzanModelPositionSet_Clear(int *positionSet) {
    /* 0x80117A9C clears/releases the model position-set structure before
       FUN_801175B8 repopulates it. The exact owned fields still need mapping. */
    if (positionSet == 0) {
        return;
    }
}

void CzanModelPositionSet_LoadFromLinkList(int *positionSet, void **linkDataList, int linkDataCount) {
    /* 0x801175B8 loads a list of WII model resources into 0x38-byte entries.
       For each link resource it:
       - clears the old position set through FUN_80117A9C
       - stores linkDataCount at positionSet[0]
       - allocates positionSet[1] as linkDataCount * 0x38
       - treats every block in each WII resource as one CzanModel primary block
       - allocates blockCount CzanModels and calls CzanModel_SetPrimaryBlock +
         CzanModel_SetEnabled(model, 1) for each one
       - scans every model object's name for strings such as s1_pos...
       - builds six lookup/count tables from those named objects

       This is a model/position lookup loader, not the draw function. */
    (void)linkDataList;
    if (positionSet == 0) {
        return;
    }

    CzanModelPositionSet_Clear(positionSet);
    positionSet[0] = linkDataCount;
}

int CzanModel_FindObjectIndexByName(int *model, int objectName) {
    /* 0x8014E8E8 scans the object table in the primary model block at
       *(model +0x04) +0x20. Each entry is 0xA0 bytes, and FUN_801332F0 compares
       the entry name/key against objectName. Returns the matching index or -1. */
    if (model == 0 || model[1] == 0) {
        return -1;
    }

    (void)objectName;
    return -1;
}

int CzanModel_GetObjectTransform(int *model, int objectIndex) {
    /* 0x801568D4 returns the runtime object transform pointer:

       *(model +0x20) + objectIndex * 0x30

       The object transform array is allocated/populated by CzanModel_BuildRuntimeData
       and consumed by draw/effect code when composing model object matrices. */
    if (model == 0 || objectIndex < 0 || model[8] == 0) {
        return 0;
    }

    return model[8] + objectIndex * 0x30;
}

static int CzanModel_FindKeyIndex(double frame, const float *keys, int keyCount, int keyStrideFloats, int cachedKeyIndex) {
    int keyIndex;

    if (keyCount <= 0) {
        return 0;
    }

    if (cachedKeyIndex < 0 || cachedKeyIndex >= keyCount) {
        cachedKeyIndex = 0;
    }

    keyIndex = cachedKeyIndex;
    while (keyIndex < keyCount && frame >= (double)keys[keyIndex * keyStrideFloats]) {
        if (keyIndex + 1 >= keyCount || frame < (double)keys[(keyIndex + 1) * keyStrideFloats]) {
            break;
        }
        keyIndex++;
    }

    return keyIndex;
}

int CzanModel_EvaluateTranslationKeys(double frame, double duration, float *outVec3, const void *keys, int keyCount, int loop, int cachedKeyIndex) {
    const float *keyFloats;
    int keyIndex;
    int nextIndex;
    float t;

    /* 0x8015D9C4 evaluates translation keyframes. Each key is 0x10 bytes:
       time, x, y, z. It returns the key index to cache for the next call. */
    if (outVec3 == 0 || keys == 0 || keyCount <= 0) {
        return 0;
    }

    keyFloats = (const float *)keys;
    keyIndex = CzanModel_FindKeyIndex(frame, keyFloats, keyCount, 4, cachedKeyIndex);
    nextIndex = keyIndex + 1;
    if (keyCount < 2 || nextIndex >= keyCount) {
        nextIndex = (loop != 0 && keyCount > 1) ? 0 : keyIndex;
    }

    if (nextIndex == keyIndex) {
        outVec3[0] = keyFloats[keyIndex * 4 + 1];
        outVec3[1] = keyFloats[keyIndex * 4 + 2];
        outVec3[2] = keyFloats[keyIndex * 4 + 3];
        return keyIndex;
    }

    t = (float)((frame - (double)keyFloats[keyIndex * 4]) /
        ((nextIndex == 0 ? duration : (double)keyFloats[nextIndex * 4]) - (double)keyFloats[keyIndex * 4]));
    outVec3[0] = keyFloats[keyIndex * 4 + 1] + (keyFloats[nextIndex * 4 + 1] - keyFloats[keyIndex * 4 + 1]) * t;
    outVec3[1] = keyFloats[keyIndex * 4 + 2] + (keyFloats[nextIndex * 4 + 2] - keyFloats[keyIndex * 4 + 2]) * t;
    outVec3[2] = keyFloats[keyIndex * 4 + 3] + (keyFloats[nextIndex * 4 + 3] - keyFloats[keyIndex * 4 + 3]) * t;
    return keyIndex;
}

int CzanModel_EvaluateRotationKeys(double frame, double duration, float *outQuat, const void *keys, int keyCount, int loop, int cachedKeyIndex) {
    const float *keyFloats;
    int keyIndex;
    int nextIndex;
    float t;
    int i;

    /* 0x8015DACC evaluates rotation keyframes. Each key is 0x14 bytes:
       time plus four rotation/quaternion floats. The original interpolation helper
       is FUN_80145FDC; the host placeholder stores component-interpolated values. */
    if (outQuat == 0 || keys == 0 || keyCount <= 0) {
        return 0;
    }

    keyFloats = (const float *)keys;
    keyIndex = CzanModel_FindKeyIndex(frame, keyFloats, keyCount, 5, cachedKeyIndex);
    nextIndex = keyIndex + 1;
    if (keyCount < 2 || nextIndex >= keyCount) {
        nextIndex = (loop != 0 && keyCount > 1) ? 0 : keyIndex;
    }

    if (nextIndex == keyIndex) {
        for (i = 0; i < 4; i++) {
            outQuat[i] = keyFloats[keyIndex * 5 + 1 + i];
        }
        return keyIndex;
    }

    t = (float)((frame - (double)keyFloats[keyIndex * 5]) /
        ((nextIndex == 0 ? duration : (double)keyFloats[nextIndex * 5]) - (double)keyFloats[keyIndex * 5]));
    for (i = 0; i < 4; i++) {
        outQuat[i] = keyFloats[keyIndex * 5 + 1 + i] +
            (keyFloats[nextIndex * 5 + 1 + i] - keyFloats[keyIndex * 5 + 1 + i]) * t;
    }
    return keyIndex;
}

int CzanModel_EvaluateScaleKeys(double frame, double duration, float *outVec3, const void *keys, int keyCount, int loop, int cachedKeyIndex) {
    /* 0x8015DBD8 is structurally identical to the translation evaluator: 0x10-byte
       keys holding time, x, y, z. */
    return CzanModel_EvaluateTranslationKeys(frame, duration, outVec3, keys, keyCount, loop, cachedKeyIndex);
}

void CzanModel_UpdateAnimationChannel(int *model, int holdFrame, int channelIndex) {
    /* 0x8015627C updates one animation channel at model +0x230 + channel*0x24.
       Channel 0 drives the main transform update. The original advances the
       channel timer in ZAB frame units, handles loop/end flags, updates animation blending through
       FUN_801564BC/FUN_8015601C, and calls CzanModel_UpdateObjectTransforms for
       channel 0. */
    unsigned char *channel;
    float frame;
    float speed;
    float frameScale;
    float duration;
    float delta;

    if (model == 0) {
        return;
    }

    channel = (unsigned char *)model + 0x230 + channelIndex * 0x24;
    frame = *(float *)(void *)(channel + 0x00);
    speed = *(float *)(void *)(channel + 0x20);
    frameScale = *(float *)(void *)((unsigned char *)model + 0x19c);
    duration = *(float *)(void *)(channel + 0x10);
    delta = 0.0f;

    if (frameScale == 0.0f) {
        frameScale = 1.0f;
    }

    if (!holdFrame && duration > 0.0f) {
        delta = speed * frameScale;
        frame += delta;
        if (speed < 0.0f) {
            if (frame > 0.0f) {
                channel[0x15] = 0;
            }
            else {
                if (channel[0x08] == 1) {
                    while (frame <= 0.0f) {
                        frame += duration;
                    }
                }
                else {
                    frame = 0.0f;
                }
                channel[0x14] = 1;
                channel[0x15] = 1;
            }
        }
        else if (frame < duration) {
            channel[0x15] = 0;
        }
        else {
            if (channel[0x08] == 1) {
                while (frame >= duration) {
                    frame -= duration;
                }
            }
            else {
                frame = duration;
            }
            channel[0x14] = 1;
            channel[0x15] = 1;
        }
        *(float *)(void *)(channel + 0x00) = frame;
    }

    if (channelIndex == 0) {
        CzanModel_UpdateHostRuntimeObjectTransformsFromBlocks(
            model,
            *(float *)(void *)((unsigned char *)model + 0x230));
        CzanModel_UpdateObjectTransforms(delta, model);
    }
}

void CzanModel_ApplyAnimationChannelFrame(double frame, int *model, int channelIndex) {
    /* 0x8015601C applies one animation channel's current frame to every object
       local transform. It reads channel data at model +0x230 + channel*0x24,
       per-object animation records from model +0x14, and updates local transforms
       at model +0x1C using translation, rotation, and scale keyframe helpers. */
    (void)frame;
    (void)channelIndex;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_BlendAnimationChannelFrame(double deltaOrScale, int *model, float *channel, int channelIndex) {
    /* 0x801564BC blends one animation channel frame into existing per-object
       animation transforms. It evaluates translation/rotation/scale keyframes into
       temporary matrices, blends them into the per-object animation record at
       model +0x14 using channel[7] as weight unless the object has a forced weight
       flag, then updates object transforms for channel 0. */
    (void)deltaOrScale;
    (void)channel;
    (void)channelIndex;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_SolveObjectAnimationTransform(void *outMatrix, void *objectWorkspace, int *objectAnimState, int *objectAnimConfig) {
    /* 0x8018B4BC solves the secondary per-object animation transform used by
       CzanModel_UpdateObjectAnimation / 0x801574B0.

       The caller passes:
       - outMatrix: stack transform later copied into model +0x1C + objectIndex*0x30
       - objectWorkspace: model +0x1CC + objectIndex*0xB4
       - objectAnimState: model +0x1C0 + objectIndex*0x24
       - objectAnimConfig: objectEntry +0x9C

       Confirmed behavior:
       - composes matrices from objectAnimState[0..2]
       - uses objectAnimConfig flags/fields to correct or blend the solved vector
       - maintains previous/current matrices in the object workspace
       - writes the solved matrix back to outMatrix and updates workspace state

       This is downstream from raw ZAB key evaluation: the keyframes have already
       produced runtime object animation state, and this helper turns that state into
       the final local object transform. */
    (void)outMatrix;
    (void)objectWorkspace;
    (void)objectAnimState;
    (void)objectAnimConfig;
}

void CzanModel_UpdateObjectAnimation(double deltaOrScale, int *model, int objectIndex) {
    /* 0x801574B0 updates one object's animation transform when model +0x1BC is
       enabled and the object table entry byte +0x98 is nonzero. It writes the
       current delta/scale into model +0x1C0 object state, evaluates animation data
       through CzanModel_SolveObjectAnimationTransform, converts it into the object's
       local transform at model +0x1C, then recomposes the world transform at
       model +0x20. */
    (void)deltaOrScale;
    (void)objectIndex;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_UpdateType2WeightedVectors(int *model, int *objectEntry, int *drawContext) {
    /* 0x8014F7D0 updates the per-submesh weighted vector buffer used by type-2
       object entries before the type-2 part tree draw path.

       Ghidra recovers model/objectEntry through the saved-register helper
       FUN_8012A134. The explicit drawContext argument is the model +0x34 runtime
       entry passed by CzanModel_UpdateObjectTransforms.

       Confirmed behavior:
       - objectEntry +0x9A is the submesh count, and +0x9C points at 0x40-byte
         per-submesh records.
       - each drawContext submesh record is 0x10 bytes; drawContext +0x04 points at
         output buffers used by later type-2 draw submitters.
       - source transforms come from model +0x28, stride 0x30.
       - for each weighted source record, it copies a matrix/vector, applies a
         weight at source +0x3C, accumulates into the output float3 buffer, then
         flushes the buffer with FlushDataCacheRange.

       This is not final drawing; it prepares the type-2 generated vector arrays that
       CzanModel_DrawType2PartTree and the type-2 primitive submitters consume. */
    if (model == 0 || objectEntry == 0 || drawContext == 0) {
        return;
    }
}

void CzanModel_UpdateObjectTransforms(double deltaOrScale, int *model) {
    /* 0x8015759C updates runtime transform arrays for every object entry in the
       primary model block. It walks the 0xA0-byte object table, composes parent
       transforms from model +0x1C/+0x20, optionally writes +0x24/+0x28 skinning or
       alternate transform arrays, updates type-2 weighted vectors through
       CzanModel_UpdateType2WeightedVectors, updates object animation data through
       CzanModel_UpdateObjectAnimation, then finalizes through FUN_801576FC. */
    const unsigned char *zmb;
    unsigned int zmbSize;
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    unsigned int objectIndex;

    (void)deltaOrScale;
    if (model == 0 || model[1] == 0 || model[7] == 0 || model[8] == 0) {
        return;
    }

    zmb = (const unsigned char *)CzanModel_GetHostPrimaryBlock(model);
    zmbSize = CzanModel_GetHostPrimaryBlockSize(model);
    if (zmb == 0 || zmbSize < 0x30 || memcmp(zmb, "ZMB ", 4) != 0) {
        return;
    }

    objectTableOffset = CzanModel_ReadBe32(zmb + 0x20);
    if (objectTableOffset > zmbSize || zmbSize - objectTableOffset < 0x0c) {
        return;
    }

    objectCount = CzanModel_ReadBe32(zmb + objectTableOffset);
    objectEntryOffset = CzanModel_ReadBe32(zmb + objectTableOffset + 8);
    if (objectCount == 0 || objectEntryOffset > zmbSize) {
        return;
    }
    if (objectCount > (unsigned int)model[0x26]) {
        objectCount = (unsigned int)model[0x26];
    }

    for (objectIndex = 0; objectIndex < objectCount; objectIndex++) {
        unsigned int entryOffset = objectEntryOffset + objectIndex * 0xa0u;
        unsigned int parentIndex;
        float *localMatrix;
        float *worldMatrix;

        if (entryOffset + 0x98u > zmbSize) {
            break;
        }

        localMatrix = (float *)(uintptr_t)(model[7] + (int)(objectIndex * 0x30u));
        worldMatrix = (float *)(uintptr_t)(model[8] + (int)(objectIndex * 0x30u));
        parentIndex = CzanModel_ReadBe32(zmb + entryOffset + 0x94);
        if ((int)parentIndex < 0 || parentIndex >= objectIndex || parentIndex >= objectCount) {
            Matrix34_Copy(worldMatrix, localMatrix);
        }
        else {
            const float *parentWorld = (const float *)(uintptr_t)(model[8] + (int)(parentIndex * 0x30u));
            Matrix34_Multiply(worldMatrix, parentWorld, localMatrix);
        }
    }

    model[0x164 / 4] = 0;
}

static CzanModelHostDrawCache *CzanModel_GetHostDrawCache(int *model) {
    unsigned int i;
    CzanModelHostDrawCache *freeCache = 0;

    if (model == 0) {
        return 0;
    }
    for (i = 0; i < CZAN_MODEL_HOST_DRAW_CACHE_CAP; i++) {
        if (gCzanModelHostDrawCaches[i].model == model) {
            return &gCzanModelHostDrawCaches[i];
        }
        if (freeCache == 0 && gCzanModelHostDrawCaches[i].model == 0) {
            freeCache = &gCzanModelHostDrawCaches[i];
        }
    }
    if (freeCache == 0) {
        freeCache = &gCzanModelHostDrawCaches[
            gCzanModelHostDrawCacheCursor++ % CZAN_MODEL_HOST_DRAW_CACHE_CAP];
    }
    memset(freeCache, 0, sizeof(*freeCache));
    freeCache->model = model;
    return freeCache;
}

static int CzanModel_ProjectHostPoint(float x, float y, float z, int *outX, int *outY) {
    float depth;
    float screenCenterX;
    float screenCenterY;
    float fovDeg;
    float aspect;
    float f;
    float focalX;
    float focalY;

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
    fovDeg = gCzanModelHostProjectionActive ? gCzanModelHostProjectionFovDeg : 45.0f;
    aspect = gCzanModelHostProjectionActive ? gCzanModelHostProjectionAspect : 1.333333373f;
    if (fovDeg <= 0.00001f) {
        fovDeg = 45.0f;
    }
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

static void CzanModel_RebuildHostDrawCache(CzanModelHostDrawCache *cache, int *model, int drawMode) {
    CzanModelSubmittedPrimitiveBuffer primitiveBuffer;
    void *primaryBlock;
    unsigned int primaryBlockSize;
    void *zabBlock;
    unsigned int zabBlockSize;
    int activeAnimationIndex;
    int textureSlot;
    float animationTick;

    if (cache == 0 || model == 0) {
        return;
    }

    primaryBlock = CzanModel_GetHostPrimaryBlock(model);
    primaryBlockSize = CzanModel_GetHostPrimaryBlockSize(model);
    activeAnimationIndex = *(int *)(void *)((unsigned char *)model + 0x234);
    zabBlock = activeAnimationIndex >= 0 ? CzanModel_GetHostContinuationBlock(model, activeAnimationIndex) : 0;
    zabBlockSize = zabBlock != 0 ? HostCzan_GetRegisteredLinkSize(zabBlock) : 0;
    textureSlot = model[0x15];
    animationTick = *(float *)(void *)((unsigned char *)model + 0x230);

    if (cache->primaryBlock == primaryBlock &&
        cache->primaryBlockSize == primaryBlockSize &&
        cache->zabBlock == zabBlock &&
        cache->zabBlockSize == zabBlockSize &&
        cache->textureSlot == textureSlot &&
        cache->drawMode == drawMode) {
        return;
    }

    cache->primaryBlock = primaryBlock;
    cache->primaryBlockSize = primaryBlockSize;
    cache->zabBlock = zabBlock;
    cache->zabBlockSize = zabBlockSize;
    cache->textureSlot = textureSlot;
    cache->drawMode = drawMode;
    cache->animationTick = animationTick;
    cache->vertexCount = 0;
    cache->primitiveCount = 0;
    if (primaryBlock == 0 || primaryBlockSize == 0) {
        return;
    }

    primitiveBuffer.vertices = cache->vertices;
    primitiveBuffer.texcoords = cache->texcoords;
    primitiveBuffer.colors = cache->colors;
    primitiveBuffer.vertexObjectIndex = cache->vertexObjectIndex;
    primitiveBuffer.vertexCapacity = CZAN_MODEL_HOST_DRAW_VERTEX_CAP;
    primitiveBuffer.vertexCount = 0;
    primitiveBuffer.primitiveStart = cache->primitiveStart;
    primitiveBuffer.primitiveVertexCount = cache->primitiveVertexCount;
    primitiveBuffer.primitiveTextureIndex = cache->primitiveTextureIndex;
    primitiveBuffer.primitiveMaterialMode = cache->primitiveMaterialMode;
    primitiveBuffer.primitiveCapacity = CZAN_MODEL_HOST_DRAW_PRIMITIVE_CAP;
    primitiveBuffer.primitiveCount = 0;
    primitiveBuffer.submittedObjectCount = 0;

    CzanModel_SubmitAnimatedZmbPrimitiveStreamsWithDrawMode(
        primaryBlock,
        primaryBlockSize,
        zabBlock,
        zabBlockSize,
        animationTick,
        drawMode,
        model[8] != 0 ? (const float *)(uintptr_t)model[8] : 0,
        &primitiveBuffer);
    cache->vertexCount = primitiveBuffer.vertexCount;
    cache->primitiveCount = primitiveBuffer.primitiveCount;
}

static void CzanModel_FlushHostTriangleBatch(
    unsigned int *batchVertexCount,
    void *textureHandle,
    int textureIndex) {
    if (batchVertexCount == 0 || *batchVertexCount == 0) {
        return;
    }

    DrawTexturedTriangleList2D(
        gCzanModelHostBatchPoints,
        gCzanModelHostBatchTexcoords,
        gCzanModelHostBatchColors,
        *batchVertexCount,
        textureHandle,
        textureIndex);
    *batchVertexCount = 0;
}

static void CzanModel_DrawHostPrimitiveStreams(int *model, const float *baseMatrix, int drawMode) {
    CzanModelHostDrawCache *cache;
    int pass;

    cache = CzanModel_GetHostDrawCache(model);
    CzanModel_RebuildHostDrawCache(cache, model, drawMode);
    if (cache == 0 || cache->vertexCount == 0 || cache->primitiveCount == 0) {
        return;
    }

    for (pass = 0; pass < 2; pass++) {
        unsigned int primitiveIndex;
        int batchTextureIndex = -0x7fffffff;
        void *batchTextureHandle = 0;
        unsigned int batchVertexCount = 0;

        for (primitiveIndex = 0; primitiveIndex < cache->primitiveCount; primitiveIndex++) {
            unsigned int start = cache->primitiveStart[primitiveIndex];
            unsigned int count = cache->primitiveVertexCount[primitiveIndex];
            int textureIndex = (int)cache->primitiveTextureIndex[primitiveIndex];
            int materialBlendPass = (cache->primitiveMaterialMode[primitiveIndex] & 0x7fu) != 0;
            int textureWidth = 0;
            int textureHeight = 0;
            int normalizeTexcoords = 0;
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
                CzanModel_FlushHostTriangleBatch(
                    &batchVertexCount,
                    batchTextureHandle,
                    batchTextureIndex);
                batchTextureIndex = textureIndex;
                batchTextureHandle = cache->textureSlot >= 0 ? (void *)(intptr_t)cache->textureSlot : 0;
            }

            if (cache->textureSlot >= 0) {
                (void)GetTextureDimensions(
                    (void *)(intptr_t)cache->textureSlot,
                    textureIndex,
                    &textureWidth,
                    &textureHeight);
            }

            for (localIndex = 0; localIndex < count; localIndex++) {
                unsigned int vertexIndex = start + localIndex;
                float point[3];
                float u;
                float v;

                point[0] = cache->vertices[vertexIndex][0];
                point[1] = cache->vertices[vertexIndex][1];
                point[2] = cache->vertices[vertexIndex][2];
                if (model != 0 && model[8] != 0) {
                    unsigned int objectIndex = cache->vertexObjectIndex[vertexIndex];
                    if (objectIndex < (unsigned int)model[0x26]) {
                        CzanModel_TransformPoint(
                            (const float *)(uintptr_t)(model[8] + (int)(objectIndex * 0x30u)),
                            point,
                            point);
                    }
                }
                if (baseMatrix != 0) {
                    CzanModel_TransformPoint(baseMatrix, point, point);
                }
                if (!CzanModel_ProjectHostPoint(
                        point[0],
                        point[1],
                        point[2],
                        &points[localIndex][0],
                        &points[localIndex][1])) {
                    projectedOk = 0;
                    break;
                }

                u = cache->texcoords[vertexIndex][0];
                v = cache->texcoords[vertexIndex][1];
                if (fabsf(u) > 1.25f || fabsf(v) > 1.25f) {
                    normalizeTexcoords = 1;
                }
                texcoords[localIndex][0] = u;
                texcoords[localIndex][1] = v;
                colors[localIndex] = cache->colors[vertexIndex];
            }

            if (!projectedOk) {
                continue;
            }

            if (normalizeTexcoords && textureWidth > 0 && textureHeight > 0) {
                for (localIndex = 0; localIndex < count; localIndex++) {
                    texcoords[localIndex][0] /= (float)textureWidth;
                    texcoords[localIndex][1] /= (float)textureHeight;
                }
            }

            for (triangleIndex = 2; triangleIndex < count; triangleIndex++) {
                unsigned int src0;
                unsigned int src1;
                unsigned int src2;
                unsigned int out;

                if (batchVertexCount + 3 > CZAN_MODEL_HOST_DRAW_BATCH_VERTEX_CAP) {
                    CzanModel_FlushHostTriangleBatch(
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
                src2 = triangleIndex;
                out = batchVertexCount;

                gCzanModelHostBatchPoints[out][0] = points[src0][0];
                gCzanModelHostBatchPoints[out][1] = points[src0][1];
                gCzanModelHostBatchTexcoords[out][0] = texcoords[src0][0];
                gCzanModelHostBatchTexcoords[out][1] = texcoords[src0][1];
                gCzanModelHostBatchColors[out] = colors[src0];
                out++;

                gCzanModelHostBatchPoints[out][0] = points[src1][0];
                gCzanModelHostBatchPoints[out][1] = points[src1][1];
                gCzanModelHostBatchTexcoords[out][0] = texcoords[src1][0];
                gCzanModelHostBatchTexcoords[out][1] = texcoords[src1][1];
                gCzanModelHostBatchColors[out] = colors[src1];
                out++;

                gCzanModelHostBatchPoints[out][0] = points[src2][0];
                gCzanModelHostBatchPoints[out][1] = points[src2][1];
                gCzanModelHostBatchTexcoords[out][0] = texcoords[src2][0];
                gCzanModelHostBatchTexcoords[out][1] = texcoords[src2][1];
                gCzanModelHostBatchColors[out] = colors[src2];
                out++;

                batchVertexCount = out;
            }
        }

        CzanModel_FlushHostTriangleBatch(
            &batchVertexCount,
            batchTextureHandle,
            batchTextureIndex);
    }
}

void CzanModel_BuildSpecialObjectMatrix(int *model, float *outMatrix, const float *baseMatrix, const float *objectMatrix) {
    /* 0x8014E96C builds the alternate object matrix used by CzanModel_DrawVisibleObjects
       when model +0x140 is active and an external/base matrix is supplied. The original
       extracts basis vectors from baseMatrix, computes a facing/scale correction from
       objectMatrix, composes temporary scale/rotation matrices, and multiplies the
       adjusted result back through baseMatrix.

       This belongs to the draw transform path, not the ZMB/ZAB parser. */
    (void)model;
    (void)outMatrix;
    (void)baseMatrix;
    (void)objectMatrix;
}

void CzanModel_DrawVisibleObjects(int *model, int arg1, const void *baseMatrix, int arg2) {
    /* 0x8014F420 is called by CtsStageObj_ApplyModelTransform / FUN_800594B8 as:

         CzanModel_DrawVisibleObjects(stageObj[0], arg1, stageObj +7, arg2)

       Confirmed behavior from the full decompile:
       - clears model +0x150 and returns unless a primary model block exists and
         model alpha/visibility at +0x7C is nonzero
       - copies/composes the caller base matrix, stores arg2 at model +0x128, and
         stores the caller matrix pointer at model +0x15C
       - when model +0x140 is set and a caller matrix exists, builds an alternate
         transform through CzanModel_BuildSpecialObjectMatrix
       - walks the primary model object's 0xA0-byte table
       - only draws objects with submesh count +0x9A, visible bytes +0x28/+0x2A clear,
         and no runtime skip entry in model +0x18
       - for each object, composes matrices and dispatches each submesh to either
         CzanModel_DrawType2PartTree when object type +0x2C is 2, or
         CzanModel_DrawStandardPartTree otherwise
       - increments model +0x1A4 modulo model +0x1A0 and marks model +0x164 = 1

       This is the CzanModel visible-object draw traversal. The part-tree walkers
       then dispatch to the actual leaf draw routines at FUN_8015BD68/FUN_8015BEFC
       and FUN_801595BC/FUN_80159730. */
    if (model == 0) {
        return;
    }
    model[0x150 / 4] = 0;
    model[0x128 / 4] = arg2;
    model[0x15c / 4] = arg1;
    CzanModel_DrawHostPrimitiveStreams(model, (const float *)baseMatrix, 0);
}

void CzanModel_DrawVisibleObjectsWithMode(int *model, int arg1, const void *baseMatrix, int arg2, int drawMode) {
    /* 0x8014ED4C is the mode-filtered companion to CzanModel_DrawVisibleObjects.
       CtsStageObj_DrawModelWithFlags maps draw flags to drawMode 0/1/2, then this
       path applies the same visible-object traversal with byte +0x29 filtering:
       mode 0 draws the normal set, mode 1 draws entries tagged 1, and mode 2 draws
       entries tagged 0. The host submitters are still shared with the normal path,
       so route through the common traversal until the mode-specific leaves are
       fully ported. */
    if (model == 0) {
        return;
    }
    model[0x150 / 4] = 0;
    model[0x128 / 4] = arg2;
    model[0x15c / 4] = arg1;
    CzanModel_DrawHostPrimitiveStreams(model, (const float *)baseMatrix, drawMode);
}

void CzanModel_DrawType2PartTree(
    int *model,
    int *partIndexSource,
    int submeshIndex,
    int partTableBase,
    const void *objectMatrix,
    int childIndex,
    int *drawContext,
    int objectIndex) {
    /* 0x80151E90 recursively walks a model part/material tree for visible objects
       whose object entry type field (+0x2C) is 2.

       Recovered behavior:
       - selects a part entry at partTableBase + model[0x2E] * *partIndexSource
       - if childIndex is nonzero, switches to that child part through parent +0x2C
       - byte part +0x13 controls leaf rendering:
         - 0: container node; recurse into children
         - 1: call CzanModel_UpdateType2PartTexcoords(model, partIndexSource,
                                                      drawContext, submeshIndex,
                                                      drawContext[3] + submeshIndex * 0x10)
         - other: call CzanModel_UpdateType2SpecialPartTexcoords(model, partIndexSource,
                                                                 partEntry, drawContext,
                                                                 submeshIndex,
                                                                 drawContext[3] + submeshIndex * 0x10)
       - child count is short part +0x2A, child table pointer is part +0x2C
       - child stride is model[0x2E] (model +0xB8)

       objectMatrix and objectIndex are passed through by the caller but are not used
       directly in this wrapper; the leaf routines use drawContext/submesh metadata. */
    (void)objectMatrix;
    (void)objectIndex;
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)submeshIndex;
    (void)partTableBase;
    (void)childIndex;
}

void CzanModel_DrawStandardPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childPass,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex) {
    /* 0x80155484 recursively walks the standard/non-type-2 part tree.

       Recovered behavior:
       - selects a part entry at partTableBase + model[0x2E] * *partIndexSource
       - on childPass, switches to the first child at part +0x2C
       - byte part +0x13 controls leaf rendering:
         - 0: container node; recurse once into the first child when childPass == 0
         - 1: call CzanModel_UpdateStandardPartTexcoords(model, partIndexSource,
                                                         objectMatrix,
                                                         drawContext[3] + submeshIndex * 0x10)
         - other: call CzanModel_UpdateStandardSpecialPartTexcoords(model, partIndexSource,
                                                                    partEntry, objectMatrix,
                                                                    drawContext[3] + submeshIndex * 0x10)

       Unlike CzanModel_DrawType2PartTree, this path only follows the first child
       pointer directly in the pasted decompile. */
    (void)objectIndex;
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)partTableBase;
    (void)childPass;
    (void)objectMatrix;
    (void)submeshIndex;
}

void CzanModel_UpdateStandardPartTexcoords(int *model, int *partIndexSource, const void *objectMatrix, unsigned short *submesh) {
    /* 0x801595BC is the standard-path leaf for part byte +0x13 == 1.

       Confirmed behavior:
       - recovers model, object vertex position table at caller state +0x24, and
         normal/vector table at caller state +0x2C from the saved-register helper
       - selects model +0xC8 as the base matrix unless model +0x12C is nonzero, in
         which case it uses that matrix pointer
       - updates only a slice of vertices based on model +0x1A0/+0x1A4
       - reads index pairs from submesh +0x0C, using each pair to select position and
         normal/vector records
       - transforms/project vectors through matrix helpers
       - writes two floats per vertex to *(submesh +0x04)
       - flushes *(submesh +0x04), vertexCount * 8 bytes

       This is a CPU texcoord/projection update, not the final draw submit. */
    if (model == 0 || partIndexSource == 0 || objectMatrix == 0 || submesh == 0) {
        return;
    }
}

void CzanModel_UpdateStandardSpecialPartTexcoords(
    int *model,
    int *partIndexSource,
    int partEntry,
    const void *objectMatrix,
    unsigned short *submesh) {
    /* 0x80159730 is the standard-path leaf for part byte +0x13 values other than 1.

       Confirmed behavior:
       - starts from the same model/objectMatrix/submesh inputs as
         CzanModel_UpdateStandardPartTexcoords
       - if part byte +0x13 is not 4 and model +0x128 is nonzero, overrides the
         projection vector from *(model +0x128)
       - chooses matrix source from objectMatrix or model +0x16C depending on
         model +0x160
       - kind 3 uses a direct transformed normal/vector projection
       - other kinds normalize a transformed vector and compute the output texcoords
         from the x/y components
       - writes two floats per vertex to *(submesh +0x04), then flushes the buffer

       Part kind 4 zeroes the projection vector before the non-kind-3 calculation. */
    if (model == 0 || partIndexSource == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)objectMatrix;
}

void CzanModel_UpdateType2PartTexcoords(
    int *model,
    int *partIndexSource,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015BD68 is the type-2 path leaf for part byte +0x13 == 1.

       Confirmed behavior:
       - uses model +0xC8 or model +0x12C as the base matrix source
       - uses drawContext[1] + submeshIndex * 0x10 +4 to locate a per-submesh vector
         table
       - updates only the current model +0x1A4 slice out of model +0x1A0 slices
       - reads position indices from submesh +0x0C
       - computes a normalized vector from base matrix translation to the source
         position, projects it through the per-submesh vector table, then writes two
         floats per vertex to *(submesh +0x04)
       - flushes *(submesh +0x04), vertexCount * 8 bytes

       The paired type-2 special leaf is CzanModel_UpdateType2SpecialPartTexcoords. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)submeshIndex;
}

void CzanModel_UpdateType2SpecialPartTexcoords(
    int *model,
    int *partIndexSource,
    int partEntry,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015BEFC is the type-2 special texcoord/projection leaf for part byte
       +0x13 values other than 1.

       Confirmed behavior:
       - uses model +0xC8/model +0x12C as the base matrix/vector source, optionally
         overridden by model +0x128 when part kind is not 4
       - uses drawContext[1] + submeshIndex * 0x10 +4 for a per-submesh vector table
       - updates only the current model +0x1A4 slice out of model +0x1A0 slices
       - has separate formulas for part kinds 2, 3, 4, and the external-matrix path
         when model +0x15C is present and model +0x160 is clear
       - writes two floats per vertex to *(submesh +0x04), then flushes the buffer

       This completes the recovered CPU texcoord generation leaves. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)submeshIndex;
}

void CzanModel_ApplyMaterialCullMode(int *partMaterial, int forceCullBack) {
    /* 0x80177150 selects the GX cull mode via FUN_801D40E0.

       Original rules:
       - if partMaterial exists and byte +0x11 is nonzero, call FUN_801D40E0(0)
       - else if forceCullBack is nonzero, call FUN_801D40E0(1)
       - otherwise call FUN_801D40E0(2)

       This is likely material-local culling/visibility state, with the caller able
       to force the middle mode. */
    (void)partMaterial;
    (void)forceCullBack;
}

void CzanModel_ApplyMaterialBlendMode(int *partMaterial, int forceAlphaCompare, int forceBlendEnabled) {
    /* 0x80177184 applies material blend/alpha state.

       Original behavior:
       - starts with FUN_801D7200(1, 3, 1)
       - materialMode = partMaterial byte +0x12 & 0x7F
       - highBit = partMaterial byte +0x12 >> 7
       - forceBlendEnabled overrides highBit to 1
       - materialMode 3 -> RenderSetBlendMode(1, 4, 5, 5)
       - materialMode 2 -> RenderSetBlendMode(1, 0, 5, 5)
       - materialMode 1 -> RenderSetBlendMode(1, 4, 1, 5), then FUN_801D7200(1, 3, 0)
       - default        -> RenderSetBlendMode(1, 4, 5, 5)
       - if materialMode == 0 or forceAlphaCompare:
           RenderSetAlphaUpdate(1)
           RenderSetAlphaCompare(7, 0, 1, 7, 0)
         else:
           RenderSetAlphaUpdate(0)
           highBit clear -> RenderSetAlphaCompare(4, 0xA0, 0, 3, 0xFF)
           highBit set   -> RenderSetAlphaCompare(4, 0,    0, 3, 0xFF)

       This is the blend/alpha compare half of the material state used by
       CzanModel_SetupPartRenderState and the material tree walkers. */
    (void)partMaterial;
    (void)forceAlphaCompare;
    (void)forceBlendEnabled;
}

void CzanModel_SetupMaterialVertexAttributes(
    int *submesh,
    int *partMaterial,
    int forceNormalAttr,
    int useGeneratedColorAttr,
    int useTexcoordAttr) {
    /* 0x801772E8 configures the GX vertex attribute layout for one material/submesh.

       Confirmed source offsets:
       - short submesh +0x06 controls position attr source mode.
       - submesh +0x24 is position-array base when short +0x06 != 1.
       - submesh +0x34 is color array base.
       - submesh +0x30 is generated texcoord array base.
       - submesh +0x2C is normal/vector array base.
       - partMaterial byte +0x10 enables normal/vector attr 10.

       Confirmed attr setup:
       - always clears attrs through RenderClearVertexDescriptors / FUN_801D2B80.
       - attr 9 is position:
         short +0x06 == 1 -> direct/indexed mode 1
         otherwise        -> mode 3 with array base submesh +0x24, stride 0x0C
       - attr 11 is color when submesh +0x34 exists:
         useGeneratedColorAttr == 0 -> mode 3, array base submesh +0x34, stride 4
         otherwise                  -> direct/indexed mode 1
       - attr 13 is texcoord when submesh +0x30 exists and useTexcoordAttr != 0:
         mode 3, array base submesh +0x30, stride 8
       - attr 10 is normal/vector when material byte +0x10 or forceNormalAttr is set
         and submesh +0x2C exists:
         short +0x06 == 1 -> direct/indexed mode 1
         otherwise        -> mode 3, array base submesh +0x2C, stride 0x0C

       The PC renderer can map this directly into a vertex declaration once the ZMB
       runtime arrays are decoded. */
    (void)submesh;
    (void)partMaterial;
    (void)forceNormalAttr;
    (void)useGeneratedColorAttr;
    (void)useTexcoordAttr;
}

int CzanModel_SetupPartRenderState(int *model, void *outState, int *drawArgs) {
    /* 0x80157FE4 is the shared material/render-state setup helper used by the
       primitive submit leaves.

       Confirmed behavior:
       - drawArgs[0] points at the part/material entry.
       - drawArgs[2] selects the texture/runtime slot; negative values use the
         default texture object at model +0x288.
       - drawArgs[3] enables the display/config path through FUN_80143858 or
         FUN_801438A4.
       - drawArgs[4] enables an extra render-state bit through FUN_801D7200.
       - drawArgs[5]/drawArgs[6] feed the material/color mask setup.
       - binds the resolved texture with GXLoadTexObj_wrapper.
       - calls CzanModel_ApplyMaterialBlendMode / FUN_80177184 and configures TEV,
         blend, alpha, texgen, and raster state through the render/GX wrapper layer.
       - returns success when a usable texture/render state was resolved; returns
         zero when the part cannot bind its required texture.

       The original also writes two small state values through the saved-register
       helper context. The host renderer should eventually turn this into a real
       RenderState object instead of a boolean stub. */
    if (model == 0 || drawArgs == 0 || drawArgs[0] == 0) {
        return 0;
    }

    (void)outState;
    return 1;
}

void CzanModel_SubmitPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    const void *objectMatrix,
    unsigned short *submesh) {
    /* 0x801586C0 is a real GX/display-list submit leaf.

       Confirmed behavior:
       - recovers model plus draw-state fields from a saved-register helper
       - calls FUN_80157FE4 to validate/setup the part and render state
       - configures vertex attribute arrays with FUN_801D2B80 plus
         RenderSetVertexAttrDescriptor, RenderSetVertexAttrFormat, and
         RenderSetVertexArray
       - binds/generated texcoords at attr 0x0D from *(submesh +0x04)
       - optionally regenerates texcoords through the same slice logic when
         model +0x164 == 0 and model +0x168 != 0
       - emits GX primitive 0x98 batches through RenderBeginPrimitiveBatch and writes
         indices, colors, and texcoords to the write-gather pipe at 0xCC008000

       This is one of the PC renderer's key targets: it tells us which arrays feed
       the final draw call. */
    if (model == 0 || partIndexSource == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)objectMatrix;
}

void CzanModel_SubmitSpecialPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    const void *objectMatrix,
    unsigned short *submesh) {
    /* 0x80158DB8 is the non-kind-1 companion to CzanModel_SubmitPartPrimitive.

       Confirmed behavior:
       - validates part/render state through FUN_80157FE4
       - configures position, normal/vector, color, and generated texcoord attrs
         through the same render/GX wrapper layer as 0x801586C0
       - for part kind 3, directly projects the normal/vector table into generated
         texcoords; for other kinds it normalizes/project vectors
       - binds attr 0x0D to *(submesh +0x04), optionally regenerating that buffer
         when model +0x164 == 0 and model +0x168 != 0
       - emits primitive 0x98 batches to the GX write-gather pipe

       This is the special/non-kind-1 primitive submit path reached by the material
       part-tree renderers. */
    if (model == 0 || partIndexSource == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)objectMatrix;
}

void CzanModel_SubmitType2PartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015ABC8 is the type-2 object-path companion to
       CzanModel_SubmitPartPrimitive for part kind 1.

       Confirmed behavior:
       - calls CzanModel_SetupPartRenderState / FUN_80157FE4 first.
       - pulls a per-submesh vector table from drawContext[1] + submeshIndex * 0x10
         + 4.
       - configures GX vertex attrs with FUN_801D2B80 plus
         RenderSetVertexAttrDescriptor, RenderSetVertexAttrFormat, and
         RenderSetVertexArray.
       - when model +0x164 is clear and model +0x168 is set, regenerates generated
         texcoords for the active model +0x1A4 slice.
       - emits primitive 0x98 batches through RenderBeginPrimitiveBatch and writes
         the position index, per-submesh vector index/data, color index, and
         generated texcoord index to the write-gather pipe.

       This is one of the exact select_cmn model submitters we need for the PC
       renderer once ZMB runtime arrays are populated. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)submeshIndex;
}

void CzanModel_SubmitType2SpecialPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015B2D8 is the type-2 object-path companion to
       CzanModel_SubmitSpecialPartPrimitive for non-kind-1 parts.

       Confirmed behavior:
       - calls CzanModel_SetupPartRenderState / FUN_80157FE4 first.
       - uses drawContext[1] + submeshIndex * 0x10 + 4 for the per-submesh vector
         table.
       - configures the same GX attr/primitive stream as the other submit leaves.
       - contains the type-2 special texcoord formulas for part kinds 2, 3, 4, and
         the external-matrix path.
       - emits primitive 0x98 batches to 0xCC008000.

       The important distinction from 0x80158DB8 is that this path uses the type-2
       drawContext/submesh vector table instead of the standard object matrix path. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)submeshIndex;
}

void CzanModel_DrawBaseMaterialPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childIndex,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex) {
    /* 0x80153F44 is the base/fallback material part draw traversal.

       Confirmed behavior:
       - accepts a null partTableBase and handles default model material state
       - applies part/material state with CzanModel_ApplyMaterialCullMode,
         CzanModel_SetupMaterialVertexAttributes, and
         CzanModel_ApplyMaterialBlendMode
       - configures texture/color/alpha/TEV state through the render/GX wrapper layer
       - when a texture is available, binds texture-set entries and emits primitive
         batches to 0xCC008000
       - recurses into child parts, using CzanModel_DrawMaterialPartTree when a child
         has byte +0x17 set and CzanModel_DrawBaseMaterialPartTree otherwise
       - when model +0x168 is enabled, delegates leaves to
         CzanModel_SubmitPartPrimitive or CzanModel_SubmitSpecialPartPrimitive

       Together with CzanModel_DrawMaterialPartTree, this is the material-state and
       GX-submit layer above the primitive leaves. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)partTableBase;
    (void)childIndex;
    (void)objectMatrix;
    (void)submeshIndex;
    (void)objectIndex;
}

void CzanModel_DrawMaterialPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childIndex,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex) {
    /* 0x801528B4 is a full material/part draw traversal with render-state setup.

       Confirmed behavior:
       - selects the current part from partTableBase + model[0x2E] * *partIndexSource,
         or from the child table when childIndex is nonzero
       - calls CzanModel_ApplyMaterialCullMode and
         CzanModel_SetupMaterialVertexAttributes to apply part/material state
       - handles special model modes at model +0x280 by dispatching to FUN_80159A04
         or FUN_8015A2B8
       - binds texture set slots through BindTextureFromTextureSet/GXLoadTexObj_wrapper
       - configures TEV/color/alpha state through the render/GX wrapper layer
       - emits primitive batches to the GX write-gather pipe
       - when model +0x168 is enabled, delegates leaf submissions to
         CzanModel_SubmitPartPrimitive for kind 1 or
         CzanModel_SubmitSpecialPartPrimitive for other kinds
       - recurses through child parts and handles extra synthetic part model +0x28C

       This is closer to the final renderer than the earlier texcoord leaves. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)partTableBase;
    (void)childIndex;
    (void)objectMatrix;
    (void)submeshIndex;
    (void)objectIndex;
}

void CzanModel_FinalizeTransformUpdate(double deltaOrScale, int *model) {
    /* 0x801576FC runs once after all object transforms are updated. It advances
       material/part frame timers from the primary block's +0x1C table, clears the
       dirty byte at model +0x50, optionally rescales delta by model +0x94 and
       +0x19C, calls FUN_80156B70, optionally calls FUN_801579A8 when model +0x1B4
       is set, then marks model +0x150 = 1. */
    (void)deltaOrScale;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_InitVisiblePartUvRuntime(int *model) {
    /* 0x801568FC / CzanModel_InitVisiblePartUvRuntime initializes/resets UV
       animation runtime fields for visible parts.

       Ghidra may show void(void) because the function begins with a saved-register
       helper. The recovered object is the CzanModel pointer.

       Confirmed behavior:
       - reads the material/part table at *(model +0x04) +0x1C
       - uses model +0x44 as the visible-part runtime array and model +0x48 as count
       - each runtime part entry is 0x50 bytes
       - clears runtime timers/offsets at +0x0C, +0x1C, +0x40, +0x44
       - clears bytes +0x4A/+0x4B
       - when the source part at runtime +0x30 has a UV/key table at +0x3C, reads
         count at source +0x3A and initializes current key indices +0x4A/+0x4B
       - computes starting UV offsets into runtime +0x0C/+0x1C and duration/range
         into +0x34/+0x38/+0x3C */
    if (model == 0) {
        return;
    }
}

void CzanModel_UpdatePartUvAnimation(double deltaOrScale, int *model) {
    /* 0x80156B70 advances UV/scroll animation for renderable parts. It walks the
       primary block's part table at +0x1C, updates runtime part records at model
       +0x44 with scrolling values, then applies keyframed UV animation curves from
       part +0x3C when available. */
    (void)deltaOrScale;
    if (model == 0 || model[1] == 0) {
        return;
    }
}
