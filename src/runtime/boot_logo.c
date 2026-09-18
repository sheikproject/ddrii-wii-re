#include "runtime/boot_logo.h"

#include "game/cgame.h"
#include "platform/render_backend.h"
#include "render/render_engine.h"
#include "resource/resource_manager.h"
#include "runtime/memory.h"
#include "runtime/module_system.h"

#include <stdint.h>
#include <stdio.h>

const int BootLogoFrameTextureIndexTable0[BOOT_LOGO_FRAME_TEXTURE_TABLE0_COUNT] = {
    0,
    1,
    1,
    1,
};

const int BootLogoFrameTextureIndexTable1[BOOT_LOGO_FRAME_TEXTURE_TABLE1_COUNT] = {
    2,
    2,
};

const int BootLogoStepConfigIndexTable[BOOT_LOGO_CONFIG_COUNT] = {
    0,
    1,
};

const BootLogoConfig BootLogoConfigTable[BOOT_LOGO_CONFIG_COUNT] = {
    {
        BootLogoFrameTextureIndexTable0,
        2,
        0,
        0,
        300.0f,
        15.0f,
        0x00017FFF,
        90.0f,
        0xFFFFFFFF,
    },
    {
        BootLogoFrameTextureIndexTable1,
        1,
        0,
        0,
        90.0f,
        15.0f,
        0x00000810,
        90.0f,
        0xFFFFFFFF,
    },
};

static const char *BootLogoLogoPaths[] = {
    "logo/logo_JPA.tpl",
    "logo/logo_ENG.tpl",
    "logo/logo_FRA.tpl",
    "logo/logo_GER.tpl",
    "logo/logo_ITA.tpl",
    "logo/logo_SPA.tpl",
    "logo/logo_DUT.tpl",
};

static const int BootLogoRegionToLogoIndex[] = {
    1, /* resource row 0 -> English */
    1, /* resource row 1 -> English */
    1, /* resource row 2 -> English */
    2, /* resource row 3 -> French */
    5, /* resource row 4 -> Spanish */
    1, /* resource row 5 -> English */
};

static ResourceHandle *gBootLogoLoadedResource;

static int BootLogoModule_ShouldSkip(const BootLogoModuleKnownFields *module,
                                     const BootLogoConfig *config) {
    if (module->stateTimer < config->inputSkipStartTime) {
        return 0;
    }

    return Platform_ConsumeConfirmPressed();
}

static int BootLogoModule_ArePendingResourcesReady(void) {
    int *resourceManager = GlobalRuntimeContext_GetPointerAt(0x260);
    if (resourceManager == 0) {
        return 1;
    }

    return GlobalResourceManager260_UpdateProgress(resourceManager);
}

const char *BootLogoModule_GetLogoPath(int logoRegionOrLanguageIndex) {
    int count = (int)(sizeof(BootLogoLogoPaths) / sizeof(BootLogoLogoPaths[0]));
    int regionCount = (int)(sizeof(BootLogoRegionToLogoIndex) / sizeof(BootLogoRegionToLogoIndex[0]));

    if (0 <= logoRegionOrLanguageIndex && logoRegionOrLanguageIndex < regionCount) {
        logoRegionOrLanguageIndex = BootLogoRegionToLogoIndex[logoRegionOrLanguageIndex];
    }
    if (logoRegionOrLanguageIndex < 0 || logoRegionOrLanguageIndex >= count) {
        logoRegionOrLanguageIndex = 1;
    }
    return BootLogoLogoPaths[logoRegionOrLanguageIndex];
}

int BootLogoModule_GetFrameTextureIndex(int logoStepIndex, int logoFrameIndex, int widescreenMode) {
    const BootLogoConfig *config;
    int configIndex;
    int tableIndex;

    if (logoStepIndex < 0 || logoStepIndex >= BOOT_LOGO_CONFIG_COUNT) {
        return 0;
    }

    configIndex = BootLogoStepConfigIndexTable[logoStepIndex];
    config = &BootLogoConfigTable[configIndex];
    if (config->frameCount <= 0) {
        return 0;
    }

    logoFrameIndex %= config->frameCount;
    if (logoFrameIndex < 0) {
        logoFrameIndex = 0;
    }

    tableIndex = logoFrameIndex * 2 + (widescreenMode ? 1 : 0);
    return config->frameTextureIndexTable[tableIndex];
}

