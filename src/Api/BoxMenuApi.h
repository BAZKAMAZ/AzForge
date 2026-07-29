#pragma once

#include <BoxMenuApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the menus as a whole.
    //
    // It hooks nothing: it calls the manager's own navigation routines and builds a menu the
    // way Menu_Register does, minus the parser. A menu a mod registers is recorded against
    // that mod, because the game's registry has no removal of its own and a menu left in it
    // would outlive the mod that put it there.
    class BoxMenuApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        // Retires the menus this mod registered: disabled so nothing can navigate into them,
        // and unlinked from the registry so the id is free again.
        static void                                                 ForgetMod(ModContext* owner);

        static const AmodBoxMenuApi*                                Table();
    };
}
