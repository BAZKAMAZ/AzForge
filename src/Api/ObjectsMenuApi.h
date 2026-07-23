#pragma once

#include <ObjectsMenuApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the in-game menu interface.
    //
    // Three detours carry it. Menu_ParseScript @0x004527a0 gives the menu object and the
    // moment its elements exist; Menu_LoadFromScript @0x0043c090 wraps it and is the only
    // place that knows the $menu_id, which the game stamps into the object only after the
    // parse has returned; MenuItem_Parse @0x0045f550 says which elements are $item blocks,
    // and therefore which of them own the text field at +0x758.
    //
    // Every element of every parsed menu is recorded, so a handle a mod holds is checked
    // before it is followed: rebuilding a menu frees its elements, and a stale handle must
    // be refused rather than written through.
    class ObjectsMenuApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodObjectsMenuApi*                            Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
