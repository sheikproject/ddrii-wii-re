#include "select/character_special_parts.h"

void LoadSpecialPartValues(
    int outValues[CHARACTER_SPECIAL_PART_VALUE_COUNT],
    const int *table,
    int entryIndex
) {
    const int *entry = (const int *)((const char *)table + entryIndex * CHARACTER_SPECIAL_PART_TABLE_STRIDE);

    for (int i = 0; i < CHARACTER_SPECIAL_PART_VALUE_COUNT; i++) {
        outValues[i] = entry[i];
    }
}

void SetIndexedSpecialPartValue(void *base, int outerIndex, int partIndex, int value) {
    char *bytes = (char *)base;
    *(int *)(bytes + outerIndex * CHARACTER_SPECIAL_PART_OBJECT_STRIDE + partIndex * 4 + 8) = value;
}

void ApplyCharacterSpecialPart(void *objectBase, int outerIndex, int partIndex, int value) {
    char *bytes = (char *)objectBase;
    SetIndexedSpecialPartValue(bytes + 0x34, outerIndex, partIndex, value);
}

void SetMiiSpecialPartTableEntry(
    int *tableBase,
    int entryIndex,
    int miiType,
    int selectedMiiListIndex,
    int bodyCostumeId,
    int unknownValue3
) {
    int *entry = (int *)((char *)tableBase + entryIndex * CHARACTER_SPECIAL_PART_TABLE_STRIDE);

    entry[0] = miiType;
    entry[1] = selectedMiiListIndex;
    entry[2] = bodyCostumeId;
    entry[3] = unknownValue3;
}

void SetMiiSpecialPartTableEntryTail(
    int *tableBase,
    int entryIndex,
    int recolorSlot1,
    int recolorSlot2,
    int recolorSlot3,
    int recolorSlot4
) {
    int *entry = (int *)((char *)tableBase + entryIndex * CHARACTER_SPECIAL_PART_TABLE_STRIDE);

    entry[4] = recolorSlot1;
    entry[5] = recolorSlot2;
    entry[6] = recolorSlot3;
    entry[7] = recolorSlot4;
}
