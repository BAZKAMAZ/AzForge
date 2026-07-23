/*
 * ForgeApi.h - the interface between AzForge and a mod. Include this one; it pulls in the
 * subsystem headers.
 *
 * A mod is a 32-bit DLL inside an .amod container. The loader resolves two exports from it
 * and passes a table of function pointers; the mod never links against AzForge, never
 * imports from it, and never needs an import library. That is deliberate: the only thing
 * shared between the two is the layout of these headers, so a mod keeps working when the
 * loader is rebuilt, and a version mismatch is reported instead of failing at load time
 * with a missing entry point.
 *
 * Rules of the boundary, all of which exist because the two sides are built separately:
 *
 *   - C linkage and __cdecl everywhere. Nothing here depends on a C++ runtime.
 *   - No C++ objects, no STL types and no exceptions cross the boundary.
 *   - Strings are UTF-8, zero terminated, owned by whoever produced them. Pointers the
 *     loader returns stay valid until AmodShutdown unless stated otherwise.
 *   - Fields are only ever appended to a table, never reordered or removed. structSize
 *     tells a mod how much of it the running loader actually filled in.
 *
 * Each subsystem is a table of its own, reached through a pointer in AmodApi and declared
 * in its own header, so the core table grows by one pointer per subsystem rather than by
 * every function the subsystem offers.
 *
 * Ownership: anything a mod creates through this API is recorded against the mod that
 * created it, and released in reverse order when the mod is unloaded. A mod does not have
 * to undo its own registrations, and cannot leave something behind that outlives it.
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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodApi
{
    /* sizeof(AmodApi) as the running loader built it. Check this before reading a field
     * that was added after the version you built against. */
    uint32_t structSize;

    /* AMOD_API_VERSION the running loader implements. */
    uint32_t apiVersion;

    /* The mod this table belongs to. */
    AmodMod self;

    /* --- since API version 1 ------------------------------------------------------- */

    /* Writes one line to forge.log. The text is taken as it is; formatting belongs to the
     * caller. */
    void (__cdecl* Log)(AmodMod mod, AmodLogLevel level, const char* text);

    /* Identity of the mod, as declared in its manifest. */
    const char* (__cdecl* GetModId)(AmodMod mod);
    const char* (__cdecl* GetModVersion)(AmodMod mod);

    /* Version string of the loader itself, for a mod that wants to report it. */
    const char* (__cdecl* GetLoaderVersion)(void);

    /* Directory that holds game.exe, UTF-8. */
    const char* (__cdecl* GetGameDirectory)(void);

    /* Load address of game.exe. The executable has no relocations, so this is 0x00400000
     * in practice; a mod that works with absolute addresses should still add the
     * difference from 0x00400000 rather than assume it. */
    uintptr_t (__cdecl* GetGameModuleBase)(void);

    /* --- since API version 2 ------------------------------------------------------- */

    /* Which layout of game.exe the loader is running on, one of AMOD_GAME_BUILD_*. */
    uint32_t (__cdecl* GetGameBuild)(void);

    /* The start-up launcher. Null on a loader that does not offer it. */
    const AmodLauncherApi* launcher;

    /* --- since API version 3 ------------------------------------------------------- */

    /* The in-game menus. Null on a loader that does not offer them. */
    const AmodObjectsMenuApi* objectsMenu;

    /* Fields added in later API versions go here, and nowhere else. */
} AmodApi;

/* Called once, before the game starts. Returning anything other than AMOD_OK unloads the
 * mod. The tables stay valid until AmodShutdown returns. */
AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api);

/* Called once, after everything the mod registered has already been released. */
AMOD_EXPORT void __cdecl AmodShutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

#include <stdarg.h>
#include <stdio.h>

/*
 * Convenience layer. Nothing here crosses the boundary: it is inline code compiled into the
 * mod that calls through the tables above, so it can be changed freely without affecting
 * the ABI. Using it is optional.
 */
namespace Amod
{
    inline const AmodApi* g_api = nullptr;

    /* Stores the tables after checking that the running loader can serve what this mod was
     * built against. Returns false when it cannot, which is the mod's cue to return
     * AMOD_ERROR_API_VERSION from AmodInit rather than to carry on partially working. */
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
