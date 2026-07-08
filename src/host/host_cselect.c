#include "host/host_cselect.h"

#include "render/render_engine.h"
#include "select/csel_mode.h"

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

enum HostInput {
    HOST_INPUT_NONE,
    HOST_INPUT_LEFT,
    HOST_INPUT_RIGHT,
    HOST_INPUT_CONFIRM,
    HOST_INPUT_BACK,
};

static int Host_ReadInput(void) {
    int key;

    if (!_kbhit()) {
        return HOST_INPUT_NONE;
    }

    key = _getch();
    if (key == 0 || key == 0xE0) {
        key = _getch();
        switch (key) {
            case 75:
                return HOST_INPUT_LEFT;
            case 77:
                return HOST_INPUT_RIGHT;
            default:
                return HOST_INPUT_NONE;
        }
    }

    switch (key) {
        case 'a':
        case 'A':
            return HOST_INPUT_LEFT;
        case 'd':
        case 'D':
            return HOST_INPUT_RIGHT;
        case '\r':
            return HOST_INPUT_CONFIRM;
        case 27:
        case 'b':
        case 'B':
            return HOST_INPUT_BACK;
        default:
            return HOST_INPUT_NONE;
    }
}

static const char *Host_GetModeName(int selectedModeIndex) {
    static const char *modeNames[] = {
        "Mode ID 1",
        "Mode ID 2",
        "Mode ID 3",
        "Mode ID 6",
        "Back / Title",
    };

    if (selectedModeIndex < 0 || selectedModeIndex >= 5) {
        return "Unknown";
    }
    return modeNames[selectedModeIndex];
}

static void Host_PrintCSelModeChoiceDetails(int selectedModeIndex) {
    const CSelModeChoice *choice = CSelMode_GetChoice(selectedModeIndex);

    if (choice == 0) {
        printf("CSelMode: selected index %d -> invalid\n", selectedModeIndex);
        return;
    }

    printf("CSelMode: selected index %d -> nextState=%d mode=%d sub=%d extra=%d\n",
           selectedModeIndex,
           choice->nextSelectState,
           choice->gameModeId,
           choice->gameModeSubId,
           choice->gameModeExtraId);
}

static void Host_DrawCSelModeMenu(int selectedModeIndex) {
    int i;
    const CSelModeChoice *choice;

    puts("");
    puts("---");
    puts("DDRII Host Skeleton");
    puts("===================");
    puts("");
    puts("CSelMode - Mode Select");
    puts("");

    for (i = 0; i < 5; i++) {
        printf("%s %s\n", i == selectedModeIndex ? ">" : " ", Host_GetModeName(i));
    }

    choice = CSelMode_GetChoice(selectedModeIndex);
    puts("");
    if (choice != 0) {
        printf("nextState=%d  mode=%d  sub=%d  extra=%d\n",
               choice->nextSelectState,
               choice->gameModeId,
               choice->gameModeSubId,
               choice->gameModeExtraId);
    }
    else {
        puts("Invalid selection");
    }

    puts("");
    puts("A/Left: previous   D/Right: next   Enter: confirm   Esc/B: exit");
}

static void Host_DrawCSelModeGl(const HostCSelectModule *module) {
    unsigned int backgroundColor = 0x74DDFDFF;
    (void)module;

    RenderBeginFrame();
    ApplyRenderConfig(0, &backgroundColor);
    RenderEndFrame();
}

int CSelect_TickHost(void *cSelect) {
    HostCSelectModule *module = (HostCSelectModule *)cSelect;
    int input;

    if (module->frame == 0) {
        puts("CSelect: enter CSelMode");
    }

    if (module->redrawNeeded) {
        Host_DrawCSelModeMenu(module->selectedModeIndex);
        module->redrawNeeded = 0;
    }

    Host_DrawCSelModeGl(module);

    input = Host_ReadInput();
    if (input == HOST_INPUT_LEFT) {
        module->selectedModeIndex = CSelMode_MoveSelection(module->selectedModeIndex, -1);
        module->redrawNeeded = 1;
    }
    else if (input == HOST_INPUT_RIGHT) {
        module->selectedModeIndex = CSelMode_MoveSelection(module->selectedModeIndex, 1);
        module->redrawNeeded = 1;
    }
    else if (input == HOST_INPUT_CONFIRM) {
        puts("CSelMode: confirm");
        Host_PrintCSelModeChoiceDetails(module->selectedModeIndex);
        return 1;
    }
    else if (input == HOST_INPUT_BACK) {
        puts("CSelMode: back/exit");
        return 1;
    }

    module->frame++;
    return 0;
}