void BootLogoModule_Init(void *module) {
    BootLogoModuleKnownFields *bootLogoModule = (BootLogoModuleKnownFields *)module;

    ClearMemory(bootLogoModule, 0, sizeof(*bootLogoModule));
    bootLogoModule->logoResourceHandle = 0;
    bootLogoModule->logoTextureHandle = -1;
}

int BootLogoModule_OnEnter(void *bootLogoModule, int moduleId) {
    BootLogoModuleKnownFields *module = (BootLogoModuleKnownFields *)bootLogoModule;
    int *playerDataManager;
    int *submanager274;
    int *globalContext;
    int logoRegionOrLanguageIndex;

    module->logoRegionOrLanguageIndex = 1;
    module->screenWidth = 640;
    module->screenHeight = 480;
    module->renderConfig = 0;
    module->state = 3;
    module->logoStepIndex = 0;
    module->logoFrameIndex = 0;
    module->frameAnimTimer = 0.0f;
    module->stateTimer = 0.0f;
    module->skipLogoOrProgressiveFlag = 0;
    module->fadeAlpha = 1.0f;

    playerDataManager = GlobalRuntimeContext_GetPointerAt(0x258);
    if (PlayerDataState_GetCurrentValue(playerDataManager) == 8) {
        module->skipLogoOrProgressiveFlag = 1;
    }

    globalContext = GlobalRuntimeContext_Get();
    if (globalContext != 0) {
        logoRegionOrLanguageIndex = globalContext[0x8c / 4];
        if (0 <= logoRegionOrLanguageIndex && logoRegionOrLanguageIndex < 10) {
            module->logoRegionOrLanguageIndex = logoRegionOrLanguageIndex;
        }
    }

    submanager274 = GlobalRuntimeContext_GetPointerAt(0x274);
    GlobalSubManager274_CopyRgba48(submanager274, (unsigned char *)&module->renderConfig);
    return moduleId;
}

