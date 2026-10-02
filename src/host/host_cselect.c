#include "host/host_cselect.h"

#include "model/czan_model.h"
#include "render/render_engine.h"
#include "resource/czan_link.h"
#include "select/csel_mode.h"

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_CSELECT_DIAGNOSTICS 0

enum HostInput {
    HOST_INPUT_NONE,
    HOST_INPUT_LEFT,
    HOST_INPUT_RIGHT,
    HOST_INPUT_CONFIRM,
    HOST_INPUT_BACK,
};

#define HOST_SELECT_TEXTURE_SLOTS 16

static TextureSlotKnownFields gHostSelectTextureSlots[HOST_SELECT_TEXTURE_SLOTS];
static TextureManagerKnownFields gHostSelectTextureManager = {
    gHostSelectTextureSlots,
    0,
    HOST_SELECT_TEXTURE_SLOTS
};

static void Host_CopyName(char *outName, unsigned int outNameSize, const unsigned char *data, unsigned int maxSize);

static int Host_ReadInput(void) {
    int key;

    if (!_kbhit()) {
        return HOST_INPUT_NONE;
    }

    key = _getch();
    if (key == 0 || key == 0xE0) {
        key = _getch();
        switch (key) {
            case 75:
                return HOST_INPUT_LEFT;
            case 77:
                return HOST_INPUT_RIGHT;
            default:
                return HOST_INPUT_NONE;
        }
    }

    switch (key) {
        case 'a':
        case 'A':
            return HOST_INPUT_LEFT;
        case 'd':
        case 'D':
            return HOST_INPUT_RIGHT;
        case '\r':
            return HOST_INPUT_CONFIRM;
        case 27:
        case 'b':
        case 'B':
            return HOST_INPUT_BACK;
        default:
            return HOST_INPUT_NONE;
    }
}

static unsigned int Host_ReadBe32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) |
           (unsigned int)p[3];
}

static unsigned int Host_ReadBe16(const unsigned char *p) {
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static float Host_ReadBeFloat(const unsigned char *p) {
    union {
        unsigned int u;
        float f;
    } value;

    value.u = Host_ReadBe32(p);
    return value.f;
}

static void Host_AppendSelectProbeLine(const char *message) {
    FILE *probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "a");
    if (probeFile == 0) {
        return;
    }
    fputs(message, probeFile);
    fputc('\n', probeFile);
    fclose(probeFile);
}

static float Host_AbsFloat(float value) {
    return value < 0.0f ? -value : value;
}

static int Host_GetSelectCameraMatrix(const HostSelectCommonModelBinding *cameraBinding, float *outMatrix) {
    const unsigned char *zmb;
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    float worldMatrices[HOST_SELECT_MAX_OBJECT_NAMES][12];

    if (cameraBinding == 0 || cameraBinding->zmbData == 0 || outMatrix == 0) {
        return 0;
    }

    zmb = (const unsigned char *)cameraBinding->zmbData;
    if (cameraBinding->zmbSize < 0x30 || memcmp(zmb, "ZMB ", 4) != 0) {
        return 0;
    }

    objectTableOffset = Host_ReadBe32(zmb + 0x20);
    if (objectTableOffset > cameraBinding->zmbSize || cameraBinding->zmbSize - objectTableOffset < 0x0c) {
        static int loggedInvalidCameraTable = 0;
        if (!loggedInvalidCameraTable) {
            FILE *probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "a");
            if (probeFile != 0) {
                fprintf(probeFile,
                        "select_cmn: camera zmb invalid table objectTable=0x%X size=0x%X raw20=%02X%02X%02X%02X\n",
                        objectTableOffset,
                        cameraBinding->zmbSize,
                        zmb[0x20],
                        zmb[0x21],
                        zmb[0x22],
                        zmb[0x23]);
                fclose(probeFile);
            }
            loggedInvalidCameraTable = 1;
        }
        return 0;
    }

    objectCount = Host_ReadBe32(zmb + objectTableOffset);
    objectEntryOffset = Host_ReadBe32(zmb + objectTableOffset + 8);
    {
        static int loggedCameraTable = 0;
        if (!loggedCameraTable) {
            FILE *probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "a");
            if (probeFile != 0) {
                fprintf(probeFile,
                        "select_cmn: camera zmb table objectTable=0x%X objectCount=%u objectEntry=0x%X size=0x%X\n",
                        objectTableOffset,
                        objectCount,
                        objectEntryOffset,
                        cameraBinding->zmbSize);
                fclose(probeFile);
            }
            loggedCameraTable = 1;
        }
    }
    if (objectEntryOffset > cameraBinding->zmbSize || objectCount == 0) {
        return 0;
    }
    if (objectCount > HOST_SELECT_MAX_OBJECT_NAMES) {
        objectCount = HOST_SELECT_MAX_OBJECT_NAMES;
    }

    CzanModel_BuildZmbObjectWorldMatrices(
        zmb,
        cameraBinding->zmbSize,
        objectEntryOffset,
        objectCount,
        worldMatrices,
        HOST_SELECT_MAX_OBJECT_NAMES);
    memcpy(outMatrix, worldMatrices[0], sizeof(worldMatrices[0]));
    return 1;
}

static int Host_ApplySelectCameraZabTranslation(const HostSelectCommonModelBinding *cameraBinding, float *cameraMatrix) {
    const unsigned char *zab;
    unsigned int channelCount;
    unsigned int channelIndex;

    if (cameraBinding == 0 || cameraBinding->zabData == 0 || cameraMatrix == 0) {
        return 0;
    }

    zab = (const unsigned char *)cameraBinding->zabData;
    if (cameraBinding->zabSize < 0x30 || memcmp(zab, "ZAB ", 4) != 0) {
        return 0;
    }

    channelCount = Host_ReadBe32(zab + 0x0c);
    for (channelIndex = 0; channelIndex < channelCount; channelIndex++) {
        unsigned int channelOffset = 0x30 + channelIndex * 0x40;
        unsigned int keyGroupCount;
        unsigned int keyGroupOffset;
        unsigned int groupIndex;
        char channelName[32];

        if (channelOffset + 0x40 > cameraBinding->zabSize) {
            break;
        }

        Host_CopyName(channelName, sizeof(channelName), zab + channelOffset, cameraBinding->zabSize - channelOffset);
        if (strcmp(channelName, "BG_Camera01") != 0) {
            continue;
        }

        keyGroupCount = Host_ReadBe32(zab + channelOffset + 0x34);
        keyGroupOffset = Host_ReadBe32(zab + channelOffset + 0x3c);
        for (groupIndex = 0; groupIndex < keyGroupCount; groupIndex++) {
            unsigned int groupOffset = keyGroupOffset + groupIndex * 0x10;
            unsigned int keyType;
            unsigned int keyCount;
            unsigned int keyOffset;

            if (groupOffset + 0x10 > cameraBinding->zabSize) {
                break;
            }

            keyType = Host_ReadBe32(zab + groupOffset);
            keyCount = Host_ReadBe32(zab + groupOffset + 8);
            keyOffset = Host_ReadBe32(zab + groupOffset + 0x0c);
            if (keyType == 0 && keyCount > 0 && keyOffset + 0x10 <= cameraBinding->zabSize) {
                cameraMatrix[3] = Host_ReadBeFloat(zab + keyOffset + 4);
                cameraMatrix[7] = Host_ReadBeFloat(zab + keyOffset + 8);
                cameraMatrix[11] = Host_ReadBeFloat(zab + keyOffset + 0x0c);
                return 1;
            }
        }
    }

    return 0;
}

