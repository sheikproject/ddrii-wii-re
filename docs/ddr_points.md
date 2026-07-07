# DDR Points

## Confirmed Field

```text
GetDDRPoints      0x800F3B68
SetDDRPoints      0x800F3B70
field offset      playerDataOrSaveData + 0x4E8
example base      0x8053FF24
example field     0x8054040C
```

`SetDDRPoints` was hit by a write watchpoint:

```text
PC = 800F3B70
LR = 800E52C8
r3 = 8053FF24
r4 = 00000DAC
```

`0xDAC` is `3500`, so this is:

```c
SetDDRPoints(0x8053FF24, 3500);
```

## Getter And Setter

```c
int GetDDRPoints(void *playerDataOrSaveData)
{
  return *(int *)((char *)playerDataOrSaveData + 0x4E8);
}

void SetDDRPoints(void *playerDataOrSaveData, int ddrPoints)
{
  *(int *)((char *)playerDataOrSaveData + 0x4E8) = ddrPoints;
}
```

## Display Chain

```text
SetupDDRPointsMenuDisplay       0x8006FAA0
UpdateDDRPointsDisplay          0x801004A0
SetDDRPointsDisplayValue        0x8010A2D8
CzanLinkManager_Init            0x80160308
CzanLinkManager_SetLink         0x801603D4
```

`SetupDDRPointsMenuDisplay` initializes a `CzanLinkManager`, links a resource, calls
`GetDDRPoints(DAT_802E70D0 + 0x14A4)`, and updates or clears related menu-display state.

`UpdateDDRPointsDisplay` is a thin wrapper:

```c
void UpdateDDRPointsDisplay(int uiRoot, uint ddrPoints)
{
  SetDDRPointsDisplayValue(*(undefined4 *)(uiRoot + 0x40), ddrPoints);
}
```

`SetDDRPointsDisplayValue` updates the numeric display. Values below `10,000,000` are
shown as a 7-digit number. Values at or above `10,000,000` use a fallback display path.
The `COMPLETE` texture is tied to completion/final-unlock state, observed at
`2,000,000` points, not directly to the `10,000,000` cap.

## Award Flow

The post-song/update flow around `0x800E52C8` does:

```c
SetDDRPoints(DAT_802E70D0 + 0x14A4, newDDRPoints);
ddrPointsUpdatedFlag = 1;
UpdateDDRPointsDisplay(DAT_802E70C0, displayedDDRPoints);
```

Confirmed total formula:

```c
earnedDDRPoints =
    baseAward +
    scoreThresholdAward +
    clearConditionAward +
    difficultyTargetAward;

newDDRPoints = currentDDRPoints + earnedDDRPoints;
if (newDDRPoints > 10000000) {
    newDDRPoints = 10000000;
}
```

Useful field names in the caller:

```text
iVar11          -> currentDDRPoints
iVar8           -> earnedDDRPoints
newDDRPoints    -> newDDRPoints
iVar4 + 0x1F30  -> ddrPointsUpdatedFlag
iVar4 + 0x1DD0  -> displayedDDRPoints / currentDDRPointsForDisplay
```

## Award Functions

```text
CalcDDRPointsBaseAward              0x80120334
CalcDDRPointsScoreThresholdAward    0x801203F4
CalcDDRPointsClearConditionAward    0x80120508
CalcDDRPointsDifficultyTargetAward  0x801205AC
```

`CalcDDRPointsBaseAward`:

```text
requiredProgress 0 ->  500
requiredProgress 1 ->  750
requiredProgress 2 -> 1000
requiredProgress 3 -> 1500
requiredProgress 4 -> 2000
```

`CalcDDRPointsScoreThresholdAward` uses threshold tables based on current DDR Points:

```text
0-899,999         DAT_802E9600
900,000-1,679,999 DAT_802E9608
1,680,000-9,999,999 DAT_802E9610
```

Awards:

```text
threshold 60  ->  250
threshold 75  ->  500
threshold 90  ->  750
threshold 100 -> 1000
```

`CalcDDRPointsClearConditionAward`:

```text
requiredCondition 50  ->  500
requiredCondition 100 ->  750
requiredCondition 200 -> 1000
requiredCondition -1 + fullComboOrSpecialClearFlag -> 1500
```

`CalcDDRPointsDifficultyTargetAward` uses target tables based on current DDR Points:

```text
0-899,999         DAT_80291E18
900,000-1,679,999 DAT_80291E3C
1,680,000-9,999,999 DAT_80291E60
```

Awards:

```text
target 2 ->  250
target 3 ->  500
target 4 ->  750
target 5 -> 1000
```
