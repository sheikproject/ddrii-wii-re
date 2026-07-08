#include "runtime/boot_logo.h"

#include "platform/render_backend.h"
#include "render/render_engine.h"
#include "resource/resource_manager.h"

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

static ResourceHandle *gBootLogoLoadedResource;

static int BootLogoModule_ShouldSkip(const BootLogoModuleKnownFields *module,
                                     const BootLogoConfig *config) {
    if (module->stateTimer < config->inputSkipStartTime) {
        return 0;
    }

    return Platform_ConsumeConfirmPressed();
}

const char *BootLogoModule_GetLogoPath(int logoRegionOrLanguageIndex) {
    int count = (int)(sizeof(BootLogoLogoPaths) / sizeof(BootLogoLogoPaths[0]));

    if (logoRegionOrLanguageIndex < 0 || logoRegionOrLanguageIndex >= count) {
        logoRegionOrLanguageIndex = 0;
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

    bootLogoModule->logoResourceHandle = -1;
    bootLogoModule->logoTextureHandle = -1;
    bootLogoModule->logoRegionOrLanguageIndex = 5;
    bootLogoModule->screenWidth = 640;
    bootLogoModule->screenHeight = 480;
    bootLogoModule->state = 3;
    bootLogoModule->logoStepIndex = 0;
    bootLogoModule->logoFrameIndex = 0;
    bootLogoModule->frameAnimTimer = 0.0f;
    bootLogoModule->stateTimer = 0.0f;
    bootLogoModule->fadeAlpha = 1.0f;
}

int BootLogoModule_Tick(void *bootLogoModule, int nextModuleId) {
    BootLogoModuleKnownFields *module = (BootLogoModuleKnownFields *)bootLogoModule;
    const BootLogoConfig *config;

    if (module->state == 3) {
        puts("BootLogoModule_Tick: state 3 -> 4");
        module->logoStepIndex = module->skipLogoOrProgressiveFlag == 0 ? 0 : 1;
        module->fadeAlpha = 0.0f;
        module->state = 4;
    }

    if (module->state == 4) {
        const char *path = BootLogoModule_GetLogoPath(module->logoRegionOrLanguageIndex);
        ResourceHandle *resource = LoadResourceByPath(0, path, 0);

        puts("BootLogoModule_Tick: state 4 -> 5");
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
        printf("BootLogoModule: textureHandle=%d\n", module->logoTextureHandle);
        puts("BootLogoModule_Tick: state 5 -> 6");
        module->state = 6;
    }

    if (module->state == 6) {
        puts("BootLogoModule_Tick: state 6 -> 7");
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
        config = &BootLogoConfigTable[configIndex];
        module->stateTimer += 1.0f;
        module->frameAnimTimer += 1.0f;

        if (module->frameAnimTimer >= config->frameAnimInterval) {
            module->frameAnimTimer -= config->frameAnimInterval;
            module->logoFrameIndex = (module->logoFrameIndex + 1) % config->frameCount;
        }

        if (module->stateTimer >= config->holdTime) {
            module->state = 10;
        }
        else if (BootLogoModule_ShouldSkip(module, config)) {
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