static int Host_ProjectCameraPoint(const float *cameraMatrix, float x, float y, float z, int *outX, int *outY) {
    float dx = x - cameraMatrix[3];
    float dy = y - cameraMatrix[7];
    float dz = z - cameraMatrix[11];
    float cameraX = cameraMatrix[0] * dx + cameraMatrix[4] * dy + cameraMatrix[8] * dz;
    float cameraY = cameraMatrix[1] * dx + cameraMatrix[5] * dy + cameraMatrix[9] * dz;
    float cameraZ = cameraMatrix[2] * dx + cameraMatrix[6] * dy + cameraMatrix[10] * dz;
    float depth = -cameraZ;
    float focal = 520.0f;

    if (depth < 1.0f) {
        depth = cameraZ;
    }
    if (depth < 1.0f) {
        return 0;
    }

    *outX = 320 + (int)((cameraX * focal) / depth);
    *outY = 240 - (int)((cameraY * focal) / depth);
    return 1;
}

static void Host_ProjectDebugPoint(float x, float y, float z, float centerX, float centerY, float centerZ, float scale, int *outX, int *outY) {
    float localX = x - centerX;
    float localY = y - centerY;
    float projectedX = -localX;
    float projectedY = -localY;

    (void)z;
    (void)centerZ;
    *outX = 320 + (int)(projectedX * scale);
    *outY = 240 + (int)(projectedY * scale);
}

static int Host_IsLikelyNameChar(unsigned char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '_' ||
           c == '-' ||
           c == '@';
}

static void Host_CopyName(char *outName, unsigned int outNameSize, const unsigned char *data, unsigned int maxSize) {
    unsigned int i;

    if (outNameSize == 0) {
        return;
    }

    for (i = 0; i + 1 < outNameSize && i < maxSize; i++) {
        if (data[i] == 0 || !Host_IsLikelyNameChar(data[i])) {
            break;
        }
        outName[i] = (char)data[i];
    }
    outName[i] = '\0';
}

