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
int CzanLinkManager_InitAndSetLink(int linkManager, int linkData);
int CzanLinkManager_Release(int linkManager, short releaseMode);

#endif
