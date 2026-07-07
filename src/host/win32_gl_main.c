#include "platform/render_backend.h"
#include "runtime/module_system.h"

int main(void) {
    int result;

    if (!Platform_InitOpenGLWindow("DDRII Static Recomp OpenGL Host", 640, 480)) {
        return 1;
    }

    result = GameMain();
    Platform_ShutdownOpenGLWindow();
    return result;
}
