/*
 * BoxMenuApi.h - the menus as a whole, and moving between them.
 *
 * ObjectsMenuApi reaches inside one menu; this reaches across them: register a menu of your
 * own for $sub_menu buttons to reach, ask which is on screen, and drive the navigation.
 * Reached through AmodApi::boxMenu.
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

    /* Registers an empty menu under this id, for $sub_menu buttons to reach and for
     * ObjectsMenu::Add to fill. Non-zero on success; zero if the id is already taken or the
     * menu system is not up. The menu persists for the life of the process. */
    int (__cdecl* Register)(AmodMod mod, const char* menuId);

    /* Registers a menu built from a script file, the way the game builds its own. The file
     * is opened through the game's resource system, so a script a mod ships under files/
     * is found there without it having to exist on disk. Non-zero on success; zero if the
     * id is taken, the script is missing or the menu system is not up.
     *
     * This is what makes a screen's layout a file rather than code: the same script another
     * menu would have been written in, restyled without rebuilding the mod. */
    int (__cdecl* RegisterFromScript)(AmodMod mod, const char* menuId, const char* scriptFile);

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

        inline bool                                                 RegisterFromScript(const char* menuId, const char* scriptFile)
        {
            return g_box != nullptr && g_box->RegisterFromScript(g_self, menuId, scriptFile) != 0;
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
