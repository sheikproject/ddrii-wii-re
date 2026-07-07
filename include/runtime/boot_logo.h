#ifndef DDRII_RUNTIME_BOOT_LOGO_H
#define DDRII_RUNTIME_BOOT_LOGO_H

#define BOOT_LOGO_MODULE_ID_NEXT 2
#define BOOT_LOGO_CONFIG_COUNT 2
#define BOOT_LOGO_FRAME_TEXTURE_TABLE0_COUNT 4
#define BOOT_LOGO_FRAME_TEXTURE_TABLE1_COUNT 2

const char *BootLogoModule_GetLogoPath(int logoRegionOrLanguageIndex);

typedef struct BootLogoModuleKnownFields {
    int logoResourceHandle;
    int logoTextureHandle;
    int logoRegionOrLanguageIndex;
    int screenWidth;
    int screenHeight;
    int renderConfig;
    int state;
    int logoStepIndex;
    int logoFrameIndex;
    float frameAnimTimer;
    float stateTimer;
    int skipLogoOrProgressiveFlag;
    float fadeAlpha;
} BootLogoModuleKnownFields;

typedef struct BootLogoConfig {
    const int *frameTextureIndexTable;
    int frameCount;
    int widescreenAdjustFlag;
    int fadeTimingIndex;
    float holdTime;
    float inputSkipStartTime;
    int inputMask;
    float frameAnimInterval;
    unsigned int renderConfigColor;
} BootLogoConfig;

extern const int BootLogoFrameTextureIndexTable0[BOOT_LOGO_FRAME_TEXTURE_TABLE0_COUNT];
extern const int BootLogoFrameTextureIndexTable1[BOOT_LOGO_FRAME_TEXTURE_TABLE1_COUNT];
extern const int BootLogoStepConfigIndexTable[BOOT_LOGO_CONFIG_COUNT];
extern const BootLogoConfig BootLogoConfigTable[BOOT_LOGO_CONFIG_COUNT];

void BootLogoModule_Init(void *module);
int BootLogoModule_Tick(void *bootLogoModule, int nextModuleId);
void BootLogoModule_Draw(void *bootLogoModule);
int BootLogoModule_GetFrameTextureIndex(int logoStepIndex, int logoFrameIndex, int widescreenMode);

#endif
