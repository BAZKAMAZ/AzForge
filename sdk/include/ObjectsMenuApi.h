/*
 * ObjectsMenuApi.h - the in-game menus.
 *
 * Every menu is built from a menu_*.txt script: a $header carrying the $menu_id, blocks of
 * decorative $images and $static_text, and a $menu_items block of interactive elements.
 * Interactive elements carry a mandatory $id - the game refuses to build one without it -
 * which is what this interface addresses them by.
 *
 * Only those interactive elements are reachable here. Decorative entries carry no identity
 * at all and are not even kept alongside the rest, so a mod that wants to rearrange the
 * artwork of a menu still does it by replacing the script.
 *
 * A mod that only wants a different arrangement can replace the script through the resource
 * layer and write no code. What this interface adds is composition: two mods replacing the
 * same menu_main.txt conflict and the last one installed wins, while two mods moving
 * different elements of the same menu simply both work.
 *
 * Reached through AmodApi::objectsMenu, which is null on a loader older than API version 3.
 */

#ifndef AZFORGE_OBJECTS_MENU_API_H
#define AZFORGE_OBJECTS_MENU_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One element of a menu. Opaque: it addresses a game object whose layout belongs to a
 * particular build, so it must not be dereferenced. GetRawPointer hands over the address
 * for a mod that accepts that coupling. A handle stays valid until its menu is rebuilt. */
typedef void* AmodMenuItem;

typedef enum AmodMenuEvent
{
    /* A menu has been built from its script and is ready to be adjusted. Event data is
     * AmodMenuLoaded. This arrives again whenever the game rebuilds the menu, so changes
     * made here last exactly as long as the game's own script settings do. */
    AMOD_MENU_LOADED = 0
} AmodMenuEvent;

typedef struct AmodMenuLoaded
{
    const char* menuId;     /* $menu_id from the script header, such as "IDM_MAIN_MENU" */
} AmodMenuLoaded;

/* The rectangle an element occupies, in the menu's own 640x480 coordinate space. */
typedef struct AmodMenuRect
{
    float left;
    float bottom;
    float right;
    float top;
} AmodMenuRect;

typedef void (__cdecl* AmodMenuCallback)(AmodMenuEvent event, void* data, void* user);

