/*
 * ForgeApi.h - the interface between AzForge and a mod. Include this one; it pulls in the
 * subsystem headers.
 *
 * A mod is a 32-bit DLL in an .amod container. The loader resolves its exports and passes
 * tables of function pointers - no linking against AzForge.dll. The boundary is C linkage and
 * __cdecl, UTF-8 strings, tables that only grow at the end and carry their structSize, and
 * registrations that are released automatically when the mod unloads.
 */

#ifndef AZFORGE_FORGE_API_H
#define AZFORGE_FORGE_API_H

#include "ForgeTypes.h"
#include "LauncherApi.h"
#include "ObjectsMenuApi.h"
#include "BoxMenuApi.h"
#include "BlockExpressionApi.h"
#include "ConsoleApi.h"
#include "RenderApi.h"

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

    /* New subsystems and functions are appended, never inserted. */
} AmodApi;

/* Called once, before the game starts. Anything other than AMOD_OK unloads the mod. The
 * tables stay valid until AmodShutdown returns. */
AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api);

/* Called once, after everything the mod registered has already been released. */
AMOD_EXPORT void __cdecl AmodShutdown(void);

/* Optional. Exported by a mod that wants to run every frame - the game's frame, in every
 * state, menu and gameplay alike - after the game has advanced the world. A mod that does
 * not export it simply gets no frame ticks. The pointer is valid only for the call. */
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

    /* Checks the loader can serve what this mod was built against and wires up the subsystem
     * namespaces. False means the mod should return AMOD_ERROR_API_VERSION. */
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
