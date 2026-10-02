#include "runtime/module_system.h"

int main(int argc, char **argv) {
    GameHost_ConfigureFromArgs(argc, argv);
    return GameMain();
}
