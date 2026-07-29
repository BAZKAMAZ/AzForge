/*
 * ObjectsMenuApi.h - the in-game menus.
 */

#ifndef AZFORGE_OBJECTS_MENU_API_H
#define AZFORGE_OBJECTS_MENU_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An element. Opaque, and valid until its menu is rebuilt. */
typedef void* AmodMenuItem;

typedef enum AmodMenuEvent
{
    /* A menu has been built and is ready to be changed. Data is AmodMenuLoaded. Arrives
     * again every time the game rebuilds the menu.
     *
     * Subscribing late does not mean missing it: every menu that already exists is
     * announced to the new subscriber as it subscribes. Mods are loaded on the loading
     * screen, by which time the game has built all of its own menus, so without that a
     * mod would never hear about any of them. */
    AMOD_MENU_LOADED = 0,

    /* A button was activated, before the game acts on it. Data is AmodMenuClicked. This is
     * how a mod's own buttons come to do something, and how it can take over one of the
     * game's. */
    AMOD_MENU_CLICKED = 1
} AmodMenuEvent;

typedef struct AmodMenuLoaded
{
    const char* menuId;     /* $menu_id from the script header, "IDM_MAIN_MENU" and so on */
} AmodMenuLoaded;

typedef struct AmodMenuClicked
{
    const char* menuId;     /* the menu the click was in */
    const char* itemId;     /* the $id of the button that was activated */
    int handled;            /* set non-zero to stop the game acting on the click */
} AmodMenuClicked;

typedef void (__cdecl* AmodMenuCallback)(AmodMenuEvent event, void* data, void* user);

/* The keys each kind of element carries are documented on the site. A key a kind does not
 * carry is refused rather than written to an unrelated field; GetRawPointer is the way past
 * the typed interface. */

