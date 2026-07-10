#ifndef DDRII_HOST_CSELECT_H
#define DDRII_HOST_CSELECT_H

typedef struct HostCSelectModule {
    int frame;
    int selectedModeIndex;
    int redrawNeeded;
    void *selectLinkData;
    unsigned int selectLinkSize;
    void *selectCommonLinkData;
    unsigned int selectCommonLinkSize;
} HostCSelectModule;

int CSelect_TickHost(void *cSelect);
void HostCSelect_SetCommonSelectResource(
    HostCSelectModule *module,
    void *selectCommonLinkData,
    unsigned int selectCommonLinkSize);

#endif