int BootLogoModule_Tick(void *bootLogoModule, int nextModuleId) {
    BootLogoModuleKnownFields *module = (BootLogoModuleKnownFields *)bootLogoModule;
    const BootLogoConfig *config;

    if (module->state == 3) {
        module->logoStepIndex = module->skipLogoOrProgressiveFlag == 0 ? 0 : 1;
        module->fadeAlpha = 0.0f;
        module->state = 4;
    }

    if (module->state == 4) {
        const char *path = BootLogoModule_GetLogoPath(module->logoRegionOrLanguageIndex);
        ResourceHandle *resource = LoadResourceByPath(0, path, 0);

        gBootLogoLoadedResource = resource;
        module->logoResourceHandle = resource != 0 && resource->loaded ? 0 : -1;
        module->logoFrameIndex = 0;
        module->state = 5;
    }

    if (module->state == 5) {
        if (module->logoResourceHandle != -1) {
            TextureSlotKnownFields textureSlot;
            TextureManagerKnownFields textureManager;

            textureManager.slots = &textureSlot;
            textureManager.nextTextureSlot = 0;
            textureManager.slotCount = 1;
            module->logoTextureHandle =
                (int)CreateTextureFromTplResource(&textureManager,
                                                  gBootLogoLoadedResource->data,
                                                  gBootLogoLoadedResource->size,
                                                  0xFFFFFFFFu);
        }
        if (module->skipLogoOrProgressiveFlag == 0) {
            BootResourceBundle_StartLoading(GameMain_GetBootResourceBundle());
        }
        else {
            CGameUiRoot_AdvanceSelectionPanelState(GameMain_GetUiRootManager());
        }
        module->state = 6;
    }

    if (module->state == 6) {
        module->stateTimer = 0.0f;
        module->state = 7;
    }

    if (module->state == 7) {
        module->stateTimer += 1.0f;
        module->fadeAlpha = module->stateTimer / 20.0f;
        if (module->fadeAlpha >= 1.0f) {
            module->fadeAlpha = 1.0f;
            module->state = 8;
        }
    }

    if (module->state == 8) {
        module->stateTimer = 0.0f;
        module->frameAnimTimer = 0.0f;
        module->state = 9;
    }

    if (module->state == 9) {
        int configIndex = BootLogoStepConfigIndexTable[module->logoStepIndex];
        int resourcesReady;
        config = &BootLogoConfigTable[configIndex];
        module->stateTimer += 1.0f;
        module->frameAnimTimer += 1.0f;

        if (module->frameAnimTimer >= config->frameAnimInterval) {
            module->frameAnimTimer -= config->frameAnimInterval;
            module->logoFrameIndex = (module->logoFrameIndex + 1) % config->frameCount;
        }

        resourcesReady =
            module->logoStepIndex <= 0 || BootLogoModule_ArePendingResourcesReady();
        if (((module->stateTimer >= config->holdTime) ||
             BootLogoModule_ShouldSkip(module, config)) &&
            resourcesReady) {
            module->state = 10;
        }
    }

    if (module->state == 10) {
        module->stateTimer = 0.0f;
        module->state = 11;
    }

    if (module->state == 11) {
        module->stateTimer += 1.0f;
        module->fadeAlpha = 1.0f - module->stateTimer / 20.0f;
        if (module->fadeAlpha <= 0.0f) {
            module->fadeAlpha = 0.0f;
            module->logoFrameIndex = 0;
            if (module->logoStepIndex < 1) {
                module->logoStepIndex++;
                module->state = 6;
            }
            else {
                if (module->skipLogoOrProgressiveFlag == 0) {
                    BootResourceBundle_ApplyLoadedResources(GameMain_GetBootResourceBundle());
                    Runtime_SetSoundArchiveReloadGuard(0);
                }
                module->state = 12;
            }
        }
    }

    if (module->state == 12) {
        module->state = 13;
    }

    if (module->state == 13) {
        return BOOT_LOGO_MODULE_ID_NEXT;
    }

    return nextModuleId;
}

void BootLogoModule_Draw(void *bootLogoModule) {
    BootLogoModuleKnownFields *module = (BootLogoModuleKnownFields *)bootLogoModule;
    BootLogoConfig config = BootLogoConfigTable[BootLogoStepConfigIndexTable[module->logoStepIndex]];
    int textureIndex;
    int textureWidth;
    int textureHeight;
    unsigned int color = 0xFFFFFFFF;
    unsigned int fadeColor;
    float fadeOverlayAlpha;
    RenderQuad quad;

    if (module->logoTextureHandle == -1) {
        return;
    }

    ApplyRenderConfig(0, &config.renderConfigColor);
    textureIndex = BootLogoModule_GetFrameTextureIndex(module->logoStepIndex, module->logoFrameIndex, 0);
    if (!GetTextureDimensions((void *)(long)module->logoTextureHandle, textureIndex, &textureWidth, &textureHeight)) {
        textureWidth = 0;
        textureHeight = 0;
    }

    quad.x = (float)(module->screenWidth / 2 - textureWidth / 2);
    quad.y = (float)(module->screenHeight / 2 - textureHeight / 2);
    quad.z = 0.0f;
    quad.width = (float)textureWidth;
    quad.height = (float)textureHeight;
    DrawTexturedQuad(&quad, textureWidth, textureHeight, (unsigned char *)&color,
                     (void *)(long)module->logoTextureHandle, textureIndex);

    fadeOverlayAlpha = 1.0f - module->fadeAlpha;
    if (fadeOverlayAlpha < 0.0f) {
        fadeOverlayAlpha = 0.0f;
    }
    if (fadeOverlayAlpha > 1.0f) {
        fadeOverlayAlpha = 1.0f;
    }

    fadeColor = (config.renderConfigColor & 0xFFFFFF00u) |
                ((unsigned int)(fadeOverlayAlpha * 255.0f) & 0xFFu);
    DrawFilledRect(0, 0, 0, module->screenWidth, module->screenHeight, &fadeColor, 0);
}
