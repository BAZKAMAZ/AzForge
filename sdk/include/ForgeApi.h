#ifndef AZFORGE_FORGE_API_H
#define AZFORGE_FORGE_API_H

#include "ForgeTypes.h"
#include "LauncherApi.h"
#include "ObjectsMenuApi.h"
#include "BoxMenuApi.h"
#include "BlockExpressionApi.h"
#include "ConsoleApi.h"
#include "RenderApi.h"
#include "WorldApi.h"
#include "BonusesApi.h"
#include "HudApi.h"
#include "InventoryApi.h"
#include "MonstersApi.h"
#include "MmdApi.h"
#include "PlayerApi.h"
#include "SoundApi.h"
#include "TextureApi.h"
#include "InteropApi.h"
#include "ModManagerApi.h"
#include "InputApi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodApi
{
    uint32_t structSize;    /* sizeof(AmodApi) as the running loader built it */
    uint32_t apiVersion;    /* AMOD_API_VERSION the running loader implements */
    AmodMod self;           /* the mod this table belongs to */

    /* One line into forge.log. The text is taken as it is; formatting is the caller's. */
    void (__cdecl* Log)(AmodMod mod, AmodLogLevel level, const char* text);

    const char* (__cdecl* GetModId)(AmodMod mod);
    const char* (__cdecl* GetModVersion)(AmodMod mod);
    const char* (__cdecl* GetLoaderVersion)(void);

    /* Directory holding game.exe, UTF-8. */
    const char* (__cdecl* GetGameDirectory)(void);

    /* Load address of game.exe, 0x00400000 in practice. */
    uintptr_t (__cdecl* GetGameModuleBase)(void);

    /* Which layout of game.exe this is, one of AMOD_GAME_BUILD_*. */
    uint32_t (__cdecl* GetGameBuild)(void);

    /* Each null on a loader that does not offer the subsystem. */
    const AmodLauncherApi* launcher;
    const AmodObjectsMenuApi* objectsMenu;
    const AmodBoxMenuApi* boxMenu;
    const AmodBlockExpressionApi* blockExpression;
    const AmodConsoleApi* console;
    const AmodRenderApi* render;
    const AmodWorldApi* world;
    const AmodInteropApi* interop;

    /* Null for every mod but core, which is loaded from beside the loader. */
    const AmodModManagerApi* modManager;

    /* The keyboard and the mouse as the game sees them. */
    const AmodInputApi* input;

    /* The bonus table */
    const AmodBonusesApi* bonuses;

    /* What the player is carrying and the chain the scoring is on */
    const AmodInventoryApi* inventory;

    /* The screen the game plays behind. A file replaces it whole; this changes it while it runs. */
    const AmodHudApi* hud;

    /* The monsters a level is filled with */
    const AmodMonstersApi* monsters;

    /* Every 3D model the game has: its triangles, models of the mod's own, and standing in for */
    const AmodMmdApi* mmd;

    /* Every texture the game has, and textures a mod draws itself. */
    const AmodTextureApi* textures;

    // Why the call before this one refused to the last one made
    const char* (__cdecl* LastError)(AmodMod mod);
    int (__cdecl* LastErrorCode)(AmodMod mod);

    /* Every sound the game has, and the loops a room keeps. */
    const AmodSoundApi* sound;

    /* The player himself: where he is, what he is doing, and whether he is alive. */
    const AmodPlayerApi* player;

    /* New subsystems and functions are appended, never inserted. */
} AmodApi;

AMOD_LAST_FIELD(AmodApi, player, 0x74);

/* Called once, before the game starts */
AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api);

/* Called once, after everything the mod registered has already been released. */
AMOD_EXPORT void __cdecl AmodShutdown(void);

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

#include <stdarg.h>
#include <stdio.h>

/* Inline layer, compiled into the mod. Changing it does not affect the ABI. Optional. */
namespace Amod
{
    inline const AmodApi* g_api = nullptr;

    /* Checks the loader can serve what this mod was built against and wires up the subsystem * namespaces */
    inline bool                                                     Attach(const AmodApi* api)
    {
        if (api == nullptr ||
            api->apiVersion < AMOD_API_VERSION ||
            api->structSize < sizeof(AmodApi))
        {
            return false;
        }

        g_api = api;
        Launcher::Attach(api->launcher, api->self);
        Menu::Attach(api->objectsMenu, api->self);
        Box::Attach(api->boxMenu, api->self);
        Data::Attach(api->blockExpression, api->self);
        Console::Attach(api->console, api->self);
        Render::Attach(api->render, api->self);
        World::Attach(api->world, api->self);
        Bonuses::Attach(api->bonuses, api->self);
        Inventory::Attach(api->inventory, api->self);
        Hud::Attach(api->hud, api->self);
        Monsters::Attach(api->monsters, api->self);
        Mmd::Attach(api->mmd, api->self);
        Textures::Attach(api->textures, api->self);
        Sound::Attach(api->sound, api->self);
        Player::Attach(api->player, api->self);
        Interop::Attach(api->interop, api->self);
        Mods::Attach(api->modManager, api->self);
        Input::Attach(api->input, api->self);
        return true;
    }

    inline bool                                                     IsAttached()
    {
        return g_api != nullptr;
    }

    inline void                                                     LogText(AmodLogLevel level, const char* text)
    {
        if (g_api != nullptr)
        {
            g_api->Log(g_api->self, level, text);
        }
    }

    inline void                                                     Log(AmodLogLevel level, const char* format, ...)
    {
        if (g_api == nullptr)
        {
            return;
        }

        char message[1024];
        va_list arguments;
        va_start(arguments, format);
        const int produced = vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);

        if (produced >= 0)
        {
            g_api->Log(g_api->self, level, message);
        }
    }

    inline const char*                                              ModId()
    {
        return g_api != nullptr ? g_api->GetModId(g_api->self) : "";
    }

    inline const char*                                              ModVersion()
    {
        return g_api != nullptr ? g_api->GetModVersion(g_api->self) : "";
    }

    inline const char*                                              LoaderVersion()
    {
        return g_api != nullptr ? g_api->GetLoaderVersion() : "";
    }

    inline const char*                                              GameDirectory()
    {
        return g_api != nullptr ? g_api->GetGameDirectory() : "";
    }

    inline uintptr_t                                                GameModuleBase()
    {
        return g_api != nullptr ? g_api->GetGameModuleBase() : 0;
    }

    inline uint32_t                                                 GameBuild()
    {
        return g_api != nullptr ? g_api->GetGameBuild() : AMOD_GAME_BUILD_UNKNOWN;
    }

    // Worth asking straight after a refusal: the next call, whatever it is, clears it.
    inline const char*                                              LastError()
    {
        return (g_api != nullptr && g_api->LastError != nullptr) ? g_api->LastError(g_api->self) : "";
    }

    inline int                                                      LastErrorCode()
    {
        return (g_api != nullptr && g_api->LastErrorCode != nullptr) ? g_api->LastErrorCode(g_api->self) : AMOD_FAIL_NONE;
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_FORGE_API_H */