static int Host_FindName(char names[][32], unsigned int nameCount, const char *name) {
    unsigned int i;

    if (name == 0 || name[0] == '\0') {
        return -1;
    }

    for (i = 0; i < nameCount; i++) {
        if (strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static unsigned int Host_CollectZmbObjectNames(const CzanLinkBlock *block, char names[][32], unsigned int maxNames) {
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    unsigned int i;
    unsigned int collected;

    if (block == 0 || block->data == 0 || block->size < 0x30 || memcmp(block->data, "ZMB ", 4) != 0) {
        return 0;
    }

    objectTableOffset = Host_ReadBe32(block->data + 0x20);
    if (objectTableOffset > block->size || block->size - objectTableOffset < 0x0c) {
        return 0;
    }

    objectCount = Host_ReadBe32(block->data + objectTableOffset);
    objectEntryOffset = Host_ReadBe32(block->data + objectTableOffset + 8);
    if (objectEntryOffset > block->size) {
        return 0;
    }

    collected = 0;
    for (i = 0; i < objectCount && collected < maxNames; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        if (entryOffset >= block->size) {
            break;
        }

        Host_CopyName(names[collected], 32, block->data + entryOffset, block->size - entryOffset);
        if (names[collected][0] != '\0') {
            collected++;
        }
    }

    return collected;
}

static void Host_LogZmbObjectNames(const CzanLinkBlock *block, unsigned int blockIndex, char names[][32], unsigned int *outNameCount) {
    unsigned int count;
    unsigned int i;

    count = Host_CollectZmbObjectNames(block, names, HOST_SELECT_MAX_OBJECT_NAMES);
    if (outNameCount != 0) {
        *outNameCount = count;
    }

    printf("select_cmn: block %u ZMB object names=%u\n", blockIndex, count);
    for (i = 0; i < count && i < 16; i++) {
        printf("select_cmn:   zmb object[%u]=%s\n", i, names[i]);
    }
    if (count > 16) {
        printf("select_cmn:   ... %u more objects\n", count - 16);
    }
}

static void Host_LogSelectCommonObjectGeometryProbe(
    HostSelectCommonModelBinding *binding,
    const CzanLinkBlock *block,
    unsigned int blockIndex,
    const char *objectName) {
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    unsigned int i;
    FILE *probeFile;
    FILE *out;

    if (block == 0 || block->data == 0 || objectName == 0 ||
        block->size < 0x30 || memcmp(block->data, "ZMB ", 4) != 0) {
        return;
    }

    objectTableOffset = Host_ReadBe32(block->data + 0x20);
    if (objectTableOffset > block->size || block->size - objectTableOffset < 0x0c) {
        return;
    }

    objectCount = Host_ReadBe32(block->data + objectTableOffset);
    objectEntryOffset = Host_ReadBe32(block->data + objectTableOffset + 8);
    if (objectEntryOffset > block->size) {
        return;
    }

    probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "w");
    out = probeFile != 0 ? probeFile : stdout;

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        char name[32];

        if (entryOffset + 0xa0 > block->size) {
            break;
        }

        Host_CopyName(name, sizeof(name), block->data + entryOffset, block->size - entryOffset);
        if (strcmp(name, objectName) != 0) {
            continue;
        }

        if (binding != 0) {
            binding->panelObjectEntryOffset = entryOffset;
            binding->hasPanelObjectEntry = 1;
        }

        {
            const unsigned char *entry = block->data + entryOffset;
            unsigned int objectType = Host_ReadBe32(entry + 0x2c);
            unsigned int submeshCount = Host_ReadBe16(entry + 0x9a);
            unsigned int submeshOffset = Host_ReadBe32(entry + 0x9c);
            unsigned int matrixFloatIndex;

            fprintf(out,
                    "select_cmn: block %u object[%u]=%s entry=0x%X type=0x%X flags28=%u flags2A=%u submeshes=%u submeshTable=0x%X\n",
                    blockIndex,
                    i,
                    name,
                    entryOffset,
                    objectType,
                    (unsigned int)entry[0x28],
                    (unsigned int)entry[0x2a],
                    submeshCount,
                    submeshOffset);
            fprintf(out, "select_cmn:   object +30 floats=");
            for (matrixFloatIndex = 0; matrixFloatIndex < 12; matrixFloatIndex++) {
                fprintf(out,
                        "%s%.6f",
                        matrixFloatIndex == 0 ? "" : ",",
                        Host_ReadBeFloat(entry + 0x30 + matrixFloatIndex * 4));
            }
            fprintf(out,
                    " translate60=%.6f,%.6f,%.6f parent=%d animFlag=%u\n",
                    Host_ReadBeFloat(entry + 0x60),
                    Host_ReadBeFloat(entry + 0x64),
                    Host_ReadBeFloat(entry + 0x68),
                    (int)Host_ReadBe32(entry + 0x94),
                    (unsigned int)entry[0x98]);
            if (objectEntryOffset + 0xa0 <= block->size) {
                const unsigned char *parentEntry = block->data + objectEntryOffset;
                fprintf(out, "select_cmn:   parent object[0] +30 floats=");
                for (matrixFloatIndex = 0; matrixFloatIndex < 12; matrixFloatIndex++) {
                    fprintf(out,
                            "%s%.6f",
                            matrixFloatIndex == 0 ? "" : ",",
                            Host_ReadBeFloat(parentEntry + 0x30 + matrixFloatIndex * 4));
                }
                fprintf(out,
                        " translate60=%.6f,%.6f,%.6f parent=%d animFlag=%u\n",
                        Host_ReadBeFloat(parentEntry + 0x60),
                        Host_ReadBeFloat(parentEntry + 0x64),
                        Host_ReadBeFloat(parentEntry + 0x68),
                        (int)Host_ReadBe32(parentEntry + 0x94),
                        (unsigned int)parentEntry[0x98]);
            }

            if (submeshOffset + 0x40 <= block->size && submeshCount > 0) {
                const unsigned char *submesh = block->data + submeshOffset;
                unsigned int primitiveStride = Host_ReadBe16(submesh + 4) != 0 ? 0x20 : 0x14;
                unsigned int primitiveCount = Host_ReadBe16(submesh + 0x0a);
                unsigned int texcoordArray = Host_ReadBe32(submesh + 4);
                unsigned int texcoordIndices = Host_ReadBe32(submesh + 8);
                unsigned int primitiveTable = Host_ReadBe32(submesh + 0x20);
                unsigned int positionArray = Host_ReadBe32(submesh + 0x24);
                unsigned int normalArray = Host_ReadBe32(submesh + 0x2c);
                unsigned int colorArray = Host_ReadBe32(submesh + 0x34);

                fprintf(out,
                        "select_cmn:   submesh[0] u16[0..A]=%u,%u,%u,%u,%u,%u primitiveStride=0x%X primitiveCount=%u\n",
                        Host_ReadBe16(submesh),
                        Host_ReadBe16(submesh + 2),
                        Host_ReadBe16(submesh + 4),
                        Host_ReadBe16(submesh + 6),
                        Host_ReadBe16(submesh + 8),
                        Host_ReadBe16(submesh + 0x0a),
                        primitiveStride,
                        primitiveCount);
                fprintf(out,
                        "select_cmn:   submesh[0] texcoordArray=0x%X texcoordIndices=0x%X primitiveTable=0x%X positionArray=0x%X normalArray=0x%X colorArray=0x%X\n",
                        texcoordArray,
                        texcoordIndices,
                        primitiveTable,
                        positionArray,
                        normalArray,
                        colorArray);

                if (primitiveTable + 0x10 <= block->size && primitiveCount > 0) {
                    const unsigned char *primitive = block->data + primitiveTable;
                    fprintf(out,
                            "select_cmn:   primitive[0] modeOrFlags=%u vertexCount=%u posIdx=0x%X normalIdx=0x%X colorIdx=0x%X\n",
                            Host_ReadBe16(primitive),
                            Host_ReadBe16(primitive + 2),
                            Host_ReadBe32(primitive + 4),
                            Host_ReadBe32(primitive + 8),
                            Host_ReadBe32(primitive + 0x0c));
                }

                if (binding != 0) {
                    unsigned int primitiveIndex;
                    binding->panelVertexCount = 0;
                    binding->panelPrimitiveCount = 0;
                    for (primitiveIndex = 0; primitiveIndex < primitiveCount; primitiveIndex++) {
                        unsigned int primitiveOffset = primitiveTable + primitiveIndex * primitiveStride;
                        unsigned int vertexCount;
                        unsigned int positionIndexStream;
                        unsigned int vertexIndex;
                        unsigned int cacheStart;

                        if (primitiveOffset + 0x10 > block->size) {
                            break;
                        }

                        vertexCount = Host_ReadBe16(block->data + primitiveOffset + 2);
                        positionIndexStream = Host_ReadBe32(block->data + primitiveOffset + 4);
                        if (positionIndexStream >= block->size) {
                            continue;
                        }

                        cacheStart = binding->panelVertexCount;
                        for (vertexIndex = 0; vertexIndex < vertexCount && binding->panelVertexCount < 64; vertexIndex++) {
                            unsigned int indexOffset = positionIndexStream + vertexIndex * 4;
                            unsigned int positionIndex;
                            unsigned int positionOffset;

                            if (indexOffset + 4 > block->size) {
                                break;
                            }

                            positionIndex = Host_ReadBe32(block->data + indexOffset) & 0xffffu;
                            positionOffset = positionArray + positionIndex * 0x0c;
                            if (positionOffset + 8 > block->size) {
                                continue;
                            }

                            binding->panelVertices[binding->panelVertexCount][0] =
                                Host_ReadBeFloat(block->data + positionOffset);
                            binding->panelVertices[binding->panelVertexCount][1] =
                                Host_ReadBeFloat(block->data + positionOffset + 4);
                            binding->panelVertices[binding->panelVertexCount][2] =
                                Host_ReadBeFloat(block->data + positionOffset + 8);
                            binding->panelVertexCount++;
                        }
                        if (binding->panelPrimitiveCount < 8 && binding->panelVertexCount > cacheStart) {
                            unsigned int cacheIndex = binding->panelPrimitiveCount;
                            binding->panelPrimitiveStart[cacheIndex] = cacheStart;
                            binding->panelPrimitiveVertexCount[cacheIndex] = binding->panelVertexCount - cacheStart;
                            binding->panelPrimitiveCount++;
                        }
                    }
                    fprintf(out,
                            "select_cmn:   cached panel vertices=%u primitives=%u\n",
                            binding->panelVertexCount,
                            binding->panelPrimitiveCount);
                }
            }
            fflush(out);
            if (probeFile != 0) {
                printf("select_cmn: wrote geometry probe to outputs\\select_cmn_geometry_probe.txt\n");
                fclose(probeFile);
            }
        }
        return;
    }

    fprintf(out, "select_cmn: object %s was not found in block %u\n", objectName, blockIndex);
    fflush(out);
    if (probeFile != 0) {
        printf("select_cmn: wrote geometry probe to outputs\\select_cmn_geometry_probe.txt\n");
        fclose(probeFile);
    }
}

static void Host_CacheSelectCommonVisibleDebugMesh(HostSelectCommonModelBinding *binding, const CzanLinkBlock *block) {
    CzanModelSubmittedPrimitiveBuffer primitiveBuffer;
    unsigned int textureCounts[16];
    unsigned int i;

    if (binding == 0 || block == 0 || block->data == 0) {
        return;
    }
    memset(textureCounts, 0, sizeof(textureCounts));

    primitiveBuffer.vertices = binding->debugVertices;
    primitiveBuffer.texcoords = binding->debugTexcoords;
    primitiveBuffer.colors = binding->debugColors;
    primitiveBuffer.vertexObjectIndex = 0;
    primitiveBuffer.vertexCapacity = HOST_SELECT_DEBUG_VERTEX_CAP;
    primitiveBuffer.vertexCount = 0;
    primitiveBuffer.primitiveStart = binding->debugPrimitiveStart;
    primitiveBuffer.primitiveVertexCount = binding->debugPrimitiveVertexCount;
    primitiveBuffer.primitiveTextureIndex = binding->debugPrimitiveTextureIndex;
    primitiveBuffer.primitiveMaterialMode = binding->debugPrimitiveMaterialMode;
    primitiveBuffer.primitiveCapacity = HOST_SELECT_DEBUG_PRIMITIVE_CAP;
    primitiveBuffer.primitiveCount = 0;
    primitiveBuffer.submittedObjectCount = 0;

    CzanModel_SubmitVisibleZmbPrimitiveStreams(block->data, block->size, &primitiveBuffer);
    binding->debugVertexCount = primitiveBuffer.vertexCount;
    binding->debugPrimitiveCount = primitiveBuffer.primitiveCount;

    {
        FILE *probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "a");
        if (probeFile != 0) {
            fprintf(probeFile,
                    "select_cmn: CzanModel submitted visible streams objects=%u primitives=%u vertices=%u boundsX=%.3f..%.3f boundsY=%.3f..%.3f boundsZ=%.3f..%.3f\n",
                    primitiveBuffer.submittedObjectCount,
                    primitiveBuffer.primitiveCount,
                    primitiveBuffer.vertexCount,
                    primitiveBuffer.boundsMin[0],
                    primitiveBuffer.boundsMax[0],
                    primitiveBuffer.boundsMin[1],
                    primitiveBuffer.boundsMax[1],
                    primitiveBuffer.boundsMin[2],
                    primitiveBuffer.boundsMax[2]);
            for (i = 0; i < binding->debugPrimitiveCount; i++) {
                unsigned int textureIndex = binding->debugPrimitiveTextureIndex[i];
                if (textureIndex < 16) {
                    textureCounts[textureIndex]++;
                }
            }
            fprintf(probeFile,
                    "select_cmn: submitted texture use 0=%u 1=%u 2=%u 3=%u 4=%u 5=%u 6=%u 7=%u 8=%u 9=%u\n",
                    textureCounts[0],
                    textureCounts[1],
                    textureCounts[2],
                    textureCounts[3],
                    textureCounts[4],
                    textureCounts[5],
                    textureCounts[6],
                    textureCounts[7],
                    textureCounts[8],
                    textureCounts[9]);
            fclose(probeFile);
        }
    }
}

