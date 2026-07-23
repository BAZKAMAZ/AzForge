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

        uint8_t* FieldOf(void* item, uintptr_t offset)
        {
            return static_cast<uint8_t*>(item) + offset;
        }

        // Repeats what MenuButton_ParseCommon @0x0045a600 works out after the rectangle is
        // known, with the same two expressions.
        void RecomputeCentre(void* item)
        {
            const float* const rect =
                reinterpret_cast<const float*>(FieldOf(item, GameAddress::kMenuElementRectOffset));
            float* const centre =
                reinterpret_cast<float*>(FieldOf(item, GameAddress::kMenuElementCentreOffset));

            centre[0] = rect[0] + (rect[2] - rect[0]) * 0.5f;
            centre[1] = rect[1] + (rect[3] - rect[1]) * 0.5f;
        }

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        uint8_t* Field(AmodMenuItem item, uintptr_t offset)
        {
            return static_cast<uint8_t*>(item) + offset;
        }

        // A handle is only followed when it is still one of the elements of a parsed menu.
        bool IsLive(AmodMenuItem item)
        {
            return item != nullptr && g_validElements.find(item) != g_validElements.end();
        }

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

        // Walks the groups of a freshly parsed menu and records what it holds.
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

        // --- detours -----------------------------------------------------------------

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

        // How a menu first comes into being: parsed, given its id, and put in the registry.
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

        // --- the table the mods call through ------------------------------------------

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

        int __cdecl ApiGetPosition(AmodMod mod, AmodMenuItem item, float* x, float* y)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || x == nullptr || y == nullptr)
            {
                return 0;
            }

            const float* const pos =
                reinterpret_cast<const float*>(Field(item, GameAddress::kMenuElementPosOffset));
            *x = pos[0];
            *y = pos[1];
            return 1;
        }

        int __cdecl ApiSetPosition(AmodMod mod, AmodMenuItem item, float x, float y)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }

            float* const pos = reinterpret_cast<float*>(Field(item, GameAddress::kMenuElementPosOffset));
            float* const rect = reinterpret_cast<float*>(Field(item, GameAddress::kMenuElementRectOffset));

            // The rectangle is what the element is drawn from, and the game derived it from
            // the position once, during parsing, through the alignment routine at
            // 0x0044a450. That routine cannot simply be run again: it consumes the first two
            // rectangle floats as a width and a height and replaces them with edges, so a
            // second pass would read edges as a size.
            //
            // Shifting instead is not an approximation. Every branch of that routine places
            // the edges at the position plus a constant that depends only on the size and
            // the alignment, so moving the position by a delta moves both edges by the same
            // delta whichever branch produced them.
            const float dx = x - pos[0];
            const float dy = y - pos[1];

            pos[0] = x;
            pos[1] = y;

            rect[0] += dx;
            rect[2] += dx;
            rect[1] += dy;
            rect[3] += dy;

            RecomputeCentre(item);
            return 1;
        }

        int __cdecl ApiGetBounds(AmodMod mod, AmodMenuItem item, AmodMenuRect* bounds)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || bounds == nullptr)
            {
                return 0;
            }

            const float* const rect =
                reinterpret_cast<const float*>(Field(item, GameAddress::kMenuElementRectOffset));
            bounds->left = rect[0];
            bounds->bottom = rect[1];
            bounds->right = rect[2];
            bounds->top = rect[3];
            return 1;
        }

        int __cdecl ApiSetBounds(AmodMod mod, AmodMenuItem item, const AmodMenuRect* bounds)
        {
            if (Resolve(mod) == nullptr || !IsLive(item) || bounds == nullptr)
            {
                return 0;
            }

            float* const rect = reinterpret_cast<float*>(Field(item, GameAddress::kMenuElementRectOffset));
            rect[0] = bounds->left;
            rect[1] = bounds->bottom;
            rect[2] = bounds->right;
            rect[3] = bounds->top;

            // The position at +0x5b8 is deliberately left alone: it is what the script asked
            // for, while the rectangle is what the element ended up occupying, and a caller
            // that sets the rectangle outright is speaking in the second of those.
            RecomputeCentre(item);
            return 1;
        }

        int __cdecl ApiIsVisible(AmodMod mod, AmodMenuItem item)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }
            return *Field(item, GameAddress::kMenuElementVisibleOffset) != 0 ? 1 : 0;
        }

        int __cdecl ApiSetVisible(AmodMod mod, AmodMenuItem item, int visible)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }

            *Field(item, GameAddress::kMenuElementVisibleOffset) = visible != 0 ? 1 : 0;
            return 1;
        }

        int __cdecl ApiIsTextItem(AmodMod mod, AmodMenuItem item)
        {
            if (Resolve(mod) == nullptr || !IsLive(item))
            {
                return 0;
            }
            return g_textElements.find(item) != g_textElements.end() ? 1 : 0;
        }

        int __cdecl ApiGetText(AmodMod mod, AmodMenuItem item, char* buffer, uint32_t size)
        {
            if (ApiIsTextItem(mod, item) == 0 || buffer == nullptr || size == 0)
            {
                return 0;
            }

            const char* const text =
                reinterpret_cast<const char*>(Field(item, GameAddress::kMenuItemTextOffset));
            strncpy_s(buffer, size, text, _TRUNCATE);
            return 1;
        }

        int __cdecl ApiSetText(AmodMod mod, AmodMenuItem item, const char* text)
        {
            if (ApiIsTextItem(mod, item) == 0 || text == nullptr)
            {
                return 0;
            }

            char* const destination =
                reinterpret_cast<char*>(Field(item, GameAddress::kMenuItemTextOffset));
            strncpy_s(destination, GameAddress::kMenuItemTextCapacity, text, _TRUNCATE);
            return 1;
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

        g_originalRegister = reinterpret_cast<RegisterRoutine>(g_registerHook.Trampoline());
        g_originalParse = reinterpret_cast<ParseScriptRoutine>(g_parseHook.Trampoline());
        g_originalItemParse = reinterpret_cast<ItemParseRoutine>(g_itemHook.Trampoline());
        g_originalLoad = reinterpret_cast<LoadFromScriptRoutine>(g_loadHook.Trampoline());

        g_table.structSize = sizeof(AmodObjectsMenuApi);
        g_table.Subscribe = &ApiSubscribe;
        g_table.Unsubscribe = &ApiUnsubscribe;
        g_table.FindItem = &ApiFindItem;
        g_table.GetItemCount = &ApiGetItemCount;
        g_table.GetItemAt = &ApiGetItemAt;
        g_table.GetItemId = &ApiGetItemId;
        g_table.GetPosition = &ApiGetPosition;
        g_table.SetPosition = &ApiSetPosition;
        g_table.GetBounds = &ApiGetBounds;
        g_table.SetBounds = &ApiSetBounds;
        g_table.IsVisible = &ApiIsVisible;
        g_table.SetVisible = &ApiSetVisible;
        g_table.IsTextItem = &ApiIsTextItem;
        g_table.GetText = &ApiGetText;
        g_table.SetText = &ApiSetText;
        g_table.GetRawPointer = &ApiGetRawPointer;

        FORGE_LOG_INFO("Menu interface installed");
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
