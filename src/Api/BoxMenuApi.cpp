#include "pch.h"

#include "src/Api/BoxMenuApi.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"

#include <stdint.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        using namespace GameAddress;

        // The tracked allocator is __cdecl with its arguments on the stack; the menu routines
        // are __thiscall, emulated as __fastcall with the manager in ecx.
        typedef void* (__cdecl* AllocTrackedRoutine)(uint32_t size, const char* file, uint32_t line);
        typedef void* (__cdecl* ReallocTrackedRoutine)(void* block, uint32_t size,
                                                       const char* file, uint32_t line);
        typedef void* (__fastcall* ConstructRoutine)(void* block, void* unused);
        typedef void* (__fastcall* FindMenuRoutine)(void* manager, void* unused, const char* id);
        typedef void (__fastcall* NavigateRoutine)(void* manager, void* unused, const char* id);
        typedef void (__fastcall* ManagerRoutine)(void* manager, void* unused);

        AllocTrackedRoutine g_alloc = nullptr;
        ReallocTrackedRoutine g_realloc = nullptr;
        ConstructRoutine g_construct = nullptr;
        FindMenuRoutine g_find = nullptr;
        NavigateRoutine g_enter = nullptr;
        NavigateRoutine g_showBox = nullptr;
        ManagerRoutine g_goBack = nullptr;
        ManagerRoutine g_closeAll = nullptr;

        AmodBoxMenuApi g_table = {};

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        // The manager is reached through a global pointer, so its address is followed once
        // per call rather than cached: the game builds it after the loader is up and tears it
        // down before the loader goes, and a stale copy would outlive both.
        void* Manager()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(kMenuManager));
            return slot != nullptr ? *slot : nullptr;
        }

        void* MenuField(void* menu, uintptr_t offset)
        {
            return *reinterpret_cast<void**>(static_cast<uint8_t*>(menu) + offset);
        }

        const char* MenuId(void* menu)
        {
            return menu != nullptr
                ? reinterpret_cast<const char*>(static_cast<uint8_t*>(menu) + kMenuIdOffset)
                : "";
        }

        bool Ready()
        {
            return g_alloc != nullptr && g_realloc != nullptr && g_construct != nullptr &&
                   g_find != nullptr && g_enter != nullptr && g_showBox != nullptr &&
                   g_goBack != nullptr && g_closeAll != nullptr;
        }

        // Appends a menu pointer to the registry at manager+0x08 / +0x38, the tail of
        // Menu_Register done without its parser: the parser is what throws on an absent
        // script, and that throw has no handler on a mod's call stack the way it does on the
        // game's. This is the grow that Collection_AppendPointer performs, in C++ so the
        // calling convention of that helper does not have to be trusted.
        bool AppendToRegistry(void* manager, void* menu)
        {
            int* const countPtr = reinterpret_cast<int*>(static_cast<uint8_t*>(manager) + 0x38);
            void*** const arraySlot = reinterpret_cast<void***>(static_cast<uint8_t*>(manager) + 0x08);

            int count = *countPtr;
            void** const array = *arraySlot;
            const bool grow = array != nullptr && count > 0;

            void** const grown = grow
                ? static_cast<void**>(g_realloc(array, static_cast<uint32_t>((count + 1) * sizeof(void*)),
                                                ".\\AzForge.cpp", __LINE__))
                : static_cast<void**>(g_alloc(static_cast<uint32_t>(sizeof(void*)),
                                              ".\\AzForge.cpp", __LINE__));
            if (grown == nullptr)
            {
                return false;
            }

            if (!grow)
            {
                count = 0;
            }

            *arraySlot = grown;
            grown[count] = menu;
            *countPtr = count + 1;
            return true;
        }

        int __cdecl ApiRegister(AmodMod mod, const char* menuId)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager == nullptr || menuId == nullptr || menuId[0] == '\0')
            {
                return 0;
            }

            // Refuse to shadow an existing menu: two menus under one id would make the
            // registry answer with whichever comes first and leave the other unreachable.
            if (g_find(manager, nullptr, menuId) != nullptr)
            {
                FORGE_LOG_WARN("A mod tried to register menu '%s', which already exists", menuId);
                return 0;
            }

            void* const block = g_alloc(kSizeBaseMenu, ".\\AzForge.cpp", __LINE__);
            if (block == nullptr)
            {
                return 0;
            }

            // Zeroed before construction so the fields the base constructor leaves alone -
            // the disabled flag at +0x209 among them - are clear rather than whatever the
            // allocator handed back; a stray disabled flag would make the menu unreachable.
            memset(block, 0, kSizeBaseMenu);
            g_construct(block, nullptr);

            // The id is what Menu_FindByMenuId matches, and it lives at menu+9.
            strncpy_s(static_cast<char*>(block) + kMenuIdOffset, 0x40, menuId, _TRUNCATE);

            if (!AppendToRegistry(manager, block))
            {
                return 0;
            }

            FORGE_LOG_INFO("[%s] registered menu '%s'", Resolve(mod)->id.c_str(), menuId);
            return 1;
        }

        int __cdecl ApiExists(AmodMod mod, const char* menuId)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager == nullptr || menuId == nullptr)
            {
                return 0;
            }
            return g_find(manager, nullptr, menuId) != nullptr ? 1 : 0;
        }

        const char* __cdecl ApiCurrent(AmodMod mod)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager == nullptr)
            {
                return "";
            }
            return MenuId(MenuField(manager, kMenuManagerCurrentOffset));
        }

        const char* __cdecl ApiBox(AmodMod mod)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager == nullptr)
            {
                return "";
            }
            return MenuId(MenuField(manager, kMenuManagerBoxOffset));
        }

        int __cdecl ApiOpen(AmodMod mod, const char* menuId)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager == nullptr || menuId == nullptr)
            {
                return 0;
            }

            // Menu_EnterMenu no-ops on an unknown id, so the existence check is what makes the
            // answer honest rather than the call itself.
            if (g_find(manager, nullptr, menuId) == nullptr)
            {
                return 0;
            }

            g_enter(manager, nullptr, menuId);
            return 1;
        }

        void __cdecl ApiBack(AmodMod mod)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager != nullptr)
            {
                g_goBack(manager, nullptr);
            }
        }

        int __cdecl ApiShowBox(AmodMod mod, const char* menuId)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager == nullptr || menuId == nullptr)
            {
                return 0;
            }

            if (g_find(manager, nullptr, menuId) == nullptr)
            {
                return 0;
            }

            g_showBox(manager, nullptr, menuId);
            return 1;
        }

        void __cdecl ApiCloseAll(AmodMod mod)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager != nullptr)
            {
                g_closeAll(manager, nullptr);
            }
        }
    }

    bool BoxMenuApi::Install()
    {
        g_alloc = reinterpret_cast<AllocTrackedRoutine>(
            GameSymbols::Address(GameSymbol::MemAllocTracked));
        g_realloc = reinterpret_cast<ReallocTrackedRoutine>(
            GameSymbols::Address(GameSymbol::MemReallocTracked));
        g_construct = reinterpret_cast<ConstructRoutine>(
            GameSymbols::Address(GameSymbol::CBaseMenuConstruct));
        g_find = reinterpret_cast<FindMenuRoutine>(
            GameSymbols::Address(GameSymbol::MenuFindByMenuId));
        g_enter = reinterpret_cast<NavigateRoutine>(
            GameSymbols::Address(GameSymbol::MenuEnterMenu));
        g_showBox = reinterpret_cast<NavigateRoutine>(
            GameSymbols::Address(GameSymbol::MenuShowBox));
        g_goBack = reinterpret_cast<ManagerRoutine>(
            GameSymbols::Address(GameSymbol::MenuGoBack));
        g_closeAll = reinterpret_cast<ManagerRoutine>(
            GameSymbols::Address(GameSymbol::MenuCloseAll));

        if (!Ready())
        {
            FORGE_LOG_WARN("Not every routine the menu-navigation interface needs was located, "
                           "so it will not be offered");
            return false;
        }

        g_table.structSize = sizeof(AmodBoxMenuApi);
        g_table.Register = &ApiRegister;
        g_table.Exists = &ApiExists;
        g_table.Current = &ApiCurrent;
        g_table.Box = &ApiBox;
        g_table.Open = &ApiOpen;
        g_table.Back = &ApiBack;
        g_table.ShowBox = &ApiShowBox;
        g_table.CloseAll = &ApiCloseAll;

        FORGE_LOG_INFO("Menu-navigation interface installed");
        return true;
    }

    void BoxMenuApi::Uninstall()
    {
        g_table = {};
        g_alloc = nullptr;
        g_realloc = nullptr;
        g_construct = nullptr;
        g_find = nullptr;
        g_enter = nullptr;
        g_showBox = nullptr;
        g_goBack = nullptr;
        g_closeAll = nullptr;
    }

    const AmodBoxMenuApi* BoxMenuApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
