#include "select/character_special_categories.h"

int GetCharacterSpecialCategory(int characterId) {
    if ((unsigned int)(characterId - 0x52) < 8) {
        if ((unsigned int)(characterId - 0x54) < 2) {
            return CHAR_SPECIAL_CATEGORY_FULL_APPLY_A;
        }
        if ((unsigned int)(characterId - 0x52) < 6) {
            return CHAR_SPECIAL_CATEGORY_FULL_APPLY_B;
        }
        return CHAR_SPECIAL_CATEGORY_PARTIAL_APPLY;
    }

    return CHAR_SPECIAL_CATEGORY_NORMAL;
}

int GetCharacterSpecialApplyCount(int category) {
    switch (category) {
        case CHAR_SPECIAL_CATEGORY_FULL_APPLY_A:
        case CHAR_SPECIAL_CATEGORY_FULL_APPLY_B:
            return 4;
        case CHAR_SPECIAL_CATEGORY_PARTIAL_APPLY:
            return 2;
        case CHAR_SPECIAL_CATEGORY_NORMAL:
        default:
            return 0;
    }
}
