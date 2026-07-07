#include "select/csel_mode.h"

const CSelModeChoice CSelMode_ChoiceTable[5] = {
    { 0x02, 0x01, 0x01, -1 },
    { 0x02, 0x02, 0x07, -1 },
    { 0x1B, 0x03, 0x05, -1 },
    { 0x15, 0x06, 0x00, -1 },
    { -1, -1, -1, -1 },
};

int CSelMode_ModeIdToSelectedIndex(int modeId) {
    switch (modeId) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 3:
            return 2;
        case 6:
            return 3;
        default:
            return 0;
    }
}

const CSelModeChoice *CSelMode_GetChoice(int selectedModeIndex) {
    if (selectedModeIndex < 0 || selectedModeIndex >= 5) {
        return 0;
    }
    return &CSelMode_ChoiceTable[selectedModeIndex];
}

int CSelMode_Init(void *cselMode) {
    (void)cselMode;

    /* Original initializes a 14-entry controller at +0x160.
       Each entry is 0x50 bytes and uses callbacks at 0x8006309C/0x800630D8. */
    return 0;
}

void CSelMode_OnEnter(void *cselMode, void *linkData) {
    (void)cselMode;
    (void)linkData;

    /* Original links the Czan resource, initializes all mode entries,
       configures layout/animation data, and sets modeState to 1. */
}

void CSelMode_Update(void) {
    /* Original reads input, changes selectedModeIndex, plays animations/sounds,
       and commits parentSelectData[0..2] from CSelMode_ChoiceTable. */
}

void CSelMode_SetInitialSelectedMode(void *cselMode) {
    (void)cselMode;

    /* Original reads **(cselMode + 0x10), maps mode IDs 1/2/3/6 to indices 0..3,
       and writes selectedModeIndex at cselMode + 0x134. */
}
