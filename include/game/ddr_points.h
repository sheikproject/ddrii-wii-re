#ifndef DDRII_GAME_DDR_POINTS_H
#define DDRII_GAME_DDR_POINTS_H

#define DDR_POINTS_FIELD_OFFSET 0x4E8
#define DDR_POINTS_MENU_PLAYER_DATA_OFFSET 0x14A4
#define DDR_POINTS_MAX 10000000
#define DDR_POINTS_FINAL_UNLOCK_THRESHOLD 2000000

int GetDDRPoints(void *playerDataOrSaveData);
void SetDDRPoints(void *playerDataOrSaveData, int ddrPoints);

void SetupDDRPointsMenuDisplay(void *menuState, void *linkData);
void UpdateDDRPointsDisplay(void *uiRoot, unsigned int ddrPoints);
void SetDDRPointsDisplayValue(void *ddrPointsDisplay, unsigned int ddrPoints);

int CalcDDRPointsBaseAward(void *pointsRules, int songGroupOrFolder, int chartId, int clearProgress);
int CalcDDRPointsScoreThresholdAward(
    void *rulesEntry,
    unsigned int currentDDRPoints,
    int chartId,
    int scorePercentOrRank
);
int CalcDDRPointsClearConditionAward(
    void *pointsRules,
    int songGroupOrFolder,
    int chartId,
    int clearConditionValue,
    int fullComboOrSpecialClearFlag
);
int CalcDDRPointsDifficultyTargetAward(
    void *rulesEntry,
    unsigned int currentDDRPoints,
    int chartId,
    int clearedDifficultyLevel
);

#endif
