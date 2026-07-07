#ifndef DDRII_SELECT_CHARACTER_SPECIAL_CATEGORIES_H
#define DDRII_SELECT_CHARACTER_SPECIAL_CATEGORIES_H

enum CharacterSpecialCategory {
    CHAR_SPECIAL_CATEGORY_FULL_APPLY_A = 0,
    CHAR_SPECIAL_CATEGORY_PARTIAL_APPLY = 1,
    CHAR_SPECIAL_CATEGORY_NORMAL = 2,
    CHAR_SPECIAL_CATEGORY_FULL_APPLY_B = 3,
};

int GetCharacterSpecialCategory(int characterId);
int GetCharacterSpecialApplyCount(int category);

#endif
