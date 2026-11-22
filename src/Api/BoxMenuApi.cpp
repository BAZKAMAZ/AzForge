#include "src/Api/BoxMenuApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Api/ObjectsMenuApi.h"

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace GameAddress;

        // AllocTracked __cdecl; menu routines __thiscall → __fastcall (ECX = manager).
        typedef void* (__cdecl* AllocTrackedRoutine)(uint32_t size, const char* file, uint32_t line);
        typedef void* (__cdecl* ReallocTrackedRoutine)(void* block, uint32_t size, const char* file, uint32_t line);
        typedef void* (__fastcall* ConstructRoutine)(void* block, void* unused);
        typedef void* (__fastcall* FindMenuRoutine)(void* manager, void* unused, const char* id);

        // Menu_Register: __thiscall (manager, menu, script, menuId, force).
        typedef char (__fastcall* RegisterRoutine)(void* manager, void* unused, void* menu, const char* script, const char* menuId, char force);
        typedef void (__fastcall* NavigateRoutine)(void* manager, void* unused, const char* id);
        typedef void (__fastcall* ManagerRoutine)(void* manager, void* unused);
        typedef void* (__stdcall* PackageOpenRoutine)(const char* name);
        typedef void (__stdcall* PackageFreeRoutine)(void* file);

        AllocTrackedRoutine g_alloc = nullptr;
        ReallocTrackedRoutine g_realloc = nullptr;
        ConstructRoutine g_construct = nullptr;
        FindMenuRoutine g_find = nullptr;
        RegisterRoutine g_register = nullptr;
        PackageOpenRoutine g_open = nullptr;
        PackageFreeRoutine g_freeFile = nullptr;
        NavigateRoutine g_enter = nullptr;
        NavigateRoutine g_showBox = nullptr;
        ManagerRoutine g_goBack = nullptr;
        ManagerRoutine g_closeAll = nullptr;

        AmodBoxMenuApi g_table = {};

        // What each mod registered, so a mod that goes away takes its menus with it.
        struct RegisteredMenu
        {
            uint32_t id;
            ModContext* owner;
            void* menu;
            std::string menuId;
        };

        std::vector<RegisteredMenu> g_registered;
        uint32_t g_nextRegisteredId = 1;

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

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
            return g_register != nullptr &&
                   g_alloc != nullptr && g_realloc != nullptr && g_construct != nullptr &&
                   g_find != nullptr && g_enter != nullptr && g_showBox != nullptr &&
                   g_goBack != nullptr && g_closeAll != nullptr;
        }

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

        bool UnlinkFromRegistry(void* manager, void* menu)
        {
            int* const countPtr =
                reinterpret_cast<int*>(static_cast<uint8_t*>(manager) + kMenuManagerRegistryCountOffset);
            void** const array =
                *reinterpret_cast<void***>(static_cast<uint8_t*>(manager) + kMenuManagerRegistryOffset);

            if (array == nullptr || *countPtr <= 0)
            {
                return false;
            }

            for (int i = 0; i < *countPtr; ++i)
            {
                if (array[i] != menu)
                {
                    continue;
                }

                for (int j = i + 1; j < *countPtr; ++j)
                {
                    array[j - 1] = array[j];
                }
                *countPtr -= 1;
                return true;
            }

            return false;
        }

        bool IsMenuInUse(void* manager, void* menu)
        {
            uint8_t* const base = static_cast<uint8_t*>(manager);

            if (MenuField(manager, kMenuManagerCurrentOffset) == menu ||
                MenuField(manager, kMenuManagerBoxOffset) == menu)
            {
                return true;
            }

            const int top = *reinterpret_cast<const int*>(base + kMenuManagerStackTopOffset);
            void** const stack = reinterpret_cast<void**>(base + kMenuManagerStackOffset);
            for (int i = 0; i <= top && i < static_cast<int>(kMenuManagerStackCapacity); ++i)
            {
                if (stack[i] == menu)
                {
                    return true;
                }
            }

            return false;
        }

        void RetireMenu(const RegisteredMenu& record)
        {
            void* const manager = Manager();
            if (manager == nullptr)
            {
                return;
            }

            *(static_cast<uint8_t*>(record.menu) + kMenuDisabledOffset) = 1;
            UnlinkFromRegistry(manager, record.menu);

            if (IsMenuInUse(manager, record.menu))
            {
                FORGE_LOG_WARN("Menu '%s' was retired while the game still had it open; it is "
                               "disabled and unreachable, and the game will let go of it on "
                               "the next Back", record.menuId.c_str());
            }
            else
            {
                FORGE_LOG_INFO("Menu '%s' retired with the mod that registered it",
                               record.menuId.c_str());
            }
        }

        void ReleaseRegisteredMenu(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_registered.begin(); it != g_registered.end(); ++it)
            {
                if (it->id == id)
                {
                    RetireMenu(*it);
                    g_registered.erase(it);
                    return;
                }
            }
        }

        int __cdecl ApiRegister(AmodMod mod, const char* menuId)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            if (manager == nullptr || menuId == nullptr || menuId[0] == '\0')
            {
                return 0;
            }

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

            memset(block, 0, kSizeBaseMenu);
            g_construct(block, nullptr);

            // The id is what Menu_FindByMenuId matches, and it lives at menu+9.
            strncpy_s(static_cast<char*>(block) + kMenuIdOffset, 0x40, menuId, _TRUNCATE);

            if (!AppendToRegistry(manager, block))
            {
                return 0;
            }

            ModContext* const context = Resolve(mod);
            RegisteredMenu record;
            record.id = g_nextRegisteredId++;
            record.owner = context;
            record.menu = block;
            record.menuId = menuId;
            g_registered.push_back(record);

            context->owned.Add(&ReleaseRegisteredMenu,
                               reinterpret_cast<void*>(static_cast<uintptr_t>(record.id)));

            FORGE_LOG_INFO("[%s] registered menu '%s'", context->id.c_str(), menuId);
            return 1;
        }

        int __cdecl ApiRegisterFromScript(AmodMod mod, const char* menuId, const char* scriptFile)
        {
            ModContext* const context = Resolve(mod);
            void* const manager = context != nullptr ? Manager() : nullptr;
            if (manager == nullptr || menuId == nullptr || menuId[0] == '\0' ||
                scriptFile == nullptr || scriptFile[0] == '\0')
            {
                return 0;
            }

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

            memset(block, 0, kSizeBaseMenu);
            g_construct(block, nullptr);

            void* const probe = g_open != nullptr ? g_open(scriptFile) : nullptr;
            if (probe == nullptr)
            {
                FORGE_LOG_WARN("[%s] asked for menu '%s' from '%s', which no mod or the game "
                               "provides", context->id.c_str(), menuId, scriptFile);
                return 0;
            }

            if (g_freeFile != nullptr)
            {
                g_freeFile(probe);
            }

            if (g_register(manager, nullptr, block, scriptFile, menuId, 1) == '\0')
            {
                FORGE_LOG_WARN("[%s] menu '%s' could not be built from '%s'",
                               context->id.c_str(), menuId, scriptFile);
                return 0;
            }

            RegisteredMenu record;
            record.id = g_nextRegisteredId++;
            record.owner = context;
            record.menu = block;
            record.menuId = menuId;
            g_registered.push_back(record);

            context->owned.Add(&ReleaseRegisteredMenu,
                               reinterpret_cast<void*>(static_cast<uintptr_t>(record.id)));

            FORGE_LOG_INFO("[%s] registered menu '%s' from '%s'", context->id.c_str(),
                           menuId, scriptFile);
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

        char g_pendingMenu[0x40] = {};
        bool g_pendingOverlay = false;

        // The game keeps the id pointer it is given, so it is handed the menu's own id, never the caller's buffer.
        void Walk(void* manager, void* menu, bool overlay)
        {
            if (overlay)
            {
                g_showBox(manager, nullptr, MenuId(menu));
            }
            else
            {
                g_enter(manager, nullptr, MenuId(menu));
            }
        }

        void __cdecl WalkPending()
        {
            if (g_pendingMenu[0] == '\0')
            {
                return;
            }

            void* const manager = Manager();

            // Looked up again by id: a menu freed in between is simply not found.
            void* const menu = manager != nullptr ? g_find(manager, nullptr, g_pendingMenu) : nullptr;
            g_pendingMenu[0] = '\0';

            if (menu != nullptr)
            {
                Walk(manager, menu, g_pendingOverlay);
            }
        }

        int OpenMenu(AmodMod mod, const char* menuId, bool overlay)
        {
            void* const manager = Resolve(mod) != nullptr ? Manager() : nullptr;
            void* const menu = manager != nullptr && menuId != nullptr ? g_find(manager, nullptr, menuId) : nullptr;

            if (menu == nullptr)
            {
                return 0;
            }

            // From a click, Menu_Update is still busy with it; the game opens the menu itself once the click is done.
            if (ObjectsMenuApi::AskMenu(MenuId(menu), overlay))
            {
                return 1;
            }

            if (overlay)
            {
                g_showBox(manager, nullptr, MenuId(menu));
            }
            else
            {
                g_enter(manager, nullptr, MenuId(menu));
            }

            return 1;
        }

        int __cdecl ApiOpen(AmodMod mod, const char* menuId)
        {
            return OpenMenu(mod, menuId, false);
        }

        void __cdecl ApiBack(AmodMod mod)
        {
            if (Resolve(mod) != nullptr)
            {
                ObjectsMenuApi::AskBack();
            }
        }

        int __cdecl ApiShowBox(AmodMod mod, const char* menuId)
        {
            return OpenMenu(mod, menuId, true);
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
        g_open = reinterpret_cast<PackageOpenRoutine>(
            GameSymbols::Address(GameSymbol::PackageOpenFile));
        g_freeFile = reinterpret_cast<PackageFreeRoutine>(
            GameModule::Resolve(kPackageFree));
        g_register = reinterpret_cast<RegisterRoutine>(
            GameSymbols::Address(GameSymbol::MenuRegister));
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
        g_table.RegisterFromScript = &ApiRegisterFromScript;
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

    void BoxMenuApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_registered.begin(); it != g_registered.end();)
        {
            if (it->owner == owner)
            {
                RetireMenu(*it);
                it = g_registered.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void BoxMenuApi::Uninstall()
    {
        g_registered.clear();
        g_table = {};
        g_alloc = nullptr;
        g_realloc = nullptr;
        g_construct = nullptr;
        g_find = nullptr;
        g_register = nullptr;
        g_open = nullptr;
        g_freeFile = nullptr;
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
