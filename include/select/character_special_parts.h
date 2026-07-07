#ifndef DDRII_SELECT_CHARACTER_SPECIAL_PARTS_H
#define DDRII_SELECT_CHARACTER_SPECIAL_PARTS_H

#define CHARACTER_SPECIAL_PART_VALUE_COUNT 8
#define CHARACTER_SPECIAL_PART_TABLE_STRIDE 0x20
#define CHARACTER_SPECIAL_PART_OBJECT_STRIDE 0xC4

void LoadSpecialPartValues(
    int outValues[CHARACTER_SPECIAL_PART_VALUE_COUNT],
    const int *table,
    int entryIndex
);

void SetIndexedSpecialPartValue(void *base, int outerIndex, int partIndex, int value);

void SetMiiSpecialPartTableEntry(
    int *tableBase,
    int entryIndex,
    int miiType,
    int selectedMiiListIndex,
    int bodyCostumeId,
    int unknownValue3
);

void SetMiiSpecialPartTableEntryTail(
    int *tableBase,
    int entryIndex,
    int recolorSlot1,
    int recolorSlot2,
    int recolorSlot3,
    int recolorSlot4
);

#endif
