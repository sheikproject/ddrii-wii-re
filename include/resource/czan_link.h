#ifndef DDRII_RESOURCE_CZAN_LINK_H
#define DDRII_RESOURCE_CZAN_LINK_H

typedef struct CzanLinkBlock {
    const unsigned char *data;
    unsigned int size;
} CzanLinkBlock;

int CzanLinkResource_IsValid(const void *linkData, unsigned int resourceSize);
unsigned int CzanLinkResource_GetBlockCount(const void *linkData, unsigned int resourceSize);
int CzanLinkResource_GetBlock(
    const void *linkData,
    unsigned int resourceSize,
    unsigned int blockIndex,
    CzanLinkBlock *outBlock);
void CzanLinkManager_Init(int *linkManager);
void CzanLinkManager_SetLink(int *linkManager, void *linkData);
int CzanLinkManager_InitAndSetLink(int linkManager, int linkData);
unsigned int CzanLinkManager_GetBlockCount(int *linkManager);
const unsigned char *CzanLinkManager_GetBlock(int *linkManager, int blockIndex, unsigned int *outSize);
int CzanLinkManager_GetBlockInfo(int *linkManager, int blockIndex, void **outBlock, int *outSize);
int CzanLinkManager_Release(int linkManager, short releaseMode);

#endif
