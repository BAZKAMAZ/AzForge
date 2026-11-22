#ifndef AZFORGE_STAGE_API_H
#define AZFORGE_STAGE_API_H

#include "ForgeTypes.h"
#include "RenderApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stage flags. */
#define AMOD_STAGE_UPDATE 0x01
#define AMOD_STAGE_RENDER 0x02
#define AMOD_STAGE_SHELL 0x04
#define AMOD_STAGE_LOAD 0x08
#define AMOD_STAGE_BOTH 0x03
#define AMOD_STAGE_ALL 0x0F

/* World update callback. */
typedef void (__cdecl* AmodStageUpdate)(float seconds, void* user);

/* World render callback. */
typedef void (__cdecl* AmodStageRender)(void* user);

/* Game shell callback; returns input swallowed state. */
typedef int (__cdecl* AmodStageShell)(void* user);

/* Game data-load callback; returns whether loading may continue. */
typedef int (__cdecl* AmodStageLoad)(void* user);

typedef struct AmodStageApi
{
    uint32_t structSize;

    /* Takes the world update or render. */
    int (__cdecl* TakeUpdate)(AmodMod mod, AmodStageUpdate update, void* user);
    int (__cdecl* TakeRender)(AmodMod mod, AmodStageRender render, void* user);

    /* Gives back held stages. */
    int (__cdecl* GiveBack)(AmodMod mod, int what);

    /* Checks held stages. */
    int (__cdecl* IsTaken)(AmodMod mod, int what);

    /* Runs the game's world update or render. */
    void (__cdecl* RunGameUpdate)(AmodMod mod, float seconds);
    void (__cdecl* RunGameRender)(AmodMod mod);

    /* Clears color and depth. */
    void (__cdecl* Clear)(AmodMod mod, uint32_t color);

    /* Sets the world camera. */
    int (__cdecl* SetCamera)(AmodMod mod, const float eye[3], const float at[3], const float up[3], float fovDegrees, float nearZ, float farZ);

    /* Takes keyboard and mouse input. */
    int (__cdecl* TakeInput)(AmodMod mod, int take);

    /* Takes or runs the game shell. */
    int (__cdecl* TakeShell)(AmodMod mod, AmodStageShell shell, void* user);
    int (__cdecl* RunGameShell)(AmodMod mod);

    /* Takes or runs the game data-load stage. */
    int (__cdecl* TakeLoad)(AmodMod mod, AmodStageLoad load, void* user);
    int (__cdecl* RunGameLoad)(AmodMod mod);

    /* Runs the game's music update. */
    void (__cdecl* RunMusic)(AmodMod mod);

    /* THE LOADING SCREEN A MOD IS BEING STARTED ON. Nothing is drawn while a mod sits in AmodInit:
       the loader repaints between mods, and inside one there is only whatever was on the screen
       last - which at startup is nothing at all, and looks like a hung white window. A mod that
       reads files in its init should say so as it goes, and each of these paints the screen there
       and then.

       Loading tells whether there is a screen to paint. Off it these do nothing and say so, so the
       same lines are safe to leave in wherever they are called from. */
    int (__cdecl* Loading)(AmodMod mod);
    int (__cdecl* LoadingSay)(AmodMod mod, const char* text);
    int (__cdecl* LoadingStep)(AmodMod mod, int percent, const char* text);

    /* A WHOLE FRAME OF THE MOD'S OWN, drawn while it is still starting. The two above write on the
       game's loading screen; this one hands the screen over. The loader opens the frame, calls
       back, and shows it - and inside the callback everything the mod draws with normally works,
       so the screen it puts up during its own loading can be the screen it wants. Call it as often
       as there is something new to show. */
    int (__cdecl* LoadingFrame)(AmodMod mod, AmodRenderCallback draw, void* user);

} AmodStageApi;

