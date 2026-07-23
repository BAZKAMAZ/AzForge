/*
 * ForgeApi.h - the interface between AzForge and a mod. Include this one; it pulls in the
 * subsystem headers.
 *
 * A mod is a 32-bit DLL inside an .amod container. The loader resolves two exports from it
 * and passes tables of function pointers: no linking, no import library, no dependency on
 * AzForge.dll. A version mismatch is reported instead of failing at load time with a missing
 * entry point.
 *
 * Rules of the boundary:
 *
 *   - C linkage and __cdecl. No C++ objects, no STL types, no exceptions cross it.
 *   - Strings are UTF-8 and zero terminated. What the loader returns stays valid until
 *     AmodShutdown unless stated otherwise.
 *   - Tables only ever grow at the end, and each carries its structSize.
 *   - Everything a mod registers is released when it is unloaded, in reverse order. A mod
 *     does not write teardown that mirrors its setup.
 *
 * Minimal mod:
 *
 *     #include <ForgeApi.h>
 *
 *     AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
 *     {
 *         if (!Amod::Attach(api))
 *             return AMOD_ERROR_API_VERSION;
 *
 *         Amod::Log(AMOD_LOG_INFO, "%s is up", Amod::ModId());
 *         return AMOD_OK;
 *     }
 *
 *     AMOD_EXPORT void __cdecl AmodShutdown(void) { }
 */

#ifndef AZFORGE_FORGE_API_H
#define AZFORGE_FORGE_API_H

#include "ForgeTypes.h"
#include "LauncherApi.h"
#include "ObjectsMenuApi.h"
#include "BoxMenuApi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodApi
{
    uint32_t structSize;    /* sizeof(AmodApi) as the running loader built it */
    uint32_t apiVersion;    /* AMOD_API_VERSION the running loader implements */
    AmodMod self;           /* the mod this table belongs to */

    /* --- since API version 1 ------------------------------------------------------- */

    /* One line into forge.log. The text is taken as it is; formatting is the caller's. */
    void (__cdecl* Log)(AmodMod mod, AmodLogLevel level, const char* text);

    const char* (__cdecl* GetModId)(AmodMod mod);
    const char* (__cdecl* GetModVersion)(AmodMod mod);
    const char* (__cdecl* GetLoaderVersion)(void);

    /* Directory holding game.exe, UTF-8. */
    const char* (__cdecl* GetGameDirectory)(void);

    /* Load address of game.exe. It has no relocations, so this is 0x00400000 in practice;
     * a mod working with absolute addresses should still add the difference from that
     * rather than assume it. */
    uintptr_t (__cdecl* GetGameModuleBase)(void);

    /* --- since API version 2 ------------------------------------------------------- */

    /* Which layout of game.exe this is, one of AMOD_GAME_BUILD_*. */
    uint32_t (__cdecl* GetGameBuild)(void);

    /* Null on a loader that does not offer the subsystem. */
    const AmodLauncherApi* launcher;

    /* --- since API version 3 ------------------------------------------------------- */

    const AmodObjectsMenuApi* objectsMenu;

    /* --- since API version 5 ------------------------------------------------------- */

    const AmodBoxMenuApi* boxMenu;

    /* Fields added in later API versions go here, and nowhere else. */
} AmodApi;

/* Called once, before the game starts. Anything other than AMOD_OK unloads the mod. The
 * tables stay valid until AmodShutdown returns. */
AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api);

/* Called once, after everything the mod registered has already been released. */
AMOD_EXPORT void __cdecl AmodShutdown(void);

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

    /* Stores the tables after checking the running loader can serve what this mod was built
     * against. False is the mod's cue to return AMOD_ERROR_API_VERSION rather than carry on
     * partially working. */
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
}

#endif /* __cplusplus */

#endif /* AZFORGE_FORGE_API_H */