static void Host_UpdateSelectCommonAnimatedDebugMesh(HostSelectCommonModelBinding *binding, float animationTick) {
    CzanModelSubmittedPrimitiveBuffer primitiveBuffer;

    if (binding == 0 || binding->zmbData == 0) {
        return;
    }

    primitiveBuffer.vertices = binding->debugVertices;
    primitiveBuffer.texcoords = binding->debugTexcoords;
    primitiveBuffer.colors = binding->debugColors;
    primitiveBuffer.vertexObjectIndex = 0;
    primitiveBuffer.vertexCapacity = HOST_SELECT_DEBUG_VERTEX_CAP;
    primitiveBuffer.vertexCount = 0;
    primitiveBuffer.primitiveStart = binding->debugPrimitiveStart;
    primitiveBuffer.primitiveVertexCount = binding->debugPrimitiveVertexCount;
    primitiveBuffer.primitiveTextureIndex = binding->debugPrimitiveTextureIndex;
    primitiveBuffer.primitiveMaterialMode = binding->debugPrimitiveMaterialMode;
    primitiveBuffer.primitiveCapacity = HOST_SELECT_DEBUG_PRIMITIVE_CAP;
    primitiveBuffer.primitiveCount = 0;
    primitiveBuffer.submittedObjectCount = 0;

    CzanModel_SubmitAnimatedZmbPrimitiveStreams(
        binding->zmbData,
        binding->zmbSize,
        binding->zabData,
        binding->zabSize,
        animationTick,
        &primitiveBuffer);
    binding->debugVertexCount = primitiveBuffer.vertexCount;
    binding->debugPrimitiveCount = primitiveBuffer.primitiveCount;
}

static unsigned int Host_LogZabChannelMatches(
    const CzanLinkBlock *block,
    unsigned int blockIndex,
    unsigned int objectBlockIndex,
    char objectNames[][32],
    unsigned int objectNameCount,
    int verbose) {
    unsigned int channelCount;
    unsigned int durationTicks;
    unsigned int i;
    unsigned int matchCount;

    if (block == 0 || block->data == 0 || block->size < 0x30 || memcmp(block->data, "ZAB ", 4) != 0) {
        return 0;
    }

    channelCount = Host_ReadBe32(block->data + 0x0c);
    durationTicks = Host_ReadBe32(block->data + 0x10);
    matchCount = 0;

    printf("select_cmn: block %u ZAB channels=%u durationTicks=%u\n", blockIndex, channelCount, durationTicks);
    for (i = 0; i < channelCount; i++) {
        unsigned int channelOffset = 0x30 + i * 0x40;
        unsigned int keyGroupCount;
        unsigned int keyGroupOffset;
        unsigned int groupIndex;
        unsigned int translationGroups;
        unsigned int rotationGroups;
        unsigned int scaleGroups;
        char channelName[32];
        int matchIndex;

        if (channelOffset >= block->size) {
            break;
        }

        Host_CopyName(channelName, sizeof(channelName), block->data + channelOffset, block->size - channelOffset);
        keyGroupCount = channelOffset + 0x38 <= block->size ? Host_ReadBe32(block->data + channelOffset + 0x34) : 0;
        keyGroupOffset = channelOffset + 0x40 <= block->size ? Host_ReadBe32(block->data + channelOffset + 0x3c) : 0;
        matchIndex = Host_FindName(objectNames, objectNameCount, channelName);
        if (matchIndex >= 0) {
            matchCount++;
        }

        if (verbose || i < 24 || matchIndex >= 0) {
            printf("select_cmn:   zab channel[%u]=%s keys=%u keyTable=0x%X match=%d\n",
                   i,
                   channelName[0] != '\0' ? channelName : "<unnamed>",
                   keyGroupCount,
                   keyGroupOffset,
                   matchIndex);
        }

        translationGroups = 0;
        rotationGroups = 0;
        scaleGroups = 0;
        for (groupIndex = 0; groupIndex < keyGroupCount; groupIndex++) {
            unsigned int groupOffset = keyGroupOffset + groupIndex * 0x10;
            unsigned int keyType;
            unsigned int keyCount;
            unsigned int keyOffset;
            unsigned int firstTick;

            if (groupOffset + 0x10 > block->size) {
                break;
            }

            keyType = Host_ReadBe32(block->data + groupOffset);
            keyCount = Host_ReadBe32(block->data + groupOffset + 8);
            keyOffset = Host_ReadBe32(block->data + groupOffset + 0x0c);
            firstTick = keyOffset + 4 <= block->size ? Host_ReadBe32(block->data + keyOffset) : 0;

            if (keyType == 0) {
                translationGroups++;
            }
            else if (keyType == 1) {
                rotationGroups++;
            }
            else if (keyType == 2) {
                scaleGroups++;
            }

            if (verbose && groupIndex < 6) {
                printf("select_cmn:     keyGroup[%u] type=%u count=%u keyOffset=0x%X firstTick=%u\n",
                       groupIndex,
                       keyType,
                       keyCount,
                       keyOffset,
                       firstTick);
            }
        }
        if (verbose || i < 24 || matchIndex >= 0) {
            printf("select_cmn:     keyGroups summary T/R/S=%u/%u/%u\n",
                   translationGroups,
                   rotationGroups,
                   scaleGroups);
        }
    }

    printf("select_cmn: block %u ZAB matched %u/%u channels against block %u ZMB objects\n",
           blockIndex,
           matchCount,
           channelCount,
           objectBlockIndex);
    return matchCount;
}