AMOD_LAST_FIELD(AmodStageApi, LoadingFrame, 0x48);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Stage
    {
        inline const AmodStageApi* g_stage = nullptr;
        inline AmodMod g_stageSelf = nullptr;

        inline void Attach(const AmodStageApi* api, AmodMod self)
        {
            g_stage = api;
            g_stageSelf = self;
        }

        inline bool IsAvailable()
        {
            return g_stage != nullptr;
        }

        inline bool TakeUpdate(AmodStageUpdate update, void* user = nullptr)
        {
            return g_stage != nullptr && g_stage->TakeUpdate(g_stageSelf, update, user) != 0;
        }

        inline bool TakeRender(AmodStageRender render, void* user = nullptr)
        {
            return g_stage != nullptr && g_stage->TakeRender(g_stageSelf, render, user) != 0;
        }

        inline bool GiveBack(int what = AMOD_STAGE_BOTH)
        {
            return g_stage != nullptr && g_stage->GiveBack(g_stageSelf, what) != 0;
        }

        inline bool IsTaken(int what = AMOD_STAGE_BOTH)
        {
            return g_stage != nullptr && g_stage->IsTaken(g_stageSelf, what) != 0;
        }

        inline void RunGameUpdate(float seconds)
        {
            if (g_stage != nullptr)
            {
                g_stage->RunGameUpdate(g_stageSelf, seconds);
            }
        }

        inline void RunGameRender()
        {
            if (g_stage != nullptr)
            {
                g_stage->RunGameRender(g_stageSelf);
            }
        }

        inline void Clear(unsigned long color = 0xFF000000)
        {
            if (g_stage != nullptr)
            {
                g_stage->Clear(g_stageSelf, static_cast<uint32_t>(color));
            }
        }

        inline bool SetCamera(const float eye[3], const float at[3], const float up[3], float fovDegrees = 60.0f, float nearZ = 0.1f, float farZ = 100.0f)
        {
            return g_stage != nullptr && g_stage->SetCamera(g_stageSelf, eye, at, up, fovDegrees, nearZ, farZ) != 0;
        }

        inline bool TakeShell(AmodStageShell shell, void* user = nullptr)
        {
            return g_stage != nullptr && g_stage->TakeShell(g_stageSelf, shell, user) != 0;
        }

        inline int RunGameShell()
        {
            return g_stage != nullptr ? g_stage->RunGameShell(g_stageSelf) : 0;
        }

        inline bool TakeLoad(AmodStageLoad load, void* user = nullptr)
        {
            return g_stage != nullptr && g_stage->TakeLoad(g_stageSelf, load, user) != 0;
        }

        inline int RunGameLoad()
        {
            return g_stage != nullptr ? g_stage->RunGameLoad(g_stageSelf) : 0;
        }

        inline bool Loading()
        {
            return g_stage != nullptr && g_stage->structSize >= sizeof(AmodStageApi) &&
                   g_stage->Loading(g_stageSelf) != 0;
        }

        inline bool LoadingSay(const char* text)
        {
            return g_stage != nullptr && g_stage->structSize >= sizeof(AmodStageApi) &&
                   g_stage->LoadingSay(g_stageSelf, text) != 0;
        }

        inline bool LoadingFrame(AmodRenderCallback draw, void* user = nullptr)
        {
            return g_stage != nullptr && g_stage->structSize >= sizeof(AmodStageApi) &&
                   g_stage->LoadingFrame(g_stageSelf, draw, user) != 0;
        }

        inline bool LoadingStep(int percent, const char* text)
        {
            return g_stage != nullptr && g_stage->structSize >= sizeof(AmodStageApi) &&
                   g_stage->LoadingStep(g_stageSelf, percent, text) != 0;
        }

        inline void RunMusic()
        {
            if (g_stage != nullptr)
            {
                g_stage->RunMusic(g_stageSelf);
            }
        }

        inline bool TakeInput(bool take = true)
        {
            return g_stage != nullptr && g_stage->TakeInput(g_stageSelf, take ? 1 : 0) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_STAGE_API_H */