typedef struct AmodObjectsMenuApi
{
    uint32_t structSize;

    AmodToken (__cdecl* Subscribe)(AmodMod mod, AmodMenuEvent event,
                                   AmodMenuCallback callback, void* user);
    void (__cdecl* Unsubscribe)(AmodMod mod, AmodToken token);

    /* Element with this $id in this menu, or null. */
    AmodMenuItem (__cdecl* FindItem)(AmodMod mod, const char* menuId, const char* itemId);

    /* Walks the interactive elements of a menu in script order. */
    int (__cdecl* GetItemCount)(AmodMod mod, const char* menuId);
    AmodMenuItem (__cdecl* GetItemAt)(AmodMod mod, const char* menuId, int index);

    const char* (__cdecl* GetItemId)(AmodMod mod, AmodMenuItem item);

    /* Both answer non-zero on success and leave the element alone otherwise: unknown key,
     * key this element does not carry, key that cannot be written, or a value of the wrong
     * shape. */
    int (__cdecl* GetProperty)(AmodMod mod, AmodMenuItem item, const char* key, AmodValue* value);
    int (__cdecl* SetProperty)(AmodMod mod, AmodMenuItem item, const char* key, const AmodValue* value);

    int (__cdecl* HasProperty)(AmodMod mod, AmodMenuItem item, const char* key);

    /* Lists the keys an element carries. Names stay valid for the life of the loader. */
    int (__cdecl* PropertyCount)(AmodMod mod, AmodMenuItem item);
    const char* (__cdecl* PropertyNameAt)(AmodMod mod, AmodMenuItem item, int index);

    /* The element itself, for whatever this interface does not cover. Null unless
     * expectedGameBuild matches AmodApi::GetGameBuild. */
    void* (__cdecl* GetRawPointer)(AmodMod mod, AmodMenuItem item, uint32_t expectedGameBuild);

    /* Builds objects into a menu from a fragment in menu_*.txt syntax and answers the first,
     * or null. The fragment goes to the game's own parser, so the objects are exactly what a
     * script would have made. They go away when the menu is rebuilt, so this belongs in an
     * AMOD_MENU_LOADED handler. */
    AmodMenuItem (__cdecl* AddObject)(AmodMod mod, const char* menuId, const char* fragment);

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

        /* One key of one element. Holds nothing, meant to be used on the spot. */
        class Property
        {
        public:
            Property(AmodMenuItem item, const char* key, int index = -1)
                : m_item(item)
                , m_key(key)
                , m_index(index)
            {
            }

            /* One record of a collection: list["items"][2], table["col.title"][1]. Without
             * a subscript the same key answers how many records there are. */
            Property                                                operator[](int index) const
            {
                return Property(m_item, m_key, index);
            }

            bool                                                    Exists() const
            {
                char buffer[96];
                return g_menu != nullptr &&
                       g_menu->HasProperty(g_self, m_item, Resolve(buffer, sizeof(buffer))) != 0;
            }

            bool                                                    Read(AmodValue& value) const
            {
                char buffer[96];
                return g_menu != nullptr &&
                       g_menu->GetProperty(g_self, m_item, Resolve(buffer, sizeof(buffer)),
                                           &value) != 0;
            }

            /* Each returns the fallback when the key is absent or holds another kind,
             * which Exists tells apart from a genuine zero. */
            float                                                   Number(int index = 0, float fallback = 0.0f) const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_FLOAT ||
                    index < 0 || static_cast<uint32_t>(index) >= value.count)
                {
                    return fallback;
                }
                return value.numbers[index];
            }

            int                                                     Integer(int fallback = 0) const
            {
                AmodValue value = {};
                if (!Read(value) ||
                    (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL))
                {
                    return fallback;
                }
                return value.integer;
            }

            bool                                                    Boolean(bool fallback = false) const
            {
                return Integer(fallback ? 1 : 0) != 0;
            }

            const char*                                             Text(const char* fallback = "") const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_STRING || value.text == nullptr)
                {
                    return fallback;
                }
                return value.text;
            }

            bool                                                    Set(const AmodValue& value) const
            {
                char buffer[96];
                return g_menu != nullptr &&
                       g_menu->SetProperty(g_self, m_item, Resolve(buffer, sizeof(buffer)),
                                           &value) != 0;
            }

            bool                                                    Set(float a) const
            {
                return SetNumbers(&a, 1);
            }

            bool                                                    Set(float a, float b) const
            {
                const float numbers[2] = { a, b };
                return SetNumbers(numbers, 2);
            }

            bool                                                    Set(float a, float b, float c) const
            {
                const float numbers[3] = { a, b, c };
                return SetNumbers(numbers, 3);
            }

            bool                                                    Set(float a, float b, float c, float d) const
            {
                const float numbers[4] = { a, b, c, d };
                return SetNumbers(numbers, 4);
            }

            bool                                                    Set(const char* text) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_STRING;
                value.text = text;
                return Set(value);
            }

            bool                                                    Set(bool boolean) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_BOOL;
                value.integer = boolean ? 1 : 0;
                return Set(value);
            }

            bool                                                    Set(int integer) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_INT;
                value.integer = integer;
                return Set(value);
            }

        private:
            const char*                                             Resolve(char* buffer, size_t size) const
            {
                if (m_index < 0)
                {
                    return m_key;
                }

                size_t at = 0;
                while (m_key[at] != '\0' && at + 16 < size)
                {
                    buffer[at] = m_key[at];
                    ++at;
                }

                char digits[12];
                int written = 0;
                int value = m_index;
                do
                {
                    digits[written++] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                }
                while (value != 0);

                buffer[at++] = '[';
                while (written > 0)
                {
                    buffer[at++] = digits[--written];
                }
                buffer[at++] = ']';
                buffer[at] = '\0';
                return buffer;
            }

            bool                                                    SetNumbers(const float* numbers, uint32_t count) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_FLOAT;
                value.count = count;
                for (uint32_t i = 0; i < count; ++i)
                {
                    value.numbers[i] = numbers[i];
                }
                return Set(value);
            }

            AmodMenuItem m_item;
            const char* m_key;
            int m_index;
        };

        class Element
        {
        public:
            Element(AmodMenuItem item = nullptr)
                : m_item(item)
            {
            }

            bool                                                    IsValid() const
            {
                return m_item != nullptr;
            }

            AmodMenuItem                                            Handle() const
            {
                return m_item;
            }

            const char*                                             Id() const
            {
                return g_menu != nullptr ? g_menu->GetItemId(g_self, m_item) : "";
            }

            Property                                                operator[](const char* key) const
            {
                return Property(m_item, key);
            }

            int                                                     PropertyCount() const
            {
                return g_menu != nullptr ? g_menu->PropertyCount(g_self, m_item) : 0;
            }

            const char*                                             PropertyNameAt(int index) const
            {
                return g_menu != nullptr ? g_menu->PropertyNameAt(g_self, m_item, index) : "";
            }

            void*                                                   Raw(uint32_t expectedGameBuild = AMOD_GAME_BUILD_AZANGARA_10) const
            {
                return g_menu != nullptr ? g_menu->GetRawPointer(g_self, m_item, expectedGameBuild)
                                         : nullptr;
            }

        private:
            AmodMenuItem m_item;
        };

        inline Element                                              Find(const char* menuId, const char* itemId)
        {
            return Element(g_menu != nullptr ? g_menu->FindItem(g_self, menuId, itemId) : nullptr);
        }

        inline int                                                  Count(const char* menuId)
        {
            return g_menu != nullptr ? g_menu->GetItemCount(g_self, menuId) : 0;
        }

        inline Element                                              At(const char* menuId, int index)
        {
            return Element(g_menu != nullptr ? g_menu->GetItemAt(g_self, menuId, index) : nullptr);
        }

        inline Element                                              Add(const char* menuId, const char* fragment)
        {
            if (g_menu == nullptr || g_menu->structSize < sizeof(AmodObjectsMenuApi))
            {
                return Element();
            }
            return Element(g_menu->AddObject(g_self, menuId, fragment));
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_OBJECTS_MENU_API_H */
