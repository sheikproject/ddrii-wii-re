#ifndef DDRII_SELECT_CHARACTER_SELECT_PAGES_H
#define DDRII_SELECT_CHARACTER_SELECT_PAGES_H

#define CSEL_CHARA_PAGE_COUNT 7
#define CSEL_CHARA_PAGE_SLOT_COUNT 12
#define CSEL_CHARA_BLANK_SLOT 0x15

extern const int gCharacterSelectPageSlots[CSEL_CHARA_PAGE_COUNT][CSEL_CHARA_PAGE_SLOT_COUNT];

int IsCharacterSelectDisplaySlotBlank(int displaySlot);

#endif
