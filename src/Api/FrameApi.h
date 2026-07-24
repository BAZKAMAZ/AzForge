#pragma once

#include <ForgeApi.h>

namespace Forge
{
    struct ModContext;

    typedef void(__cdecl* AmodUpdateRoutine)(const AmodFrame* frame);

    // Loader side of the per-frame update.
    //
    // A mod that exports AmodUpdate is registered here; a single detour on Game_FrameTick
    // then calls every registered mod once per frame, with the delta the game itself worked
    // out. Unlike the launcher and menu interfaces there is no table a mod calls - AmodUpdate
    // is an export the loader resolves and drives, the way it does AmodInit and AmodShutdown.
    class FrameApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        // A loaded mod's AmodUpdate export. Ignored when update is null.
        static void                                                 Register(ModContext* owner, AmodUpdateRoutine update);
        static void                                                 ForgetMod(ModContext* owner);
    };
}
