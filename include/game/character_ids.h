#ifndef DDRII_GAME_CHARACTER_IDS_H
#define DDRII_GAME_CHARACTER_IDS_H

enum CharacterId {
    CHAR_ID_MII = 0x52,
    CHAR_ID_MII_SELECT_SLOT = 0x50, /* Mii ID returned for select slot 3 by 0x8006C588 */

    CHAR_ID_EMI = 0xC8,
    CHAR_ID_DISCO = 0xC9,
    CHAR_ID_RUBY = 0xCA,
    CHAR_ID_YUNI = 0xCB,
    CHAR_ID_RAGE = 0xCC,
    CHAR_ID_RENA = 0xCD,
    CHAR_ID_NAOKI = 0xCE,
    CHAR_ID_JUN = 0xCF,
    CHAR_ID_U1 = 0xD0,

    CHAR_ID_INVALID = -1,
};

enum SpecialCharacterSlotId {
    CHAR_SLOT_ALL_RANDOM = 10000,
    CHAR_SLOT_RANDOM_MALE = 10001,
    CHAR_SLOT_RANDOM_FEMALE = 10002,
};

#endif
