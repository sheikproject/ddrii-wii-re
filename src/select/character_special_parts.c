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
