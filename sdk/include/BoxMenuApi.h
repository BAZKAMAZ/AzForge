/*
 * BoxMenuApi.h - the menus as a whole, and moving between them.
 *
 * ObjectsMenuApi reaches inside one menu; this reaches across them. A menu is addressed by
 * its $menu_id, and every button that carries $sub_menu already navigates to one. Here a mod
 * can register a menu of its own for those buttons to reach, ask which menu is on screen, and
 * drive the navigation itself.
 *
 * The manager keeps a current menu, an optional box overlay layered over it, and a stack of
 * the menus stepped through to get here - the same stack the game's ID_BACK button pops.
 *
 * Reached through AmodApi::boxMenu, null on a loader older than API version 5.
 */

#ifndef AZFORGE_BOX_MENU_API_H
#define AZFORGE_BOX_MENU_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodBoxMenuApi
{
    uint32_t structSize;

    /* --- since API version 5 ------------------------------------------------------- */

    /* Registers an empty menu under this id, for $sub_menu buttons to reach and for
     * ObjectsMenu::Add to fill. Non-zero on success; zero if the id is already taken or the
     * menu system is not up. The menu persists for the life of the process. */
    int (__cdecl* Register)(AmodMod mod, const char* menuId);

    /* Whether a menu with this id exists, whether the game's or a mod's. */
    int (__cdecl* Exists)(AmodMod mod, const char* menuId);

    /* The $menu_id of the menu on screen, and of the overlay over it, or "" for none. The
     * pointers stay valid until the menu changes. */
    const char* (__cdecl* Current)(AmodMod mod);
    const char* (__cdecl* Box)(AmodMod mod);

    /* Steps into a menu, remembering the current one so Back returns to it - the same thing a
     * $sub_menu button does. Non-zero when the menu exists. */
    int (__cdecl* Open)(AmodMod mod, const char* menuId);

    /* Steps back one level: closes the overlay if one is up, otherwise pops the stack. The
     * same thing an ID_BACK button does. */
    void (__cdecl* Back)(AmodMod mod);

    /* Lays a menu over the current one as an overlay rather than replacing it, as a $box_menu
     * button does. Non-zero when the menu exists. */
    int (__cdecl* ShowBox)(AmodMod mod, const char* menuId);

    /* Closes the overlay, the current menu and the whole stack, as ID_CLOSE_MENU does. */
    void (__cdecl* CloseAll)(AmodMod mod);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodBoxMenuApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

/*
 * Inline layer, compiled into the mod:
 *
 *     if (!Amod::Box::Exists("IDM_MODS"))
 *     {
 *         Amod::Box::Register("IDM_MODS");
 *         Amod::Menu::Add("IDM_MODS", R"( $menu_items { ... } )");
 *     }
 *
 *     if (Amod::Box::IsCurrent("IDM_MAIN_MENU"))
 *         Amod::Box::Open("IDM_MODS");
 */
namespace Amod
{
    namespace Box
    {
        inline const AmodBoxMenuApi* g_box = nullptr;
        inline AmodMod g_self = nullptr;

        inline bool                                                 StrEqual(const char* a, const char* b)
        {
            if (a == nullptr || b == nullptr)
            {
                return false;
            }
            for (; *a != '\0' && *a == *b; ++a, ++b)
            {
            }
            return *a == *b;
        }

        inline void                                                 Attach(const AmodBoxMenuApi* api, AmodMod self)
        {
            g_box = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_box != nullptr;
        }

        inline bool                                                 Register(const char* menuId)
        {
            return g_box != nullptr && g_box->Register(g_self, menuId) != 0;
        }

        inline bool                                                 Exists(const char* menuId)
        {
            return g_box != nullptr && g_box->Exists(g_self, menuId) != 0;
        }

        inline const char*                                         Current()
        {
            return g_box != nullptr ? g_box->Current(g_self) : "";
        }

        inline const char*                                         BoxMenu()
        {
            return g_box != nullptr ? g_box->Box(g_self) : "";
        }

        inline bool                                                IsCurrent(const char* menuId)
        {
            const char* const current = Current();
            return menuId != nullptr && current[0] != '\0' && StrEqual(current, menuId);
        }

        inline bool                                                Open(const char* menuId)
        {
            return g_box != nullptr && g_box->Open(g_self, menuId) != 0;
        }

        inline void                                                Back()
        {
            if (g_box != nullptr)
            {
                g_box->Back(g_self);
            }
        }

        inline bool                                                ShowBox(const char* menuId)
        {
            return g_box != nullptr && g_box->ShowBox(g_self, menuId) != 0;
        }

        inline void                                                CloseAll()
        {
            if (g_box != nullptr)
            {
                g_box->CloseAll(g_self);
            }
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_BOX_MENU_API_H */
