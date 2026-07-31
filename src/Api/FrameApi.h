#pragma once

#include <ForgeApi.h>

namespace Forge
{
    struct ModContext;

    typedef void(__cdecl* AmodUpdateRoutine)(const AmodFrame* frame);

    class FrameApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        // A loaded mod's AmodUpdate export. Ignored when update is null.
        static void                                                 Register(ModContext* owner, AmodUpdateRoutine update);
        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 AddInternalTick(void(__cdecl* tick)(), bool afterMods = false);
    };
}
