#ifndef AZFORGE_BOX_MENU_API_H
#define AZFORGE_BOX_MENU_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodBoxMenuApi
{
    uint32_t structSize;

    /* Empty menu for $sub_menu / ObjectsMenu::Add. */
    int (__cdecl* Register)(AmodMod mod, const char* menuId);

    /* Menu from a script file (resource system; files/ works). */
    int (__cdecl* RegisterFromScript)(AmodMod mod, const char* menuId, const char* scriptFile);

    int (__cdecl* Exists)(AmodMod mod, const char* menuId);

    /* $menu_id of the screen / overlay, or "". */
    const char* (__cdecl* Current)(AmodMod mod);
    const char* (__cdecl* Box)(AmodMod mod);

    int (__cdecl* Open)(AmodMod mod, const char* menuId);       /* push, like $sub_menu */
    void (__cdecl* Back)(AmodMod mod);
    int (__cdecl* ShowBox)(AmodMod mod, const char* menuId);    /* overlay, like $box_menu */
    void (__cdecl* CloseAll)(AmodMod mod);
} AmodBoxMenuApi;

AMOD_LAST_FIELD(AmodBoxMenuApi, CloseAll, 0x24);

#ifdef __cplusplus
}
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
