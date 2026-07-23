#include "pch.h"

#include "src/Api/ObjectsMenuApi.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <map>
#include <set>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        typedef int(__fastcall* RegisterRoutine)(void* manager, void* unused, void* menu,
                                                 const char* scriptFile, const char* menuId,
                                                 int force);
        typedef int(__stdcall* LoadFromScriptRoutine)(const char* menuId, const char* scriptFile);
        typedef int(__fastcall* ParseScriptRoutine)(void* menu, void* unused, const char* scriptFile);
        typedef int(__fastcall* ItemParseRoutine)(void* element, void* unused, void* parser, void* extra);
        typedef void*(__fastcall* FindItemRoutine)(void* menu, void* unused, const char* itemId);

        struct Subscription
        {
            uint32_t id;
            ModContext* owner;
            AmodMenuEvent event;
            AmodMenuCallback callback;
            void* user;
        };

        InlineHook g_registerHook;
        InlineHook g_loadHook;
        InlineHook g_parseHook;
        InlineHook g_itemHook;

        RegisterRoutine g_originalRegister = nullptr;
        LoadFromScriptRoutine g_originalLoad = nullptr;
        ParseScriptRoutine g_originalParse = nullptr;
        ItemParseRoutine g_originalItemParse = nullptr;
        FindItemRoutine g_findItem = nullptr;

        std::map<std::string, void*> g_menusById;
        std::map<void*, std::vector<void*>> g_elementsByMenu;
        std::set<void*> g_validElements;
        std::set<void*> g_textElements;

        void* g_parsingMenu = nullptr;
        void* g_lastParsedMenu = nullptr;

        std::vector<Subscription> g_subscriptions;
        uint32_t g_nextSubscriptionId = 1;

        // These game routines answer in AL and leave the rest of EAX holding whatever was
        // there, so only the low byte carries the result. The full value is still what gets
        // returned to the caller, untouched.
        bool Succeeded(int result)
        {
            return (result & 0xFF) != 0;
        }

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        uint8_t* Field(void* item, uintptr_t offset)
        {
            return static_cast<uint8_t*>(item) + offset;
        }

        float* Floats(void* item, uintptr_t offset)
        {
            return reinterpret_cast<float*>(Field(item, offset));
        }

        // A handle is only followed when it is still one of the elements of a parsed menu.
        bool IsLive(AmodMenuItem item)
        {
            return item != nullptr && g_validElements.find(item) != g_validElements.end();
        }

        bool IsTextElement(AmodMenuItem item)
        {
            return g_textElements.find(item) != g_textElements.end();
        }

        // --- geometry ------------------------------------------------------------------

        // Repeats what MenuButton_ParseCommon @0x0045a600 works out once the rectangle is
        // known, with the same two expressions. This is what MenuItem_Render @0x0045f620
        // actually draws at, so a rectangle changed without this changes nothing on screen.
        void RecomputeCentre(void* item)
        {
            const float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            float* const centre = Floats(item, GameAddress::kMenuElementCentreOffset);

            centre[0] = rect[0] + (rect[2] - rect[0]) * 0.5f;
            centre[1] = rect[1] + (rect[3] - rect[1]) * 0.5f;
        }

        // Places the rectangle from the position, a size and the alignment flags, mirroring
        // FUN_0044a450 @0x0044a450 branch for branch.
        //
        // That routine is not called instead, because it is destructive: on entry it reads
        // the first two rectangle floats as a width and a height and replaces them with
        // edges, so running it a second time would read edges as a size. Reproducing it
        // here means the size can be supplied explicitly and the element re-placed as often
        // as a mod likes.
        void ApplyGeometry(void* item, float width, float height)
        {
            const float* const pos = Floats(item, GameAddress::kMenuElementPosOffset);
            float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            const uint32_t align = *reinterpret_cast<const uint32_t*>(
                Field(item, GameAddress::kMenuElementAlignOffset));

            if ((align & 0x04) != 0)            // horizontally centred
            {
                rect[0] = pos[0] - width * 0.5f;
                rect[2] = pos[0] + width * 0.5f;
            }
            else if ((align & 0x02) != 0)       // right edge at the position
            {
                rect[0] = pos[0] - width;
                rect[2] = pos[0];
            }
            else                                // left edge at the position
            {
                rect[0] = pos[0];
                rect[2] = pos[0] + width;
            }

            if ((align & 0x20) != 0)            // vertically centred
            {
                rect[1] = pos[1] - height * 0.5f;
                rect[3] = pos[1] + height * 0.5f;
            }
            else if ((align & 0x08) != 0)       // top edge at the position
            {
                rect[1] = pos[1] - height;
                rect[3] = pos[1];
            }
            else                                // bottom edge at the position
            {
                rect[1] = pos[1];
                rect[3] = pos[1] + height;
            }

            RecomputeCentre(item);
        }

        void CurrentSize(void* item, float& width, float& height)
        {
            const float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            width = rect[2] - rect[0];
            height = rect[3] - rect[1];
        }

        // --- the property table --------------------------------------------------------

        enum class Store
        {
            Float,
            String,
            Bool,
            Int
        };

        struct PropertyDescriptor
        {
            const char* key;
            uintptr_t offset;
            Store store;
            uint32_t count;     // how many floats, for Store::Float
            size_t capacity;    // bytes available, for Store::String
            bool itemOnly;      // only on elements MenuItem_Parse built
            bool writable;
            bool (*read)(void* item, AmodValue& value);              // null: plain load
            bool (*write)(void* item, const AmodValue& value);       // null: plain store
        };

        bool ReadSize(void* item, AmodValue& value)
        {
            float width = 0.0f;
            float height = 0.0f;
            CurrentSize(item, width, height);

            value.kind = AMOD_VALUE_FLOAT;
            value.count = 2;
            value.numbers[0] = width;
            value.numbers[1] = height;
            return true;
        }

        bool WriteSize(void* item, const AmodValue& value)
        {
            ApplyGeometry(item, value.numbers[0], value.numbers[1]);
            return true;
        }

        bool WritePosition(void* item, const AmodValue& value)
        {
            float width = 0.0f;
            float height = 0.0f;
            CurrentSize(item, width, height);

            float* const pos = Floats(item, GameAddress::kMenuElementPosOffset);
            pos[0] = value.numbers[0];
            pos[1] = value.numbers[1];

            ApplyGeometry(item, width, height);
            return true;
        }

        // Alignment decides how the rectangle hangs off the position, so changing it has to
        // place the element again. The size is what the element currently occupies, which is
        // the same thing $rect_size gave the parser.
        bool WriteAlign(void* item, const AmodValue& value)
        {
            float width = 0.0f;
            float height = 0.0f;
            CurrentSize(item, width, height);

            *reinterpret_cast<int*>(Field(item, GameAddress::kMenuElementAlignOffset)) = value.integer;

            ApplyGeometry(item, width, height);
            return true;
        }

        bool WriteRect(void* item, const AmodValue& value)
        {
            float* const rect = Floats(item, GameAddress::kMenuElementRectOffset);
            for (int i = 0; i < 4; ++i)
            {
                rect[i] = value.numbers[i];
            }

            // The position at +0x5b8 is deliberately left alone: it is what the script asked
            // for, while the rectangle is what the element ended up occupying, and a caller
            // that sets the edges outright is speaking in the second of those.
            RecomputeCentre(item);
            return true;
        }

        // Every string capacity below is the distance to the next field the parser writes,
        // so none of them is a guess.
        constexpr PropertyDescriptor g_properties[] =
        {
            // key                offset  store          n  cap    itemOnly writable read        write
            // The parser reads every string with a limit of 0x100 whatever field it is
            // filling, so a long $id in a script overruns into the text that follows it.
            // The capacity here is the distance to that next field instead, which is
            // stricter than the game is with itself.
            { "id",               0x715,  Store::String, 0, 0x43,  false,   true,    nullptr,    nullptr },
            { "sub_menu",         0x014,  Store::String, 0, 0x40,  false,   true,    nullptr,    nullptr },
            { "box_menu",         0x054,  Store::String, 0, 0x40,  false,   true,    nullptr,    nullptr },
            { "reg_menu",         0x094,  Store::String, 0, 0x100, false,   true,    nullptr,    nullptr },
            { "group",            0x194,  Store::String, 0, 0x100, false,   true,    nullptr,    nullptr },
            { "help",             0x2B5,  Store::String, 0, 0x100, false,   true,    nullptr,    nullptr },
            { "warning",          0x3B5,  Store::String, 0, 0x100, false,   true,    nullptr,    nullptr },
            { "lock",             0x4B5,  Store::String, 0, 0x100, false,   true,    nullptr,    nullptr },
            { "unreg_enabled",    0x5B5,  Store::Bool,   0, 0,     false,   true,    nullptr,    nullptr },
            { "pos",              0x5B8,  Store::Float,  2, 0,     false,   true,    nullptr,    &WritePosition },
            { "rect",             0x294,  Store::Float,  4, 0,     false,   true,    nullptr,    &WriteRect },
            { "size",             0,      Store::Float,  2, 0,     false,   true,    &ReadSize,  &WriteSize },
            { "rect_size",        0,      Store::Float,  2, 0,     false,   true,    &ReadSize,  &WriteSize },
            { "centre",           0x5C0,  Store::Float,  2, 0,     false,   false,   nullptr,    nullptr },
            { "align",            0x5C8,  Store::Int,    0, 0,     false,   true,    nullptr,    &WriteAlign },
            { "font_id",          0x5F8,  Store::Int,    0, 0,     false,   false,   nullptr,    nullptr },
            { "caption.text",     0x5FF,  Store::String, 0, 0x101, false,   true,    nullptr,    nullptr },
            { "caption.offs",     0x700,  Store::Float,  2, 0,     false,   true,    nullptr,    nullptr },
            { "caption.rect_len", 0x708,  Store::Float,  1, 0,     false,   true,    nullptr,    nullptr },
            { "visible",          0x714,  Store::Bool,   0, 0,     false,   true,    nullptr,    nullptr },
            { "text",             0x758,  Store::String, 0, 0x40,  true,    true,    nullptr,    nullptr },
            { "cursor3d",         0x79C,  Store::Bool,   0, 0,     true,    true,    nullptr,    nullptr },
        };

        constexpr size_t kPropertyCount = sizeof(g_properties) / sizeof(g_properties[0]);

        const PropertyDescriptor* FindDescriptor(AmodMenuItem item, const char* key)
        {
            if (key == nullptr)
            {
                return nullptr;
            }

            for (const PropertyDescriptor& descriptor : g_properties)
            {
                if (strcmp(descriptor.key, key) != 0)
                {
                    continue;
                }

                // A key an element does not carry is refused, never written to whatever that
                // other kind of element keeps at the same offset.
                if (descriptor.itemOnly && !IsTextElement(item))
                {
                    return nullptr;
                }

                return &descriptor;
            }

            return nullptr;
        }

        bool LoadProperty(void* item, const PropertyDescriptor& descriptor, AmodValue& value)
        {
            value = AmodValue();

            if (descriptor.read != nullptr)
            {
                return descriptor.read(item, value);
            }

            switch (descriptor.store)
            {
            case Store::Float:
            {
                const float* const numbers = Floats(item, descriptor.offset);
                value.kind = AMOD_VALUE_FLOAT;
                value.count = descriptor.count;
                for (uint32_t i = 0; i < descriptor.count; ++i)
                {
                    value.numbers[i] = numbers[i];
                }
                return true;
            }

            case Store::String:
                value.kind = AMOD_VALUE_STRING;
                value.text = reinterpret_cast<const char*>(Field(item, descriptor.offset));
                return true;

            case Store::Bool:
                value.kind = AMOD_VALUE_BOOL;
                value.integer = *Field(item, descriptor.offset) != 0 ? 1 : 0;
                return true;

            case Store::Int:
                value.kind = AMOD_VALUE_INT;
                value.integer = *reinterpret_cast<const int*>(Field(item, descriptor.offset));
                return true;
            }

            return false;
        }

        bool StoreProperty(void* item, const PropertyDescriptor& descriptor, const AmodValue& value)
        {
            if (!descriptor.writable)
            {
                return false;
            }

            switch (descriptor.store)
            {
            case Store::Float:
                if (value.kind != AMOD_VALUE_FLOAT || value.count != descriptor.count)
                {
                    return false;
                }
                break;

            case Store::String:
                if (value.kind != AMOD_VALUE_STRING || value.text == nullptr)
                {
                    return false;
                }
                break;

            case Store::Bool:
            case Store::Int:
                if (value.kind != AMOD_VALUE_BOOL && value.kind != AMOD_VALUE_INT)
                {
                    return false;
                }
                break;
            }

            if (descriptor.write != nullptr)
            {
                return descriptor.write(item, value);
            }

            switch (descriptor.store)
            {
            case Store::Float:
            {
                float* const numbers = Floats(item, descriptor.offset);
                for (uint32_t i = 0; i < descriptor.count; ++i)
                {
                    numbers[i] = value.numbers[i];
                }
                return true;
            }

            case Store::String:
                strncpy_s(reinterpret_cast<char*>(Field(item, descriptor.offset)),
                          descriptor.capacity, value.text, _TRUNCATE);
                return true;

            case Store::Bool:
                *Field(item, descriptor.offset) = value.integer != 0 ? 1 : 0;
                return true;

            case Store::Int:
                *reinterpret_cast<int*>(Field(item, descriptor.offset)) = value.integer;
                return true;
            }

            return false;
        }

        // --- bookkeeping ---------------------------------------------------------------

        void ForgetMenu(void* menu)
        {
            const auto found = g_elementsByMenu.find(menu);
            if (found == g_elementsByMenu.end())
            {
                return;
            }

            for (void* element : found->second)
            {
                g_validElements.erase(element);
                g_textElements.erase(element);
            }
            g_elementsByMenu.erase(found);
        }

        void RecordMenu(void* menu)
        {
            std::vector<void*> elements;

            uint8_t* const groups = *reinterpret_cast<uint8_t**>(Field(menu, GameAddress::kMenuGroupsOffset));
            const int groupCount = *reinterpret_cast<int*>(Field(menu, GameAddress::kMenuGroupCountOffset));

            if (groups != nullptr && groupCount > 0)
            {
                for (int i = 0; i < groupCount; ++i)
                {
                    uint8_t* const group = groups + static_cast<size_t>(i) * GameAddress::kMenuGroupStride;
                    void** const items = *reinterpret_cast<void***>(group + GameAddress::kMenuGroupItemsOffset);
                    const int itemCount = *reinterpret_cast<int*>(group + GameAddress::kMenuGroupItemCountOffset);

                    for (int k = 0; items != nullptr && k < itemCount; ++k)
                    {
                        if (items[k] != nullptr)
                        {
                            elements.push_back(items[k]);
                            g_validElements.insert(items[k]);
                        }
                    }
                }
            }

            g_elementsByMenu[menu] = std::move(elements);
        }

        void Dispatch(AmodMenuEvent event, void* data)
        {
            const std::vector<Subscription> snapshot = g_subscriptions;
            for (const Subscription& item : snapshot)
            {
                if (item.event == event && item.callback != nullptr)
                {
                    item.callback(event, data, item.user);
                }
            }
        }

        void ReleaseSubscription(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (size_t i = 0; i < g_subscriptions.size(); ++i)
            {
                if (g_subscriptions[i].id == id)
                {
                    g_subscriptions.erase(g_subscriptions.begin() + static_cast<ptrdiff_t>(i));
                    return;
                }
            }
        }

        // --- detours -------------------------------------------------------------------

        // The id is read out of the object, never taken from the caller's argument: a menu
        // built from a script gets it from the $menu_id of its own $header, and the argument
        // is null in that case. Menu_Register has already copied the argument in by the time
        // this runs, so the object is right either way.
        void Publish(void* menu)
        {
            if (menu == nullptr)
            {
                return;
            }

            RecordMenu(menu);

            const char* const menuId =
                reinterpret_cast<const char*>(Field(menu, GameAddress::kMenuIdOffset));
            if (menuId[0] == '\0')
            {
                return;
            }

            g_menusById[menuId] = menu;

            AmodMenuLoaded loaded = {};
            loaded.menuId = menuId;
            Dispatch(AMOD_MENU_LOADED, &loaded);
        }

        int __fastcall ParseScriptDetour(void* menu, void* unused, const char* scriptFile)
        {
            // Rebuilding a menu frees whatever it held, so the old handles stop being live
            // before the new ones appear.
            ForgetMenu(menu);

            g_parsingMenu = menu;
            const int result = g_originalParse(menu, unused, scriptFile);
            g_parsingMenu = nullptr;

            // Nothing is read out of the object here. This routine also serves callers that
            // pass something other than a menu, and only Menu_Register tells the two apart.
            if (Succeeded(result))
            {
                g_lastParsedMenu = menu;
            }

            return result;
        }

        int __fastcall ItemParseDetour(void* element, void* unused, void* parser, void* extra)
        {
            const int result = g_originalItemParse(element, unused, parser, extra);

            // Only what came through here owns the text field MenuItem_Render draws.
            if (Succeeded(result) && g_parsingMenu != nullptr)
            {
                g_textElements.insert(element);
            }

            return result;
        }

        int __fastcall RegisterDetour(void* manager, void* unused, void* menu,
                                      const char* scriptFile, const char* menuId, int force)
        {
            const int result = g_originalRegister(manager, unused, menu, scriptFile, menuId, force);

            if (Succeeded(result))
            {
                Publish(menu);
            }

            return result;
        }

        int __stdcall LoadFromScriptDetour(const char* menuId, const char* scriptFile)
        {
            g_lastParsedMenu = nullptr;

            const int result = g_originalLoad(menuId, scriptFile);

            if (Succeeded(result))
            {
                Publish(g_lastParsedMenu);
            }

            return result;
        }

        void* MenuByIdOrNull(const char* menuId)
        {
            if (menuId == nullptr)
            {
                return nullptr;
            }

            const auto found = g_menusById.find(menuId);
            return found == g_menusById.end() ? nullptr : found->second;
        }

        // --- the table the mods call through -------------------------------------------

        AmodToken __cdecl ApiSubscribe(AmodMod mod, AmodMenuEvent event,
                                       AmodMenuCallback callback, void* user)
        {
            ModContext* context = Resolve(mod);
            if (context == nullptr || callback == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            Subscription item;
            item.id = g_nextSubscriptionId++;
            item.owner = context;
            item.event = event;
            item.callback = callback;
            item.user = user;
            g_subscriptions.push_back(item);

            return context->owned.Add(&ReleaseSubscription,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(item.id)));
        }

        void __cdecl ApiUnsubscribe(AmodMod mod, AmodToken token)
        {
            ModContext* context = Resolve(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }

        AmodMenuItem __cdecl ApiFindItem(AmodMod mod, const char* menuId, const char* itemId)
        {
            void* const menu = Resolve(mod) != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr || itemId == nullptr || g_findItem == nullptr)
            {
                return nullptr;
            }

            void* const element = g_findItem(menu, nullptr, itemId);
            return IsLive(element) ? element : nullptr;
        }

        int __cdecl ApiGetItemCount(AmodMod mod, const char* menuId)
        {
            void* const menu = Resolve(mod) != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr)
            {
                return 0;
            }

            const auto found = g_elementsByMenu.find(menu);
            return found == g_elementsByMenu.end() ? 0 : static_cast<int>(found->second.size());
        }

        AmodMenuItem __cdecl ApiGetItemAt(AmodMod mod, const char* menuId, int index)
        {
            void* const menu = Resolve(mod) != nullptr ? MenuByIdOrNull(menuId) : nullptr;
            if (menu == nullptr || index < 0)
            {
                return nullptr;
            }

            const auto found = g_elementsByMenu.find(menu);
            if (found == g_elementsByMenu.end() ||
                static_cast<size_t>(index) >= found->second.size())
            {
                return nullptr;
            }

            return found->second[static_cast<size_t>(index)];
        }

        const char* __cdecl ApiGetItemId(AmodMod mod, AmodMenuItem item)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return "";
            }
            return reinterpret_cast<const char*>(Field(item, GameAddress::kMenuElementIdOffset));
        }

        int __cdecl ApiGetProperty(AmodMod mod, AmodMenuItem item, const char* key, AmodValue* value)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || value == nullptr)
            {
                return 0;
            }

            const PropertyDescriptor* const descriptor = FindDescriptor(item, key);
            if (descriptor == nullptr)
            {
                return 0;
            }

            return LoadProperty(item, *descriptor, *value) ? 1 : 0;
        }

        int __cdecl ApiSetProperty(AmodMod mod, AmodMenuItem item, const char* key,
                                   const AmodValue* value)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || value == nullptr)
            {
                return 0;
            }

            const PropertyDescriptor* const descriptor = FindDescriptor(item, key);
            if (descriptor == nullptr)
            {
                return 0;
            }

            return StoreProperty(item, *descriptor, *value) ? 1 : 0;
        }

        int __cdecl ApiHasProperty(AmodMod mod, AmodMenuItem item, const char* key)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }
            return FindDescriptor(item, key) != nullptr ? 1 : 0;
        }

        int __cdecl ApiPropertyCount(AmodMod mod, AmodMenuItem item)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }

            int count = 0;
            for (const PropertyDescriptor& descriptor : g_properties)
            {
                if (!descriptor.itemOnly || IsTextElement(item))
                {
                    ++count;
                }
            }
            return count;
        }

        const char* __cdecl ApiPropertyNameAt(AmodMod mod, AmodMenuItem item, int index)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || index < 0)
            {
                return "";
            }

            int seen = 0;
            for (const PropertyDescriptor& descriptor : g_properties)
            {
                if (descriptor.itemOnly && !IsTextElement(item))
                {
                    continue;
                }

                if (seen == index)
                {
                    return descriptor.key;
                }
                ++seen;
            }

            return "";
        }

        void* __cdecl ApiGetRawPointer(AmodMod mod, AmodMenuItem item, uint32_t expectedGameBuild)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) ||
                expectedGameBuild != AMOD_GAME_BUILD_AZANGARA_10)
            {
                return nullptr;
            }
            return item;
        }

        AmodObjectsMenuApi g_table = {};

        bool InstallOne(InlineHook& hook, GameSymbol symbol, void* detour,
                        const uint8_t* prologue, size_t prologueLength)
        {
            void* const target = GameSymbols::Address(symbol);
            if (target == nullptr)
            {
                FORGE_LOG_ERROR("%s was not located, the menu interface is not available",
                                GameSymbols::Name(symbol));
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, prologue, prologueLength, mismatch))
            {
                FORGE_LOG_ERROR("%s does not open with the instructions AzForge steals (%s)",
                                GameSymbols::Name(symbol), mismatch.c_str());
                return false;
            }

            std::string error;
            if (!hook.Install(target, detour, prologue, prologueLength, error))
            {
                FORGE_LOG_ERROR("Cannot hook %s: %s", GameSymbols::Name(symbol), error.c_str());
                return false;
            }

            return true;
        }
    }

    bool ObjectsMenuApi::Install()
    {
        if (g_parseHook.IsInstalled())
        {
            return true;
        }

        g_findItem = reinterpret_cast<FindItemRoutine>(
            GameSymbols::Address(GameSymbol::MenuFindItemById));
        if (g_findItem == nullptr)
        {
            FORGE_LOG_ERROR("Menu_FindItemById was not located, the menu interface is not available");
            return false;
        }

        if (!InstallOne(g_parseHook, GameSymbol::MenuParseScript,
                        reinterpret_cast<void*>(&ParseScriptDetour),
                        kMenuParseScriptPrologue, sizeof(kMenuParseScriptPrologue)) ||
            !InstallOne(g_itemHook, GameSymbol::MenuItemParse,
                        reinterpret_cast<void*>(&ItemParseDetour),
                        kMenuItemParsePrologue, sizeof(kMenuItemParsePrologue)) ||
            !InstallOne(g_loadHook, GameSymbol::MenuLoadFromScript,
                        reinterpret_cast<void*>(&LoadFromScriptDetour),
                        kMenuLoadFromScriptPrologue, sizeof(kMenuLoadFromScriptPrologue)) ||
            !InstallOne(g_registerHook, GameSymbol::MenuRegister,
                        reinterpret_cast<void*>(&RegisterDetour),
                        kMenuRegisterPrologue, sizeof(kMenuRegisterPrologue)))
        {
            Uninstall();
            return false;
        }

        g_originalParse = reinterpret_cast<ParseScriptRoutine>(g_parseHook.Trampoline());
        g_originalItemParse = reinterpret_cast<ItemParseRoutine>(g_itemHook.Trampoline());
        g_originalLoad = reinterpret_cast<LoadFromScriptRoutine>(g_loadHook.Trampoline());
        g_originalRegister = reinterpret_cast<RegisterRoutine>(g_registerHook.Trampoline());

        g_table.structSize = sizeof(AmodObjectsMenuApi);
        g_table.Subscribe = &ApiSubscribe;
        g_table.Unsubscribe = &ApiUnsubscribe;
        g_table.FindItem = &ApiFindItem;
        g_table.GetItemCount = &ApiGetItemCount;
        g_table.GetItemAt = &ApiGetItemAt;
        g_table.GetItemId = &ApiGetItemId;
        g_table.GetProperty = &ApiGetProperty;
        g_table.SetProperty = &ApiSetProperty;
        g_table.HasProperty = &ApiHasProperty;
        g_table.PropertyCount = &ApiPropertyCount;
        g_table.PropertyNameAt = &ApiPropertyNameAt;
        g_table.GetRawPointer = &ApiGetRawPointer;

        FORGE_LOG_INFO("Menu interface installed, %zu propert(ies) per element", kPropertyCount);
        return true;
    }

    void ObjectsMenuApi::Uninstall()
    {
        g_registerHook.Uninstall();
        g_loadHook.Uninstall();
        g_parseHook.Uninstall();
        g_itemHook.Uninstall();

        g_originalRegister = nullptr;
        g_originalLoad = nullptr;
        g_originalParse = nullptr;
        g_originalItemParse = nullptr;
        g_findItem = nullptr;

        g_subscriptions.clear();
        g_menusById.clear();
        g_elementsByMenu.clear();
        g_validElements.clear();
        g_textElements.clear();
        g_parsingMenu = nullptr;
        g_lastParsedMenu = nullptr;
        g_table = {};
    }

    const AmodObjectsMenuApi* ObjectsMenuApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void ObjectsMenuApi::ForgetMod(ModContext* owner)
    {
        for (size_t i = g_subscriptions.size(); i-- > 0;)
        {
            if (g_subscriptions[i].owner == owner)
            {
                g_subscriptions.erase(g_subscriptions.begin() + static_cast<ptrdiff_t>(i));
            }
        }
    }
}
