/*
 * ObjectsMenuApi.h - the in-game menus.
 *
 * Menus are built from menu_*.txt scripts. Interactive elements declare a mandatory $id and
 * are addressed by it; decorative $image and $static_text entries carry no identity and are
 * not reachable here.
 *
 * Elements are read and written through the same keys their script block uses, so $pos is
 * "pos" and a key inside a nested block is a path: "caption.offs".
 *
 * Reached through AmodApi::objectsMenu, null on a loader older than API version 3.
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
     * again every time the game rebuilds the menu. */
    AMOD_MENU_LOADED = 0
} AmodMenuEvent;

typedef struct AmodMenuLoaded
{
    const char* menuId;     /* $menu_id from the script header, "IDM_MAIN_MENU" and so on */
} AmodMenuLoaded;

typedef void (__cdecl* AmodMenuCallback)(AmodMenuEvent event, void* data, void* user);

typedef enum AmodValueKind
{
    AMOD_VALUE_NONE = 0,
    AMOD_VALUE_BOOL = 1,
    AMOD_VALUE_INT = 2,
    AMOD_VALUE_FLOAT = 3,   /* one to four, see count */
    AMOD_VALUE_STRING = 4
} AmodValueKind;

typedef struct AmodValue
{
    AmodValueKind kind;
    uint32_t count;         /* floats in use */
    int integer;            /* AMOD_VALUE_BOOL, AMOD_VALUE_INT */
    float numbers[4];       /* AMOD_VALUE_FLOAT */
    const char* text;       /* AMOD_VALUE_STRING; when read, points into the element */
} AmodValue;

/*
 * Keys carried by every element:
 *
 *   id                string   renaming it is allowed and breaks lookups by the old name
 *   pos               float 2  writing it moves the rectangle and the drawing centre too
 *   size, rect_size   float 2  width and height; writing places the element again
 *   rect              float 4  left, bottom, right, top
 *   centre            float 2  where the element is drawn, read only
 *   align             int      1 left, 2 right, 4 centred, 8 top, 0x20 centred vertically
 *   visible           bool
 *   unreg_enabled     bool
 *   group             string
 *   sub_menu          string
 *   box_menu          string
 *   reg_menu          string
 *   help              string
 *   warning           string
 *   lock              string
 *   font_id           int      resolved font, read only
 *   caption.text      string
 *   caption.offs      float 2
 *   caption.rect_len  float 1
 *
 * Carried only by elements the game built as $item:
 *
 *   text              string   the label that is drawn
 *   cursor3d          bool
 *
 * A key this kind of element does not carry is refused: the field belongs to another kind,
 * and what this one keeps at that offset is not known, so writing there would corrupt
 * something unrelated. GetRawPointer is the way past that.
 */

typedef struct AmodObjectsMenuApi
{
    uint32_t structSize;

    /* --- since API version 3 ------------------------------------------------------- */

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

    /* Fields added in later API versions go here, and nowhere else. */
} AmodObjectsMenuApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

/*
 * Inline layer, compiled into the mod:
 *
 *     auto quit = Amod::Menu::Find(menuId, "ID_QUIT");
 *     quit["pos"].Set(320.0f, 200.0f);
 *     quit["text"].Set("LEAVE");
 *     float x = quit["pos"].Number(0);
 */
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
            Property(AmodMenuItem item, const char* key)
                : m_item(item)
                , m_key(key)
            {
            }

            bool                                                    Exists() const
            {
                return g_menu != nullptr && g_menu->HasProperty(g_self, m_item, m_key) != 0;
            }

            bool                                                    Read(AmodValue& value) const
            {
                return g_menu != nullptr && g_menu->GetProperty(g_self, m_item, m_key, &value) != 0;
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
                return g_menu != nullptr && g_menu->SetProperty(g_self, m_item, m_key, &value) != 0;
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
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_OBJECTS_MENU_API_H */
