#include "game/character_ids.h"
#include "select/csel_chara.h"

int GetCharacterIdForCurrentSelectSlot(int slot) {
    switch (slot) {
        case 0:
            return CHAR_SLOT_ALL_RANDOM;
        case 1:
            return CHAR_SLOT_RANDOM_FEMALE;
        case 2:
            return CHAR_SLOT_RANDOM_MALE;
        case 3:
            return CHAR_ID_MII_SELECT_SLOT;
        case 4:
            return CHAR_ID_EMI;
        case 5:
            return CHAR_ID_DISCO;
        case 6:
            return CHAR_ID_YUNI;
        case 7:
            return CHAR_ID_RAGE;
        case 8:
            return CHAR_ID_RUBY;
        case 12:
            return CHAR_ID_RENA;
        case 13:
            return CHAR_ID_NAOKI;
        case 14:
            return CHAR_ID_JUN;
        case 15:
            return CHAR_ID_U1;
        default:
            return CHAR_ID_INVALID;
    }
}
