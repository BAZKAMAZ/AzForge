#pragma once

#include <InteropApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the direct interface.
    //
    // It installs nothing of its own. It lends mods the three things the loader is built on -
    // Signature, InlineHook and the module's executable sections - so that a capability the
    // loader has never heard of does not have to be added to the loader first.
    //
    // Hooks a mod installs are held here rather than by the mod, because a hook has to be
    // taken out of the game's code before the mod's DLL is unmapped: a detour left pointing
    // into a module that is gone is the one failure this must never allow.
    class InteropApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodInteropApi*                                Table();

        // Removes every hook this mod installed. Runs before the mod's DLL is unmapped.
        static void                                                 ForgetMod(ModContext* owner);
    };
}
