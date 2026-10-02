#include "platform/render_backend.h"
#include "runtime/module_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void Host_SelectDataDirectory(void) {
    /* Game data is read from input\DATA relative to the working directory. When
       the exe is started from elsewhere (e.g. double-clicked in outputs\), use the
       exe folder or its parent, whichever holds input\DATA. */
    char directory[MAX_PATH];
    char *slash;
    int level;

    if (GetFileAttributesA("input\\DATA") != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    if (GetModuleFileNameA(0, directory, sizeof(directory)) == 0) {
        return;
    }
    for (level = 0; level < 2; level++) {
        char probe[MAX_PATH + 16];

        slash = strrchr(directory, '\\');
        if (slash == 0) {
            return;
        }
        *slash = '\0';
        snprintf(probe, sizeof(probe), "%s\\input\\DATA", directory);
        if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) {
            SetCurrentDirectoryA(directory);
            return;
        }
    }
}

int main(int argc, char **argv) {
    int result;

    if (getenv("DDRII_UNBUFFERED_LOG") != 0) {
        /* Debug aid: keep the log complete when the process is killed. */
        setvbuf(stdout, 0, _IONBF, 0);
    }
    Host_SelectDataDirectory();
    GameHost_ConfigureFromArgs(argc, argv);

    if (!Platform_InitOpenGLWindow("DDRII Static Recomp OpenGL Host", 640, 480)) {
        return 1;
    }

    result = GameMain();
    Platform_ShutdownOpenGLWindow();
    return result;
}
