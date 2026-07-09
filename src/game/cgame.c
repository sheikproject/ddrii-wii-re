#include "game/cgame.h"

void CGame_PrepareManagersAndResources(int *cgame) {
    /* 0x8003CDC4 is the CGame setup/loading state machine.

       Key fields:
       cgame +0x008 -> next/active module state, set to 5 on error and 2 when ready
       cgame +0x00C -> setup substate
       cgame +0x0B8 -> pending/current game setup mode; -1 means use player data directly
       cgame +0x3F0..+0x428 -> owned CGame subsystem pointers created by CGameFactorySetup

       Substates:
       0 -> reset managers/subsystems and choose direct player-data setup or boot-temp loading
       1 -> wait for gBootTempManager to finish loading
       2 -> apply boot resource bundle and run CGame readiness check
       3 -> wait for external/resource managers, then wire CGame subsystems
       8 -> wait for DAT_802E71F8 +0x0C to clear
       0x0C -> ready; sets cgame +0x08 to module state 2

       The important resource path is:
       if substate 1 and FUN_8002202C(gBootTempManager) != 0:
         BootResourceBundle_ApplyLoadedResources(gBootTempManager)
         substate = 2
    */
    (void)cgame;
}