static unsigned int Host_GetZabChannelCount(const CzanLinkBlock *block) {
    if (block == 0 || block->data == 0 || block->size < 0x10 || memcmp(block->data, "ZAB ", 4) != 0) {
        return 0;
    }

    return Host_ReadBe32(block->data + 0x0c);
}

static unsigned int Host_GetZabDurationTicks(const CzanLinkBlock *block) {
    if (block == 0 || block->data == 0 || block->size < 0x14 || memcmp(block->data, "ZAB ", 4) != 0) {
        return 0;
    }

    return Host_ReadBe32(block->data + 0x10);
}

static void Host_LoadSelectCommonModelBinding(
    HostSelectCommonModelBinding *binding,
    const CzanLinkBlock *zmbBlock,
    const CzanLinkBlock *textureBlock,
    const CzanLinkBlock *zabBlock,
    unsigned int zmbBlockIndex,
    unsigned int zabBlockIndex,
    int verboseZab) {
    unsigned char *oldOwnedZmb;
    unsigned char *oldOwnedZab;

    if (binding == 0) {
        return;
    }

    oldOwnedZmb = binding->ownedZmbData;
    oldOwnedZab = binding->ownedZabData;
    memset(binding, 0, sizeof(*binding));
    if (oldOwnedZmb != 0) {
        free(oldOwnedZmb);
    }
    if (oldOwnedZab != 0) {
        free(oldOwnedZab);
    }

    if (zmbBlock != 0) {
        binding->ownedZmbData = (unsigned char *)malloc(zmbBlock->size);
        if (binding->ownedZmbData != 0) {
            memcpy(binding->ownedZmbData, zmbBlock->data, zmbBlock->size);
            binding->ownedZmbSize = zmbBlock->size;
            binding->zmbData = binding->ownedZmbData;
            binding->zmbSize = binding->ownedZmbSize;
        }
        else {
            binding->zmbData = zmbBlock->data;
            binding->zmbSize = zmbBlock->size;
        }
        if (zmbBlockIndex == 5) {
            FILE *probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "a");
            if (probeFile != 0) {
                const unsigned char *storedZmb = (const unsigned char *)binding->zmbData;
                fprintf(probeFile,
                        "select_cmn: camera load block5 stored=%p raw=%p size=0x%X magic=%02X%02X%02X%02X\n",
                        storedZmb,
                        zmbBlock->data,
                        binding->zmbSize,
                        binding->zmbSize >= 4 ? storedZmb[0] : 0,
                        binding->zmbSize >= 4 ? storedZmb[1] : 0,
                        binding->zmbSize >= 4 ? storedZmb[2] : 0,
                        binding->zmbSize >= 4 ? storedZmb[3] : 0);
                fclose(probeFile);
            }
        }
        binding->objectNameCount =
            Host_CollectZmbObjectNames(zmbBlock, binding->objectNames, HOST_SELECT_MAX_OBJECT_NAMES);
        Host_LogZmbObjectNames(zmbBlock, zmbBlockIndex, binding->objectNames, &binding->objectNameCount);
        if (zmbBlockIndex == 0) {
            Host_LogSelectCommonObjectGeometryProbe(binding, zmbBlock, zmbBlockIndex, "a_BG01a_panel");
        }
        if (zmbBlockIndex != 5) {
            Host_CacheSelectCommonVisibleDebugMesh(binding, zmbBlock);
        }
    }

    if (textureBlock != 0) {
        binding->textureData = textureBlock->data;
        binding->textureSize = textureBlock->size;
        if (textureBlock->data != 0 && textureBlock->size >= 0x20 &&
            Host_ReadBe32(textureBlock->data) == 0x0020AF30u) {
            binding->textureSlot = CreateTextureFromTplResource(
                &gHostSelectTextureManager,
                (void *)textureBlock->data,
                (int)textureBlock->size,
                0xFFFFFFFFu);
            binding->hasTextureSlot = 1;
            printf("select_cmn: loaded texture block into slot %u size=0x%X\n",
                   binding->textureSlot,
                   binding->textureSize);
        }
    }

    if (zabBlock != 0) {
        CzanLinkBlock copiedZabBlock;
        binding->ownedZabData = (unsigned char *)malloc(zabBlock->size);
        if (binding->ownedZabData != 0) {
            memcpy(binding->ownedZabData, zabBlock->data, zabBlock->size);
            binding->ownedZabSize = zabBlock->size;
            binding->zabData = binding->ownedZabData;
            binding->zabSize = binding->ownedZabSize;
        }
        else {
            binding->zabData = zabBlock->data;
            binding->zabSize = zabBlock->size;
        }
        copiedZabBlock.data = (const unsigned char *)binding->zabData;
        copiedZabBlock.size = binding->zabSize;
        binding->zabChannelCount = Host_GetZabChannelCount(&copiedZabBlock);
        binding->zabDurationTicks = Host_GetZabDurationTicks(&copiedZabBlock);
        binding->matchedChannelCount = Host_LogZabChannelMatches(
            &copiedZabBlock,
            zabBlockIndex,
            zmbBlockIndex,
            binding->objectNames,
            binding->objectNameCount,
            verboseZab);
    }
}

static void Host_LogSelectCommonBlock(const CzanLinkBlock *block, unsigned int index) {
    char magic[5];

    if (block->data == 0 || block->size < 4) {
        printf("select_cmn: block %u empty/invalid\n", index);
        return;
    }

    memcpy(magic, block->data, 4);
    magic[4] = '\0';
    printf("select_cmn: block %u size=0x%X magic=%.4s\n", index, block->size, magic);

    if (memcmp(block->data, "ZMB ", 4) == 0 && block->size >= 0x28) {
        printf("select_cmn:   ZMB +18 textureFrames=0x%X +1C materials=0x%X +20 objects=0x%X +24 relocated=%u\n",
               Host_ReadBe32(block->data + 0x18),
               Host_ReadBe32(block->data + 0x1C),
               Host_ReadBe32(block->data + 0x20),
               Host_ReadBe32(block->data + 0x24));
    }
}