typedef struct AmodObjectsMenuApi
{
    uint32_t structSize;

    /* --- since API version 3 ------------------------------------------------------- */

    AmodToken (__cdecl* Subscribe)(AmodMod mod, AmodMenuEvent event,
                                   AmodMenuCallback callback, void* user);
    void (__cdecl* Unsubscribe)(AmodMod mod, AmodToken token);

    /* Resolves an interactive element by the $id its script block declares. Returns null
     * when the menu has not been built yet or holds no such element. */
    AmodMenuItem (__cdecl* FindItem)(AmodMod mod, const char* menuId, const char* itemId);

    /* Walks the interactive elements of a menu in the order the script declares them, for a
     * mod that would rather pick by what it finds than name an id.
     *
     * Decorative entries are not part of this: the groups a menu keeps hold only the
     * elements of its $menu_items, and the $images and $static_text blocks live elsewhere.
     * Reaching those is not offered yet rather than offered and wrong. */
    int (__cdecl* GetItemCount)(AmodMod mod, const char* menuId);
    AmodMenuItem (__cdecl* GetItemAt)(AmodMod mod, const char* menuId, int index);

    /* The $id the element's script block declares. */
    const char* (__cdecl* GetItemId)(AmodMod mod, AmodMenuItem item);

    /* Position and rectangle are part of what every element carries, so these work for any
     * handle. All of them answer non-zero on success.
     *
     * Moving an element carries its rectangle along, because the rectangle is what the game
     * draws from: it was worked out from the position and the alignment while the script was
     * being read, and nothing recomputes it afterwards. Setting the rectangle instead leaves
     * the position as the script declared it. */
    int (__cdecl* GetPosition)(AmodMod mod, AmodMenuItem item, float* x, float* y);
    int (__cdecl* SetPosition)(AmodMod mod, AmodMenuItem item, float x, float y);
    int (__cdecl* GetBounds)(AmodMod mod, AmodMenuItem item, AmodMenuRect* bounds);
    int (__cdecl* SetBounds)(AmodMod mod, AmodMenuItem item, const AmodMenuRect* bounds);
    int (__cdecl* IsVisible)(AmodMod mod, AmodMenuItem item);
    int (__cdecl* SetVisible)(AmodMod mod, AmodMenuItem item, int visible);

    /* Text lives in a field only elements built as $item have, so writing it to anything
     * else would land in whatever that other kind of element keeps at the same offset.
     * The loader records which elements the game parsed as items and refuses the rest;
     * IsTextItem answers in advance. */
    int (__cdecl* IsTextItem)(AmodMod mod, AmodMenuItem item);
    int (__cdecl* GetText)(AmodMod mod, AmodMenuItem item, char* buffer, uint32_t size);
    int (__cdecl* SetText)(AmodMod mod, AmodMenuItem item, const char* text);

    /* The element's address, for a mod that works with the layout directly. Returns null
     * unless expectedGameBuild matches what AmodApi::GetGameBuild reports. */
    void* (__cdecl* GetRawPointer)(AmodMod mod, AmodMenuItem item, uint32_t expectedGameBuild);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodObjectsMenuApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Menu
    {
        inline const AmodObjectsMenuApi* g_menu = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodObjectsMenuApi* api, AmodMod self)
        {
            g_menu = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_menu != nullptr;
        }

        inline AmodToken                                            Subscribe(AmodMenuEvent event, AmodMenuCallback callback, void* user = nullptr)
        {
            return g_menu != nullptr ? g_menu->Subscribe(g_self, event, callback, user)
                                     : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Unsubscribe(AmodToken token)
        {
            if (g_menu != nullptr)
            {
                g_menu->Unsubscribe(g_self, token);
            }
        }

        inline AmodMenuItem                                         Find(const char* menuId, const char* itemId)
        {
            return g_menu != nullptr ? g_menu->FindItem(g_self, menuId, itemId) : nullptr;
        }

        inline int                                                  Count(const char* menuId)
        {
            return g_menu != nullptr ? g_menu->GetItemCount(g_self, menuId) : 0;
        }

        inline AmodMenuItem                                         At(const char* menuId, int index)
        {
            return g_menu != nullptr ? g_menu->GetItemAt(g_self, menuId, index) : nullptr;
        }

        inline const char*                                          Id(AmodMenuItem item)
        {
            return g_menu != nullptr ? g_menu->GetItemId(g_self, item) : "";
        }

        inline bool                                                 GetPosition(AmodMenuItem item, float* x, float* y)
        {
            return g_menu != nullptr && g_menu->GetPosition(g_self, item, x, y) != 0;
        }

        inline bool                                                 SetPosition(AmodMenuItem item, float x, float y)
        {
            return g_menu != nullptr && g_menu->SetPosition(g_self, item, x, y) != 0;
        }

        inline bool                                                 GetBounds(AmodMenuItem item, AmodMenuRect* bounds)
        {
            return g_menu != nullptr && g_menu->GetBounds(g_self, item, bounds) != 0;
        }

        inline bool                                                 SetBounds(AmodMenuItem item, const AmodMenuRect* bounds)
        {
            return g_menu != nullptr && g_menu->SetBounds(g_self, item, bounds) != 0;
        }

        inline bool                                                 IsVisible(AmodMenuItem item)
        {
            return g_menu != nullptr && g_menu->IsVisible(g_self, item) != 0;
        }

        inline bool                                                 SetVisible(AmodMenuItem item, bool visible)
        {
            return g_menu != nullptr && g_menu->SetVisible(g_self, item, visible ? 1 : 0) != 0;
        }

        inline bool                                                 IsTextItem(AmodMenuItem item)
        {
            return g_menu != nullptr && g_menu->IsTextItem(g_self, item) != 0;
        }

        inline bool                                                 GetText(AmodMenuItem item, char* buffer, uint32_t size)
        {
            return g_menu != nullptr && g_menu->GetText(g_self, item, buffer, size) != 0;
        }

        inline bool                                                 SetText(AmodMenuItem item, const char* text)
        {
            return g_menu != nullptr && g_menu->SetText(g_self, item, text) != 0;
        }

        inline void*                                                Raw(AmodMenuItem item, uint32_t expectedGameBuild = AMOD_GAME_BUILD_AZANGARA_10)
        {
            return g_menu != nullptr ? g_menu->GetRawPointer(g_self, item, expectedGameBuild) : nullptr;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_OBJECTS_MENU_API_H */
