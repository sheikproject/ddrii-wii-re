#include "host/host_cselect.h"

#include "render/render_engine.h"
#include "resource/czan_link.h"
#include "select/csel_mode.h"

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_SELECT_MAX_OBJECT_NAMES 512

enum HostInput {
    HOST_INPUT_NONE,
    HOST_INPUT_LEFT,
    HOST_INPUT_RIGHT,
    HOST_INPUT_CONFIRM,
    HOST_INPUT_BACK,
};

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

static void Host_LogZabChannelMatches(
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
        return;
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
    CzanLinkBlock nestedBlock;
    CzanLinkBlock zmbBlock0;
    CzanLinkBlock zabBlock2;
    unsigned int topCount;
    unsigned int nestedCount;
    unsigned int i;
    CzanLinkBlock zmbBlock5;
    char block5ObjectNames[HOST_SELECT_MAX_OBJECT_NAMES][32];
    unsigned int block5ObjectNameCount;

    module->selectCommonLinkData = selectCommonLinkData;
    module->selectCommonLinkSize = selectCommonLinkSize;

    if (!CzanLinkResource_IsValid(selectCommonLinkData, selectCommonLinkSize)) {
        puts("select_cmn: not a valid WII resource");
        return;
    }

    topCount = CzanLinkResource_GetBlockCount(selectCommonLinkData, selectCommonLinkSize);
    printf("select_cmn: WII blockCount=%u\n", topCount);

    if (!CzanLinkResource_GetBlock(selectCommonLinkData, selectCommonLinkSize, 0, &topBlock)) {
        puts("select_cmn: missing top block 0");
        return;
    }

    nestedCount = CzanLinkResource_GetBlockCount(topBlock.data, topBlock.size);
    printf("select_cmn: top block 0 common model package blocks=%u\n", nestedCount);
    for (i = 0; i < nestedCount; i++) {
        if (CzanLinkResource_GetBlock(topBlock.data, topBlock.size, i, &nestedBlock)) {
            Host_LogSelectCommonBlock(&nestedBlock, i);
        }
    }

    block5ObjectNameCount = 0;
    memset(block5ObjectNames, 0, sizeof(block5ObjectNames));
    if (CzanLinkResource_GetBlock(topBlock.data, topBlock.size, 5, &zmbBlock5)) {
        Host_LogZmbObjectNames(&zmbBlock5, 5, block5ObjectNames, &block5ObjectNameCount);
    }

    for (i = 6; i <= 15 && i < nestedCount; i++) {
        if (CzanLinkResource_GetBlock(topBlock.data, topBlock.size, i, &nestedBlock)) {
            Host_LogZabChannelMatches(&nestedBlock, i, 5, block5ObjectNames, block5ObjectNameCount, 1);
        }
    }

    memset(block5ObjectNames, 0, sizeof(block5ObjectNames));
    if (CzanLinkResource_GetBlock(topBlock.data, topBlock.size, 0, &zmbBlock0) &&
        CzanLinkResource_GetBlock(topBlock.data, topBlock.size, 2, &zabBlock2)) {
        unsigned int block0ObjectNameCount;

        Host_LogZmbObjectNames(&zmbBlock0, 0, block5ObjectNames, &block0ObjectNameCount);
        Host_LogZabChannelMatches(&zabBlock2, 2, 0, block5ObjectNames, block0ObjectNameCount, 0);
    }
}

static const char *Host_GetModeName(int selectedModeIndex) {
    static const char *modeNames[] = {
        "Mode ID 1",
        "Mode ID 2",
        "Mode ID 3",
        "Mode ID 6",
        "Back / Title",
    };

    if (selectedModeIndex < 0 || selectedModeIndex >= 5) {
        return "Unknown";
    }
    return modeNames[selectedModeIndex];
}

static void Host_PrintCSelModeChoiceDetails(int selectedModeIndex) {
    const CSelModeChoice *choice = CSelMode_GetChoice(selectedModeIndex);

    if (choice == 0) {
        printf("CSelMode: selected index %d -> invalid\n", selectedModeIndex);
        return;
    }

    printf("CSelMode: selected index %d -> nextState=%d mode=%d sub=%d extra=%d\n",
           selectedModeIndex,
           choice->nextSelectState,
           choice->gameModeId,
           choice->gameModeSubId,
           choice->gameModeExtraId);
}

static void Host_DrawCSelModeMenu(int selectedModeIndex) {
    int i;
    const CSelModeChoice *choice;

    puts("");
    puts("---");
    puts("DDRII Host Skeleton");
    puts("===================");
    puts("");
    puts("CSelMode - Mode Select");
    puts("");

    for (i = 0; i < 5; i++) {
        printf("%s %s\n", i == selectedModeIndex ? ">" : " ", Host_GetModeName(i));
    }

    choice = CSelMode_GetChoice(selectedModeIndex);
    puts("");
    if (choice != 0) {
        printf("nextState=%d  mode=%d  sub=%d  extra=%d\n",
               choice->nextSelectState,
               choice->gameModeId,
               choice->gameModeSubId,
               choice->gameModeExtraId);
    }
    else {
        puts("Invalid selection");
    }

    puts("");
    puts("A/Left: previous   D/Right: next   Enter: confirm   Esc/B: exit");
}

static void Host_DrawCSelModeGl(const HostCSelectModule *module) {
    unsigned int backgroundColor = 0x74DDFDFF;
    (void)module;

    RenderBeginFrame();
    ApplyRenderConfig(0, &backgroundColor);
    RenderEndFrame();
}

int CSelect_TickHost(void *cSelect) {
    HostCSelectModule *module = (HostCSelectModule *)cSelect;
    int input;

    if (module->frame == 0) {
        puts("CSelect: enter CSelMode");
    }

    if (module->redrawNeeded) {
        Host_DrawCSelModeMenu(module->selectedModeIndex);
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
        puts("CSelMode: confirm");
        Host_PrintCSelModeChoiceDetails(module->selectedModeIndex);
        return 1;
    }
    else if (input == HOST_INPUT_BACK) {
        puts("CSelMode: back/exit");
        return 1;
    }

    module->frame++;
    return 0;
}
