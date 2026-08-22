#ifndef AZFORGE_STAGE_API_H
#define AZFORGE_STAGE_API_H

#include "ForgeTypes.h"

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

} AmodStageApi;

AMOD_LAST_FIELD(AmodStageApi, RunMusic, 0x38);

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