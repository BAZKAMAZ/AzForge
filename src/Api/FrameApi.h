#pragma once

#include <ForgeApi.h>

namespace Forge
{
    struct ModContext;

    typedef void(__cdecl* AmodUpdateRoutine)(const AmodFrame* frame);

    // Loader side of the per-frame update. One detour on Game_FrameTick calls each mod's
    // AmodUpdate export once a frame; there is no table a mod calls.
    class FrameApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        // A loaded mod's AmodUpdate export. Ignored when update is null.
        static void                                                 Register(ModContext* owner, AmodUpdateRoutine update);
        static void                                                 ForgetMod(ModContext* owner);

        // A loader subsystem's per-frame nudge, to notice state that only exists once the
        // game runs (the console becoming ready). Called every frame, before the mods.
        static void                                                 AddInternalTick(void(__cdecl* tick)());
    };
}