void HostCSelect_SetCommonSelectResource(
    HostCSelectModule *module,
    void *selectCommonLinkData,
    unsigned int selectCommonLinkSize) {
    CzanLinkBlock topBlock;
    unsigned int topCount;

    module->selectCommonLinkData = selectCommonLinkData;
    module->selectCommonLinkSize = selectCommonLinkSize;
    memset(module->selectCommon, 0, sizeof(module->selectCommon));

    if (!CzanLinkResource_IsValid(selectCommonLinkData, selectCommonLinkSize)) {
        puts("select_cmn: not a valid WII resource");
        return;
    }

    topCount = CzanLinkResource_GetBlockCount(selectCommonLinkData, selectCommonLinkSize);
    if (HOST_CSELECT_DIAGNOSTICS) {
        printf("select_cmn: WII blockCount=%u\n", topCount);
    }

    if (!CzanLinkResource_GetBlock(selectCommonLinkData, selectCommonLinkSize, 0, &topBlock)) {
        puts("select_cmn: missing top block 0");
        return;
    }

    CSelectCommon_LoadResource(module->selectCommon, (void *)topBlock.data);
    {
        int *ownerModel = CzanModelOwner_GetHostModel((int *)((unsigned char *)module->selectCommon + 0x128));
        if (ownerModel != 0) {
            if (HOST_CSELECT_DIAGNOSTICS) {
                printf("select_cmn: loaded model owner through game loader model=%p primary=%p continuation0=%p\n",
                       ownerModel,
                       CzanModel_GetHostPrimaryBlock(ownerModel),
                       CzanModel_GetHostContinuationBlock(ownerModel, 0));
            }
        }
    }
}

static void Host_DrawSelectCommonBindingDiagnostic(const HostSelectCommonModelBinding *binding) {
    unsigned int i;
    unsigned int visibleCount;

    if (binding == 0 || binding->zmbData == 0 || binding->matchedChannelCount == 0) {
        return;
    }

    visibleCount = binding->matchedChannelCount;
    if (visibleCount > 160) {
        visibleCount = 160;
    }

    for (i = 0; i < visibleCount; i++) {
        int column = (int)(i % 32);
        int row = (int)(i / 32);
        int x = 28 + column * 18;
        int y = 32 + row * 18;
        int size = 10 + (int)((i + binding->objectNameCount) % 5);
        unsigned int color =
            0x3050FFFFu ^
            ((i * 0x00130700u) & 0x00FFFF00u) ^
            ((binding->matchedChannelCount * 0x00010100u) & 0x00FFFF00u);

        DrawFilledRect(x, y, 0, size, size, &color, 0);
    }
}

static int Host_FindZmbObjectEntry(const HostSelectCommonModelBinding *binding, const char *objectName, unsigned int *outEntryOffset) {
    const unsigned char *zmb;
    unsigned int objectTableOffset;
    unsigned int objectCount;
    unsigned int objectEntryOffset;
    unsigned int i;

    if (binding == 0 || binding->zmbData == 0 || objectName == 0) {
        return 0;
    }

    zmb = (const unsigned char *)binding->zmbData;
    if (binding->zmbSize < 0x30 || memcmp(zmb, "ZMB ", 4) != 0) {
        return 0;
    }

    objectTableOffset = Host_ReadBe32(zmb + 0x20);
    if (objectTableOffset > binding->zmbSize || binding->zmbSize - objectTableOffset < 0x0c) {
        return 0;
    }

    objectCount = Host_ReadBe32(zmb + objectTableOffset);
    objectEntryOffset = Host_ReadBe32(zmb + objectTableOffset + 8);
    if (objectEntryOffset > binding->zmbSize) {
        return 0;
    }

    for (i = 0; i < objectCount; i++) {
        unsigned int entryOffset = objectEntryOffset + i * 0xa0;
        char name[32];

        if (entryOffset + 0xa0 > binding->zmbSize) {
            break;
        }

        Host_CopyName(name, sizeof(name), zmb + entryOffset, binding->zmbSize - entryOffset);
        if (strcmp(name, objectName) == 0) {
            if (outEntryOffset != 0) {
                *outEntryOffset = entryOffset;
            }
            return 1;
        }
    }

    return 0;
}

