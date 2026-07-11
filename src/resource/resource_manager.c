#include "resource/resource_manager.h"

#include "resource/czan_snd_read.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ResourceHandle gLastLoadedResource;
static unsigned char *gLastLoadedResourceData;

static int ReadFile(const char *path, void **outData, int *outSize) {
    FILE *file = fopen(path, "rb");
    long size;
    void *data;

    if (file == 0) {
        return 0;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return 0;
    }

    data = malloc((size_t)size);
    if (data == 0) {
        fclose(file);
        return 0;
    }

    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return 0;
    }

    fclose(file);
    *outData = data;
    *outSize = (int)size;
    return 1;
}

ResourceHandle *LoadResourceByPath(void *resourceManager, const char *path, int flags) {
    char hostPath[512];
    (void)resourceManager;

    if (gLastLoadedResourceData != 0) {
        free(gLastLoadedResourceData);
        gLastLoadedResourceData = 0;
    }

    snprintf(hostPath, sizeof(hostPath), "input\\DATA\\%s", path);
    {
        char *p;
        for (p = hostPath; *p != '\0'; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
    }

    gLastLoadedResource.path = path;
    gLastLoadedResource.data = 0;
    gLastLoadedResource.size = 0;
    gLastLoadedResource.loaded = ReadFile(hostPath, &gLastLoadedResource.data, &gLastLoadedResource.size);
    gLastLoadedResourceData = (unsigned char *)gLastLoadedResource.data;

    printf("resource: LoadResourceByPath path=%s hostPath=%s flags=%d loaded=%d size=%d\n",
           path,
           hostPath,
           flags,
           gLastLoadedResource.loaded,
           gLastLoadedResource.size);
    return &gLastLoadedResource;
}

int ResourceSlotManager_ClaimFreeSlot(int *slotPool) {
    /* 0x80186F4C scans a slot pool for the first free 0x290-byte record.

       Confirmed pool fields:
       slotPool +0x56B8 -> slot record array base
       slotPool +0x56BC -> slot record count
       slot record +0x04 bit 0 -> in-use flag

       When a free slot is found, the original sets bit 0 and returns the slot
       index. If all slots are already in use, it returns -1. */
    if (slotPool == 0) {
        return -1;
    }

    return -1;
}

int *ResourceSlotManager_GetClaimedSlot(int *slotPool, int slotIndex) {
    /* 0x801871A8 validates a slot index and returns the claimed 0x290-byte record.

       Confirmed behavior:
       - returns null when slotIndex < 0
       - returns null when slotIndex >= slotPool +0x56BC count
       - computes slot = *(slotPool +0x56B8) + slotIndex * 0x290
       - returns null unless slot +0x04 bit 0 is set
       - otherwise returns slot */
    (void)slotIndex;
    if (slotPool == 0) {
        return 0;
    }

    return 0;
}

int ResourceSlotManager_AllocateSlot(int *slotManager, int setupData) {
    /* 0x80024EA8 allocates or reserves one slot from gManager_802E70A8.

       Confirmed original flow:
       - return -1 when slotManager[0] is null
       - slotIndex = ResourceSlotManager_ClaimFreeSlot(slotManager[0])
       - return -1 when no slot is available
       - slotObject = ResourceSlotManager_GetClaimedSlot(slotManager[0], slotIndex)
       - if setupData != 0, call FUN_801843CC(slotObject, setupData)
       - return slotIndex

       Callers currently pass setupData=0 from the CzanModel owner resource-group
       path, so this behaves as a plain slot/handle allocator there. */
    (void)setupData;
    if (slotManager == 0 || slotManager[0] == 0) {
        return -1;
    }

    return -1;
}

int ResourceSlotHandle_IsActivePending(int *slotHandle) {
    /* 0x80024FA4 checks the current slot record for a specific active/pending
       flag state.

       Confirmed original flow:
       - if slotHandle[0] exists, get slotHandle[1] through
         ResourceSlotManager_GetClaimedSlot
       - return 1 when slot +0x230 bit 0 is set and bit 1 is clear
       - otherwise return 0 */
    if (slotHandle == 0 || slotHandle[0] == 0) {
        return 0;
    }

    return 0;
}

void ResourceSlotHandle_Rebind(int *slotHandle, int resourceOrPayload, int setupData) {
    /* 0x80025668 releases an existing slot handle, allocates a replacement slot,
       optionally initializes it, then applies resource/payload data.

       Confirmed original flow:
       - if slotHandle[0] exists, release slotHandle[1] through FUN_80186FB8
       - slotHandle[1] = -1
       - allocate a new slot with ResourceSlotManager_ClaimFreeSlot(slotHandle[0])
       - if setupData != 0, initialize the slot object through FUN_801843CC
       - store the new slot index in slotHandle[1]
       - fetch the slot object through ResourceSlotManager_GetClaimedSlot
       - call CzanMovieObj_Reset(slotObject)
       - call CzanMovieObj_LoadResource(slotObject, resourceOrPayload) */
    (void)resourceOrPayload;
    (void)setupData;
    if (slotHandle == 0) {
        return;
    }

    slotHandle[1] = -1;
}

void MovieSlotHandle_ResetClaimedSlot(int *slotHandle) {
    /* 0x80025104 resets the claimed CzanMovieObj for a movie slot handle.

       Original flow:
       - if slotHandle[0] exists, fetch the claimed slot object through
         ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotHandle[1])
       - if the slot object exists, call CzanMovieObj_Reset(slotObject)

       This is a movie-slot reset wrapper, not a file/resource loader. */
    (void)slotHandle;
}

int MovieSlotHandle_IsReadyForDisplay(int *slotHandle, int slotIndex) {
    /* 0x80025148 checks whether a claimed CzanMovieObj slot is ready enough for
       display/playback.

       Original flow:
       - if the current slot is active/pending according to ResourceSlotHandle_IsActivePending,
         return 0
       - fetch the claimed movie slot by slotIndex
       - query stream progress from the sound/video readers at slot +0x08 and +0xE4
       - return 1 only when video progress is at least 60% and audio/progressive data
         is at least 50%

       This is a readiness/progress check, not a reset or load call. */
    (void)slotHandle;
    (void)slotIndex;
    return 0;
}

void MovieSlotHandle_SetObjectEnabled(int *slotHandle, int slotIndex, int enabled) {
    /* 0x80025248 fetches one claimed CzanMovieObj slot and forwards enabled to the
       movie object's child/object record at +0x114.

       Original flow:
       - if slotHandle[0] exists, fetch ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
       - if the slot object exists, call FUN_80190440(slotObject +0x114, enabled)

       This is the small on/off switch used by bank-5/movie binding mode changes. */
    (void)slotHandle;
    (void)slotIndex;
    (void)enabled;
}

void MovieSlotHandle_LoadResource(int *slotHandle, int slotIndex, int resourceOrPath) {
    /* 0x80024F3C fetches a claimed CzanMovieObj slot, resets it, then binds a THP
       resource/path through CzanMovieObj_LoadResource(slotObject, resourceOrPath).

       This is the path-binding wrapper used by active-controller and select-common
       movie background code after a movie slot has already been allocated. */
    (void)slotHandle;
    (void)slotIndex;
    (void)resourceOrPath;
}

void MovieSlotHandle_StartPlayback(int *slotHandle, int slotIndex, int enabled) {
    /* 0x8002500C starts/prepares playback on a claimed CzanMovieObj slot.

       Original flow:
       - fetch ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
       - call FUN_80185150(slotObject)
       - clamp the input scalar to the movie fade/start range and store it at +0x150
       - call FUN_80184FE8(slotObject, enabled, 0)

       The common callers pass 1.0f as the scalar, gManager_802E70A8 as the handle,
       and an enable flag chosen from the active-controller movie category. */
    (void)slotHandle;
    (void)slotIndex;
    (void)enabled;
}

int *MovieSlotHandle_GetClaimedObject(int *slotHandle, int slotIndex) {
    /* 0x80025368 is the thin getter for the current claimed CzanMovieObj slot.

       Original behavior:
       if slotHandle[0] exists:
         return ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
       return null */
    (void)slotIndex;
    if (slotHandle == 0 || slotHandle[0] == 0) {
        return 0;
    }

    return 0;
}

void MovieSlotHandle_SetPlacementRect(
    int *slotHandle,
    int slotIndex,
    double x,
    double y,
    double width,
    double height) {
    /* 0x80025508 writes four float placement/timing values into the claimed movie
       object. The original mirrors x/y into +0x264 and integer copies at +0x244/
       +0x248, then mirrors width/height into +0x26C and integer copies at
       +0x24C/+0x250.

       ActiveControllerMovieBindings_StartCategoryMovie uses it for category byte 1
       after starting the movie playback. */
    (void)slotHandle;
    (void)slotIndex;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void MovieSlotHandle_SetPlaybackFlag278(int *slotHandle, int slotIndex, int value) {
    /* 0x8002561C writes one playback/control value to claimed movie object +0x278.
       The active-controller stage movie path passes zero after applying placement
       data, so keep the field-specific name until the flag meaning is confirmed. */
    (void)slotHandle;
    (void)slotIndex;
    (void)value;
}

int ActiveControllerMovieBindings_HasPendingSlots(int *movieBindings, int mode) {
    /* 0x80055314 checks whether active controller movie/background slots are still
       pending.

       mode 1:
       - if global transition flag movieBindings +0xB36C is clear, checks special
         slot +0xB34C and category slots +0xB340/+0xB344/+0xB348 for active/pending
         movie slots through ResourceSlotHandle_IsActivePending.
       - if no active/pending slot is found, validates that required slots are ready
         through MovieSlotHandle_IsReadyForDisplay.

       mode 3:
       - checks category slots 1..2 and returns pending when any valid slot is not
         ready for display.

       Return value is nonzero while a binding is still pending/not ready. */
    (void)movieBindings;
    (void)mode;
    return 0;
}

void ActiveControllerMovieBindings_Reset(int *movieBindings) {
    /* 0x80055268 clears the active controller's movie/background binding records.

       Confirmed fields:
       movieBindings +0xB360 -> cleared to zero
       movieBindings +0xB34C -> special slot index for category 3
       movieBindings +0xB340/+0xB344/+0xB348 -> slot indices for categories 0..2
       movieBindings +0xB334/+0xB338/+0xB33C -> per-category active flags cleared

       Each valid slot index is released/reset through MovieSlotHandle_ResetClaimedSlot
       against gManager_802E70A8. */
    (void)movieBindings;
}

void ActiveControllerMovieBindings_SetMode(int *movieBindings, int mode) {
    /* 0x80055090 changes the active controller movie/background binding mode.

       Confirmed behavior:
       - stores mode at movieBindings +0xB360
       - mode 1 loads/rebinds categories 0..3 through
         ActiveControllerMovieBindings_LoadCategoryMovie and clears transition flags
         at +0xF068/+0xF06C
       - mode 2 enables the special/category movie slots through
         MovieSlotHandle_SetObjectEnabled, refreshes categories 1..2 through
         ActiveControllerMovieBindings_StartCategoryMovie, and sets +0xF06C = 1
       - mode 4 enables the special/current mode slot so it can be displayed during
         the timed controller transition

       This helper does not parse THP data itself; it controls which claimed movie
       slots are active for the active gameplay controller. */
    (void)movieBindings;
    (void)mode;
}

void ActiveControllerMovieBindings_LoadCategoryMovie(int *movieBindings, unsigned int category) {
    /* 0x80055590 chooses and binds the THP movie path for one active-controller
       background category.

       Confirmed path rules:
       - category 3 uses the special slot +0xB34C and binds
         "movie/stage/single01_w.thp"
       - category type byte 0 -> "movie/stage/upt01.thp"
       - category type byte 1 -> "movie/bgv/%s.thp" using the category string at
         owner +0xB328/+0xB32C/+0xB330
       - category type byte 2 -> randomized numbered stage movie path based on
         owner +0xB368:
           1: "movie/stage/upt_%s%02d.thp"
           2: "movie/stage/pop_%s%02d.thp"
           4: "movie/stage/mvo_%s%02d.thp"
           5/default: "movie/stage/fvo_%s%02d.thp"
       - category type byte 3 -> "movie/stage/%s.thp"
       - category type byte 4 -> "movie/zz_pv/ddr%03d.thp"

       After binding the path through MovieSlotHandle_LoadResource, several
       stage-movie paths mark movie object fields +0x284 and +0x288 as enabled. */
    (void)movieBindings;
    (void)category;
}

void ActiveControllerMovieBindings_StartCategoryMovie(int *movieBindings, unsigned int category) {
    /* 0x80055914 starts/enables one already-bound active-controller movie category.

       Confirmed behavior:
       - category 3 uses special slot +0xB34C; other categories use +0xB340 + category*4
       - returns when the slot id is -1 or global transition flag +0xB36C is set
       - non-special categories set a per-category started flag at owner +0xB334
       - category type 4 can suppress the enable flag when owner +0xA0 bit 0x400000 is clear
       - calls MovieSlotHandle_StartPlayback(1.0f, gManager_802E70A8, slot, enableFlag)
       - for owner +0xB324 category byte 1, also applies movie position/scale/timing
         through MovieSlotHandle_SetPlacementRect and clears the field at +0x278
         through MovieSlotHandle_SetPlaybackFlag278 */
    (void)movieBindings;
    (void)category;
}

void CzanMovieObj_AllocBuffer(int *movieObj, int bufferSize) {
    /* 0x801843CC is named by assert strings in zanMovie.cpp as
       CzanMovieObj::AllocBuffer().

       Confirmed fields:
       movieObj +0x220 -> allocated buffer pointer
       movieObj +0x224 -> allocated buffer size
       movieObj +0x230 -> flags; bit 0 means the movie object is active/allocated

       Original behavior:
       - asserts if flag bit 0 at +0x230 is already set
       - frees an existing buffer at +0x220 when inactive
       - if bufferSize != 0, allocates a 0x20-aligned buffer of bufferSize bytes
         and stores pointer/size at +0x220/+0x224 */
    (void)bufferSize;
    if (movieObj == 0) {
        return;
    }
}

void CzanMovieObj_InitDefaults(int *movieObj) {
    /* 0x80184108 initializes/defaults a CzanMovieObj slot after reset.

       Confirmed behavior:
       - clears flags/state at +0x228..+0x240
       - clears small blocks at +0x244, +0x254, +0x25C, +0x264, +0x26C
       - stores default scalar at +0x274 and default int 1 at +0x278
       - initializes color/config bytes at +0x27C to 0xFF
       - clears +0x280, +0x284, +0x288
       - reads a local config block from FUN_80166D54
       - clamps config floats into ranges and stores them at +0x14C..+0x180
       - copies config words to +0x170, +0x188, +0x18C, +0x190 */
    if (movieObj == 0) {
        return;
    }
}

void CzanMovieObj_Reset(int *movieObj) {
    /* 0x80184678 resets a CzanMovieObj slot record.

       Confirmed behavior:
       - when active and flag bit 1 is set, releases subsystems guarded by
         +0x230 bits 0x40000, 0x10000, and 0x20000
       - clears runtime fields +0x234, +0x238, +0x23C, +0x240
       - clears flag range +0x230 bits masked by 0xFFFFC3FF
       - resets child objects at +0x114, +0x08, and +0xE4
       - frees +0x228 when present
       - unregisters/register-clears +0x114 through manager DAT_802E71B8 +0x268
       - calls CzanMovieObj_InitDefaults(movieObj) for base reset/defaults */
    if (movieObj == 0) {
        return;
    }
}

void CzanMovieObj_LoadResource(int *movieObj, int resourceOrPayload) {
    /* 0x801844E8 performs the same reset as CzanMovieObj_Reset, then binds a new
       resource/payload.

       Confirmed post-reset behavior:
       - registers movieObj +0x114 through DAT_802E71B8 +0x268
       - calls CzanSndRead_Open(movieObj +0x08, resourceOrPayload)
       - sets +0x230 bit 0
       - calls FUN_8019CE38(movieObj +0x08) */
    (void)resourceOrPayload;
    if (movieObj == 0) {
        return;
    }
}

void LargeResourceManager_ReloadFromLink(int *largeResourceManager, int linkData) {
    /* 0x80025CA0 reloads the huge global manager allocated at DAT_802E70BC
       with size 0x3010B8. It initializes a CzanLinkManager-like stack object
       through CzanLinkManager_InitAndSetLink, tears down existing state if largeResourceManager[0]
       is nonzero, loads block 0 into the sub-manager at +0x2FBA7C, initializes
       a common manager at +0x10, initializes seven large banks starting at
       +0x91610 with stride 0x58534, marks the manager active, refreshes state,
       then releases the stack link manager with releaseMode -1.

       Bank setup passes 0x5460 for banks 0..4 and 0 for banks 5..6.
       The listing confirms incoming r4 is passed through as linkData. */
    (void)largeResourceManager;
    (void)linkData;
}

void CharacterAssetManager_UnloadActiveAssets(int *characterAssetManager) {
    /* 0x800CC630 tears down active character assets when
       characterAssetManager +0x28164 == 1.

       Confirmed original flow:
       - clears the active flag at +0x28164
       - releases/clears the manager-local object at +0x28168
       - unloads CzanModelManager bank 4 from gManager_802E70B8
       - clears special/character part state at characterAssetManager +0x34 */
    if (characterAssetManager == 0) {
        return;
    }
    if (*(int *)((unsigned char *)characterAssetManager + 0x28164) == 1) {
        *(int *)((unsigned char *)characterAssetManager + 0x28164) = 0;
    }
}
