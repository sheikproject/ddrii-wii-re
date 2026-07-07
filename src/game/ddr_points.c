#include "game/ddr_points.h"

int GetDDRPoints(void *playerDataOrSaveData) {
    char *bytes = (char *)playerDataOrSaveData;
    return *(int *)(bytes + DDR_POINTS_FIELD_OFFSET);
}

void SetDDRPoints(void *playerDataOrSaveData, int ddrPoints) {
    char *bytes = (char *)playerDataOrSaveData;
    *(int *)(bytes + DDR_POINTS_FIELD_OFFSET) = ddrPoints;
}

void SetupDDRPointsMenuDisplay(void *menuState, void *linkData) {
    (void)menuState;
    (void)linkData;
}

void UpdateDDRPointsDisplay(void *uiRoot, unsigned int ddrPoints) {
    void **fields = (void **)uiRoot;
    SetDDRPointsDisplayValue(fields[0x40 / 4], ddrPoints);
}

void SetDDRPointsDisplayValue(void *ddrPointsDisplay, unsigned int ddrPoints) {
    (void)ddrPointsDisplay;
    (void)ddrPoints;
}

int CalcDDRPointsBaseAward(void *pointsRules, int songGroupOrFolder, int chartId, int clearProgress) {
    (void)pointsRules;
    (void)songGroupOrFolder;
    (void)chartId;

    if (clearProgress < 0) {
        return 0;
    }
    if (clearProgress > 4) {
        clearProgress = 4;
    }

    static const int awards[] = { 500, 750, 1000, 1500, 2000 };
    return awards[clearProgress];
}

int CalcDDRPointsScoreThresholdAward(
    void *rulesEntry,
    unsigned int currentDDRPoints,
    int chartId,
    int scorePercentOrRank
) {
    (void)rulesEntry;
    (void)currentDDRPoints;
    (void)chartId;

    if (scorePercentOrRank >= 100) {
        return 1000;
    }
    if (scorePercentOrRank >= 90) {
        return 750;
    }
    if (scorePercentOrRank >= 75) {
        return 500;
    }
    if (scorePercentOrRank >= 60) {
        return 250;
    }
    return 0;
}

int CalcDDRPointsClearConditionAward(
    void *pointsRules,
    int songGroupOrFolder,
    int chartId,
    int clearConditionValue,
    int fullComboOrSpecialClearFlag
) {
    (void)pointsRules;
    (void)songGroupOrFolder;
    (void)chartId;

    if (fullComboOrSpecialClearFlag) {
        return 1500;
    }
    if (clearConditionValue >= 200) {
        return 1000;
    }
    if (clearConditionValue >= 100) {
        return 750;
    }
    if (clearConditionValue >= 50) {
        return 500;
    }
    return 0;
}

int CalcDDRPointsDifficultyTargetAward(
    void *rulesEntry,
    unsigned int currentDDRPoints,
    int chartId,
    int clearedDifficultyLevel
) {
    (void)rulesEntry;
    (void)currentDDRPoints;
    (void)chartId;

    if (clearedDifficultyLevel >= 5) {
        return 1000;
    }
    if (clearedDifficultyLevel >= 4) {
        return 750;
    }
    if (clearedDifficultyLevel >= 3) {
        return 500;
    }
    if (clearedDifficultyLevel >= 2) {
        return 250;
    }
    return 0;
}