static unsigned int Host_AverageVertexColors(unsigned int c0, unsigned int c1, unsigned int c2) {
    unsigned int r = ((c0 >> 24) & 0xffu) + ((c1 >> 24) & 0xffu) + ((c2 >> 24) & 0xffu);
    unsigned int g = ((c0 >> 16) & 0xffu) + ((c1 >> 16) & 0xffu) + ((c2 >> 16) & 0xffu);
    unsigned int b = ((c0 >> 8) & 0xffu) + ((c1 >> 8) & 0xffu) + ((c2 >> 8) & 0xffu);
    unsigned int a = (c0 & 0xffu) + (c1 & 0xffu) + (c2 & 0xffu);

    r /= 3;
    g /= 3;
    b /= 3;
    a /= 3;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

static int Host_DrawSelectCommonPanelPrimitive(
    const HostSelectCommonModelBinding *binding,
    const HostSelectCommonModelBinding *cameraBinding) {
    const unsigned char *zmb;
    unsigned int entryOffset;
    const unsigned char *entry;
    unsigned int submeshOffset;
    const unsigned char *submesh;
    unsigned int primitiveCount;
    unsigned int primitiveStride;
    unsigned int primitiveTable;
    unsigned int positionArray;
    unsigned int primitiveIndex;
    float minX;
    float maxX;
    float minY;
    float maxY;
    int haveBounds;

    if (binding != 0 && binding->debugVertexCount > 0) {
        unsigned int i;
        unsigned int primitiveIndex;
        float minAxis[3];
        float maxAxis[3];
        float centerAxis[3];
        float minProjectedX;
        float maxProjectedX;
        float minProjectedY;
        float maxProjectedY;
        float spanX;
        float spanY;
        float scaleX;
        float scaleY;
        float scale;
        float cameraMatrix[12];
        int hasCamera;
        static int cameraProbeLogged = 0;

        minAxis[0] = maxAxis[0] = binding->debugVertices[0][0];
        minAxis[1] = maxAxis[1] = binding->debugVertices[0][1];
        minAxis[2] = maxAxis[2] = binding->debugVertices[0][2];
        for (i = 1; i < binding->debugVertexCount; i++) {
            unsigned int axis;
            for (axis = 0; axis < 3; axis++) {
                float value = binding->debugVertices[i][axis];
                if (value < minAxis[axis]) minAxis[axis] = value;
                if (value > maxAxis[axis]) maxAxis[axis] = value;
            }
        }

        centerAxis[0] = (minAxis[0] + maxAxis[0]) * 0.5f;
        centerAxis[1] = (minAxis[1] + maxAxis[1]) * 0.5f;
        centerAxis[2] = (minAxis[2] + maxAxis[2]) * 0.5f;

        minProjectedX = maxProjectedX = binding->debugVertices[0][0] - centerAxis[0];
        minProjectedY = maxProjectedY = -(binding->debugVertices[0][1] - centerAxis[1]);
        for (i = 1; i < binding->debugVertexCount; i++) {
            float projectedX = binding->debugVertices[i][0] - centerAxis[0];
            float projectedY = -(binding->debugVertices[i][1] - centerAxis[1]);
            if (projectedX < minProjectedX) minProjectedX = projectedX;
            if (projectedX > maxProjectedX) maxProjectedX = projectedX;
            if (projectedY < minProjectedY) minProjectedY = projectedY;
            if (projectedY > maxProjectedY) maxProjectedY = projectedY;
        }

        spanX = maxProjectedX - minProjectedX;
        spanY = maxProjectedY - minProjectedY;
        scaleX = spanX != 0.0f ? 420.0f / spanX : 1.0f;
        scaleY = spanY != 0.0f ? 300.0f / spanY : 1.0f;
        scale = scaleX < scaleY ? scaleX : scaleY;
        if (scale < 0.01f || scale > 1000.0f) {
            scale = 1.0f;
        }
        hasCamera = Host_GetSelectCameraMatrix(cameraBinding, cameraMatrix);
        if (!cameraProbeLogged) {
            FILE *probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "a");
            if (probeFile != 0) {
                fprintf(probeFile,
                        "select_cmn: camera binding at draw zmb=%p zmbSize=0x%X zmbMagic=%02X%02X%02X%02X zab=%p zabSize=0x%X hasCamera=%d\n",
                        cameraBinding != 0 ? cameraBinding->zmbData : 0,
                        cameraBinding != 0 ? cameraBinding->zmbSize : 0,
                        cameraBinding != 0 && cameraBinding->zmbData != 0 && cameraBinding->zmbSize >= 4 ? ((const unsigned char *)cameraBinding->zmbData)[0] : 0,
                        cameraBinding != 0 && cameraBinding->zmbData != 0 && cameraBinding->zmbSize >= 4 ? ((const unsigned char *)cameraBinding->zmbData)[1] : 0,
                        cameraBinding != 0 && cameraBinding->zmbData != 0 && cameraBinding->zmbSize >= 4 ? ((const unsigned char *)cameraBinding->zmbData)[2] : 0,
                        cameraBinding != 0 && cameraBinding->zmbData != 0 && cameraBinding->zmbSize >= 4 ? ((const unsigned char *)cameraBinding->zmbData)[3] : 0,
                        cameraBinding != 0 ? cameraBinding->zabData : 0,
                        cameraBinding != 0 ? cameraBinding->zabSize : 0,
                        hasCamera);
                if (cameraBinding != 0 && cameraBinding->zmbData != 0 && cameraBinding->zmbSize >= 0x10) {
                    const unsigned char *cameraRaw = (const unsigned char *)cameraBinding->zmbData;
                    fprintf(probeFile,
                            "select_cmn: camera raw first16=%08X,%08X,%08X,%08X\n",
                            Host_ReadBe32(cameraRaw),
                            Host_ReadBe32(cameraRaw + 4),
                            Host_ReadBe32(cameraRaw + 8),
                            Host_ReadBe32(cameraRaw + 0x0c));
                }
                fclose(probeFile);
            }
        }
        if (hasCamera) {
            int appliedZab = Host_ApplySelectCameraZabTranslation(cameraBinding, cameraMatrix);
            if (!cameraProbeLogged) {
                FILE *probeFile = fopen("outputs\\select_cmn_geometry_probe.txt", "a");
                if (probeFile != 0) {
                    fprintf(probeFile,
                            "select_cmn: camera projection active zabTranslation=%d matrixT=%.3f,%.3f,%.3f basisX=%.3f,%.3f,%.3f basisY=%.3f,%.3f,%.3f basisZ=%.3f,%.3f,%.3f\n",
                            appliedZab,
                            cameraMatrix[3],
                            cameraMatrix[7],
                            cameraMatrix[11],
                            cameraMatrix[0],
                            cameraMatrix[4],
                            cameraMatrix[8],
                            cameraMatrix[1],
                            cameraMatrix[5],
                            cameraMatrix[9],
                            cameraMatrix[2],
                            cameraMatrix[6],
                            cameraMatrix[10]);
                    fclose(probeFile);
                }
            }
        }
        cameraProbeLogged = 1;

        for (primitiveIndex = 0; primitiveIndex < binding->debugPrimitiveCount; primitiveIndex++) {
            unsigned int start = binding->debugPrimitiveStart[primitiveIndex];
            unsigned int count = binding->debugPrimitiveVertexCount[primitiveIndex];
            unsigned int localIndex;
            unsigned int lineColor = 0x303030FFu;
            unsigned int textureIndex = binding->debugPrimitiveTextureIndex[primitiveIndex];
            int projectedPoints[2048][2];
            float projectedTexcoords[2048][2];
            unsigned int projectedColors[2048];
            int projectedOk = 1;

            if (start >= binding->debugVertexCount) {
                continue;
            }
            if (start + count > binding->debugVertexCount) {
                count = binding->debugVertexCount - start;
            }
            if (count > 2048) {
                count = 2048;
            }

            for (localIndex = 0; localIndex < count; localIndex++) {
                unsigned int vertex = start + localIndex;
                if (hasCamera) {
                    if (!Host_ProjectCameraPoint(
                            cameraMatrix,
                            binding->debugVertices[vertex][0],
                            binding->debugVertices[vertex][1],
                            binding->debugVertices[vertex][2],
                            &projectedPoints[localIndex][0],
                            &projectedPoints[localIndex][1])) {
                        projectedOk = 0;
                        break;
                    }
                }
                else {
                    Host_ProjectDebugPoint(
                        binding->debugVertices[vertex][0],
                        binding->debugVertices[vertex][1],
                        binding->debugVertices[vertex][2],
                        centerAxis[0],
                        centerAxis[1],
                        centerAxis[2],
                        scale,
                        &projectedPoints[localIndex][0],
                        &projectedPoints[localIndex][1]);
                }

                projectedTexcoords[localIndex][0] = binding->debugTexcoords[vertex][0];
                projectedTexcoords[localIndex][1] = binding->debugTexcoords[vertex][1];
                projectedColors[localIndex] = binding->debugColors[vertex];
            }

            if (!projectedOk || count < 3) {
                continue;
            }

            DrawTexturedTriangleStrip2D(
                projectedPoints,
                projectedTexcoords,
                projectedColors,
                count,
                binding->hasTextureSlot ? (void *)(long)binding->textureSlot : 0,
                (int)textureIndex);

            if (binding->hasTextureSlot) {
                continue;
            }

            for (localIndex = 1; localIndex < count; localIndex++) {
                DrawLine2D(
                    projectedPoints[localIndex - 1][0],
                    projectedPoints[localIndex - 1][1],
                    projectedPoints[localIndex][0],
                    projectedPoints[localIndex][1],
                    &lineColor);
            }
        }

        return 1;
    }

    if (binding != 0 && binding->hasPanelObjectEntry) {
        entryOffset = binding->panelObjectEntryOffset;
    }
    else if (!Host_FindZmbObjectEntry(binding, "a_BG01a_panel", &entryOffset)) {
        Host_AppendSelectProbeLine("select_cmn draw: a_BG01a_panel object not found");
        return 0;
    }

    zmb = (const unsigned char *)binding->zmbData;
    if (entryOffset + 0xa0 > binding->zmbSize) {
        Host_AppendSelectProbeLine("select_cmn draw: cached panel object entry is out of range");
        return 0;
    }
    entry = zmb + entryOffset;
    submeshOffset = Host_ReadBe32(entry + 0x9c);
    if (Host_ReadBe16(entry + 0x9a) == 0 || submeshOffset + 0x40 > binding->zmbSize) {
        Host_AppendSelectProbeLine("select_cmn draw: invalid panel submesh table");
        return 0;
    }

    submesh = zmb + submeshOffset;
    primitiveStride = Host_ReadBe16(submesh + 4) != 0 ? 0x20 : 0x14;
    primitiveCount = Host_ReadBe16(submesh + 0x0a);
    primitiveTable = Host_ReadBe32(submesh + 0x20);
    positionArray = Host_ReadBe32(submesh + 0x24);
    if (primitiveCount == 0 || primitiveTable >= binding->zmbSize || positionArray >= binding->zmbSize) {
        Host_AppendSelectProbeLine("select_cmn draw: invalid primitive/position table");
        return 0;
    }

    minX = 0.0f;
    maxX = 0.0f;
    minY = 0.0f;
    maxY = 0.0f;
    haveBounds = 0;

    for (primitiveIndex = 0; primitiveIndex < primitiveCount; primitiveIndex++) {
        unsigned int primitiveOffset = primitiveTable + primitiveIndex * primitiveStride;
        unsigned int vertexCount;
        unsigned int positionIndexStream;
        unsigned int vertexIndex;

        if (primitiveOffset + 0x10 > binding->zmbSize) {
            break;
        }

        vertexCount = Host_ReadBe16(zmb + primitiveOffset + 2);
        positionIndexStream = Host_ReadBe32(zmb + primitiveOffset + 4);
        if (positionIndexStream >= binding->zmbSize) {
            continue;
        }

        for (vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
            unsigned int indexOffset = positionIndexStream + vertexIndex * 4;
            unsigned int positionIndex;
            unsigned int positionOffset;
            float x;
            float y;

            if (indexOffset + 4 > binding->zmbSize) {
                break;
            }

            positionIndex = Host_ReadBe32(zmb + indexOffset) & 0xffffu;
            positionOffset = positionArray + positionIndex * 0x0c;
            if (positionOffset + 8 > binding->zmbSize) {
                continue;
            }

            x = Host_ReadBeFloat(zmb + positionOffset);
            y = Host_ReadBeFloat(zmb + positionOffset + 4);
            if (!haveBounds) {
                minX = maxX = x;
                minY = maxY = y;
                haveBounds = 1;
            }
            else {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (!haveBounds) {
        Host_AppendSelectProbeLine("select_cmn draw: no valid panel vertices");
        return 0;
    }

    {
        float spanX = maxX - minX;
        float spanY = maxY - minY;
        float scaleX = spanX != 0.0f ? 420.0f / spanX : 1.0f;
        float scaleY = spanY != 0.0f ? 300.0f / spanY : 1.0f;
        float scale = scaleX < scaleY ? scaleX : scaleY;
        unsigned int mainColor = 0x40F6D2FFu;
        unsigned int firstColor = 0xFF4050FFu;

        if (scale < 0.01f || scale > 1000.0f) {
            scale = 1.0f;
        }

        for (primitiveIndex = 0; primitiveIndex < primitiveCount; primitiveIndex++) {
            unsigned int primitiveOffset = primitiveTable + primitiveIndex * primitiveStride;
            unsigned int vertexCount;
            unsigned int positionIndexStream;
            unsigned int vertexIndex;

            if (primitiveOffset + 0x10 > binding->zmbSize) {
                break;
            }

            vertexCount = Host_ReadBe16(zmb + primitiveOffset + 2);
            positionIndexStream = Host_ReadBe32(zmb + primitiveOffset + 4);
            if (positionIndexStream >= binding->zmbSize) {
                continue;
            }

            for (vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
                unsigned int indexOffset = positionIndexStream + vertexIndex * 4;
                unsigned int positionIndex;
                unsigned int positionOffset;
                const unsigned int *color;
                int x;
                int y;
                float vx;
                float vy;

                if (indexOffset + 4 > binding->zmbSize) {
                    break;
                }

                positionIndex = Host_ReadBe32(zmb + indexOffset) & 0xffffu;
                positionOffset = positionArray + positionIndex * 0x0c;
                if (positionOffset + 8 > binding->zmbSize) {
                    continue;
                }

                vx = Host_ReadBeFloat(zmb + positionOffset);
                vy = Host_ReadBeFloat(zmb + positionOffset + 4);
                x = 320 + (int)(((vx - (minX + maxX) * 0.5f) * scale));
                y = 240 - (int)(((vy - (minY + maxY) * 0.5f) * scale));
                color = (primitiveIndex == 0 && vertexIndex == 0) ? &firstColor : &mainColor;
                DrawFilledRect(x - 2, y - 2, 0, 5, 5, color, 0);
            }
        }
    }

    return 1;
}

static void Host_DrawCSelModeGl(HostCSelectModule *module) {
    unsigned int backgroundColor = 0xFFFFFFFF;
    (void)module;

    RenderBeginFrame();
    ApplyRenderConfig(0, &backgroundColor);
    CSelMode_DrawHostUi();
    RenderEndFrame();
}

int CSelect_TickHost(void *cSelect) {
    HostCSelectModule *module = (HostCSelectModule *)cSelect;
    int input;

    if (module->redrawNeeded) {
        module->redrawNeeded = 0;
    }

    Host_DrawCSelModeGl(module);

    input = Host_ReadInput();
    if (input == HOST_INPUT_LEFT) {
        module->selectedModeIndex = CSelMode_MoveSelection(module->selectedModeIndex, -1);
        module->redrawNeeded = 1;
    }
    else if (input == HOST_INPUT_RIGHT) {
        module->selectedModeIndex = CSelMode_MoveSelection(module->selectedModeIndex, 1);
        module->redrawNeeded = 1;
    }
    else if (input == HOST_INPUT_CONFIRM) {
        return 1;
    }
    else if (input == HOST_INPUT_BACK) {
        return 1;
    }

    module->frame++;
    return 0;
}
