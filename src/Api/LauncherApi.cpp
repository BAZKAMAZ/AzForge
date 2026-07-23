#include "pch.h"

#include "src/Api/LauncherApi.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        typedef int(__fastcall* OnInitDialogRoutine)(void* self, void* unused);
        typedef int(__fastcall* OnCommandRoutine)(void* self, void* unused, int commandId, int notifyCode);
        typedef void(__fastcall* RunModalRoutine)(void* self, void* unused, HINSTANCE instance,
                                                  HWND parent, const char* dialogTemplate);
        typedef void(__fastcall* HideOptionalButtonsRoutine)(void* self, void* unused);

        struct Subscription
        {
            uint32_t id;
            ModContext* owner;
            AmodLauncherEvent event;
            AmodLauncherCallback callback;
            void* user;
        };

        InlineHook g_initHook;
        InlineHook g_commandHook;
        InlineHook g_modalHook;
        InlineHook g_hideHook;

        OnInitDialogRoutine g_originalOnInit = nullptr;
        OnCommandRoutine g_originalOnCommand = nullptr;
        RunModalRoutine g_originalRunModal = nullptr;
        HideOptionalButtonsRoutine g_originalHideOptional = nullptr;

        void* g_object = nullptr;
        HWND g_window = nullptr;

        std::vector<Subscription> g_subscriptions;
        uint32_t g_nextSubscriptionId = 1;

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        HWND ControlWindow(int controlId)
        {
            if (g_window == nullptr || !IsWindow(g_window))
            {
                return nullptr;
            }
            return GetDlgItem(g_window, controlId);
        }

        void Dispatch(AmodLauncherEvent event, void* data)
        {
            // A callback may unsubscribe itself or another, so the list is walked over a
            // copy rather than in place.
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

        int __fastcall OnInitDialogDetour(void* self, void* unused)
        {
            const int result = g_originalOnInit(self, unused);

            g_object = self;
            g_window = *reinterpret_cast<HWND*>(static_cast<uint8_t*>(self) +
                                                GameAddress::kLauncherWindowOffset);

            Dispatch(AMOD_LAUNCHER_OPENED, nullptr);
            return result;
        }

        void __fastcall HideOptionalButtonsDetour(void* self, void* unused)
        {
            g_originalHideOptional(self, unused);

            // The window is needed to reach the controls, and at the first call this runs
            // inside Launcher_OnInitDialog, before that detour has recorded it.
            if (g_window == nullptr)
            {
                g_object = self;
                g_window = *reinterpret_cast<HWND*>(static_cast<uint8_t*>(self) +
                                                    GameAddress::kLauncherWindowOffset);
            }

            Dispatch(AMOD_LAUNCHER_LAYOUT_APPLIED, nullptr);
        }

        int __fastcall OnCommandDetour(void* self, void* unused, int commandId, int notifyCode)
        {
            AmodLauncherCommand command = {};
            command.commandId = commandId;
            command.notifyCode = notifyCode;
            command.handled = 0;

            Dispatch(AMOD_LAUNCHER_COMMAND, &command);

            if (command.handled != 0)
            {
                return 0;
            }

            return g_originalOnCommand(self, unused, commandId, notifyCode);
        }

        void __fastcall RunModalDetour(void* self, void* unused, HINSTANCE instance, HWND parent,
                                       const char* dialogTemplate)
        {
            const bool launcher = reinterpret_cast<uintptr_t>(dialogTemplate) ==
                                  GameAddress::kLauncherDialogTemplate;

            g_originalRunModal(self, unused, instance, parent, dialogTemplate);

            if (launcher)
            {
                Dispatch(AMOD_LAUNCHER_CLOSING, nullptr);
                g_window = nullptr;
                g_object = nullptr;
            }
        }

        // --- the table the mods call through ----------------------------------------

        AmodToken __cdecl ApiSubscribe(AmodMod mod, AmodLauncherEvent event,
                                       AmodLauncherCallback callback, void* user)
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

        int __cdecl ApiIsOpen(AmodMod mod)
        {
            return (Resolve(mod) != nullptr && g_window != nullptr && IsWindow(g_window)) ? 1 : 0;
        }

        int __cdecl ApiFindControl(AmodMod mod, const char* name)
        {
            if (Resolve(mod) == nullptr || name == nullptr || !GameModule::IsReady())
            {
                return 0;
            }

            struct MapEntry
            {
                uint32_t controlId;
                const char* section;
                const char* key;
            };

            const MapEntry* entries = static_cast<const MapEntry*>(
                GameModule::Resolve(GameAddress::kLauncherControlMap));
            const int count = *static_cast<const int*>(
                GameModule::Resolve(GameAddress::kLauncherControlMapCount));

            for (int i = 0; i < count; ++i)
            {
                if (entries[i].key != nullptr && strcmp(entries[i].key, name) == 0)
                {
                    return static_cast<int>(entries[i].controlId);
                }
            }

            return 0;
        }

        int __cdecl ApiSetText(AmodMod mod, int controlId, const char* text)
        {
            const HWND control = (Resolve(mod) != nullptr && text != nullptr)
                                 ? ControlWindow(controlId) : nullptr;
            return (control != nullptr && SetWindowTextA(control, text)) ? 1 : 0;
        }

        int __cdecl ApiGetText(AmodMod mod, int controlId, char* buffer, uint32_t size)
        {
            if (Resolve(mod) == nullptr || buffer == nullptr || size == 0)
            {
                return 0;
            }

            const HWND control = ControlWindow(controlId);
            if (control == nullptr)
            {
                return 0;
            }

            buffer[0] = '\0';
            GetWindowTextA(control, buffer, static_cast<int>(size));
            return 1;
        }

        int __cdecl ApiSetVisible(AmodMod mod, int controlId, int visible)
        {
            const HWND control = Resolve(mod) != nullptr ? ControlWindow(controlId) : nullptr;
            if (control == nullptr)
            {
                return 0;
            }

            ShowWindow(control, visible != 0 ? SW_SHOW : SW_HIDE);
            return 1;
        }

        int __cdecl ApiSetEnabled(AmodMod mod, int controlId, int enabled)
        {
            const HWND control = Resolve(mod) != nullptr ? ControlWindow(controlId) : nullptr;
            if (control == nullptr)
            {
                return 0;
            }

            EnableWindow(control, enabled != 0 ? TRUE : FALSE);
            return 1;
        }

        int __cdecl ApiSetCaption(AmodMod mod, const char* text)
        {
            if (Resolve(mod) == nullptr || text == nullptr || g_window == nullptr || !IsWindow(g_window))
            {
                return 0;
            }

            return SetWindowTextA(g_window, text) ? 1 : 0;
        }

        int __cdecl ApiSetBounds(AmodMod mod, int controlId, int x, int y, int width, int height)
        {
            const HWND control = Resolve(mod) != nullptr ? ControlWindow(controlId) : nullptr;
            if (control == nullptr)
            {
                return 0;
            }

            // Zero keeps the current value, which is the rule Dialog_MoveItem @0x004243a0
            // follows for the game's own dialog script.
            RECT bounds = {};
            GetWindowRect(control, &bounds);
            ScreenToClient(g_window, reinterpret_cast<LPPOINT>(&bounds.left));
            ScreenToClient(g_window, reinterpret_cast<LPPOINT>(&bounds.right));

            const int finalX = x != 0 ? x : bounds.left;
            const int finalY = y != 0 ? y : bounds.top;
            const int finalWidth = width != 0 ? width : bounds.right - bounds.left;
            const int finalHeight = height != 0 ? height : bounds.bottom - bounds.top;

            return MoveWindow(control, finalX, finalY, finalWidth, finalHeight, TRUE) ? 1 : 0;
        }

        int __cdecl ApiSetLogo(AmodMod mod, const void* bitmapFile, uint32_t size)
        {
            if (Resolve(mod) == nullptr || bitmapFile == nullptr || g_object == nullptr ||
                g_window == nullptr || !IsWindow(g_window))
            {
                return 0;
            }

            if (size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
            {
                return 0;
            }

            const uint8_t* bytes = static_cast<const uint8_t*>(bitmapFile);
            const BITMAPFILEHEADER* file = reinterpret_cast<const BITMAPFILEHEADER*>(bytes);
            if (file->bfType != 0x4D42 || file->bfOffBits >= size)
            {
                return 0;
            }

            const BITMAPINFO* info =
                reinterpret_cast<const BITMAPINFO*>(bytes + sizeof(BITMAPFILEHEADER));

            const HDC screen = GetDC(g_window);
            if (screen == nullptr)
            {
                return 0;
            }

            const HBITMAP bitmap = CreateDIBitmap(screen, &info->bmiHeader, CBM_INIT,
                                                  bytes + file->bfOffBits, info, DIB_RGB_COLORS);
            ReleaseDC(g_window, screen);

            if (bitmap == nullptr)
            {
                return 0;
            }

            uint8_t* const object = static_cast<uint8_t*>(g_object);
            HBITMAP* const slot =
                reinterpret_cast<HBITMAP*>(object + GameAddress::kLauncherLogoBitmapOffset);

            if (*slot != nullptr)
            {
                DeleteObject(*slot);
            }
            *slot = bitmap;

            // The game paints from a memory DC it creates when it first loads a logo; a mod
            // that replaces the logo before that happened has to provide one.
            HDC* const memoryDc = reinterpret_cast<HDC*>(object + GameAddress::kLauncherLogoDcOffset);
            if (*memoryDc == nullptr)
            {
                const HDC window = GetDC(g_window);
                *memoryDc = CreateCompatibleDC(window);
                ReleaseDC(g_window, window);
            }

            InvalidateRect(g_window, nullptr, TRUE);
            return 1;
        }

        void* __cdecl ApiGetWindow(AmodMod mod, uint32_t expectedGameBuild)
        {
            if (Resolve(mod) == nullptr || expectedGameBuild != AMOD_GAME_BUILD_AZANGARA_10)
            {
                return nullptr;
            }

            return (g_window != nullptr && IsWindow(g_window)) ? g_window : nullptr;
        }

        AmodLauncherApi g_table = {};

        bool InstallOne(InlineHook& hook, GameSymbol symbol, void* detour,
                        const uint8_t* prologue, size_t prologueLength)
        {
            void* const target = GameSymbols::Address(symbol);
            if (target == nullptr)
            {
                FORGE_LOG_ERROR("%s was not located, the launcher interface is not available",
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

    bool LauncherApi::Install()
    {
        if (g_initHook.IsInstalled())
        {
            return true;
        }

        if (!InstallOne(g_initHook, GameSymbol::LauncherOnInitDialog,
                        reinterpret_cast<void*>(&OnInitDialogDetour),
                        kLauncherOnInitDialogPrologue, sizeof(kLauncherOnInitDialogPrologue)) ||
            !InstallOne(g_commandHook, GameSymbol::LauncherOnCommand,
                        reinterpret_cast<void*>(&OnCommandDetour),
                        kLauncherOnCommandPrologue, sizeof(kLauncherOnCommandPrologue)) ||
            !InstallOne(g_modalHook, GameSymbol::DialogRunModal,
                        reinterpret_cast<void*>(&RunModalDetour),
                        kDialogRunModalPrologue, sizeof(kDialogRunModalPrologue)) ||
            !InstallOne(g_hideHook, GameSymbol::LauncherHideOptionalButtons,
                        reinterpret_cast<void*>(&HideOptionalButtonsDetour),
                        kLauncherHideOptionalButtonsPrologue,
                        sizeof(kLauncherHideOptionalButtonsPrologue)))
        {
            Uninstall();
            return false;
        }

        g_originalOnInit = reinterpret_cast<OnInitDialogRoutine>(g_initHook.Trampoline());
        g_originalOnCommand = reinterpret_cast<OnCommandRoutine>(g_commandHook.Trampoline());
        g_originalRunModal = reinterpret_cast<RunModalRoutine>(g_modalHook.Trampoline());
        g_originalHideOptional =
            reinterpret_cast<HideOptionalButtonsRoutine>(g_hideHook.Trampoline());

        g_table.structSize = sizeof(AmodLauncherApi);
        g_table.Subscribe = &ApiSubscribe;
        g_table.Unsubscribe = &ApiUnsubscribe;
        g_table.IsOpen = &ApiIsOpen;
        g_table.FindControl = &ApiFindControl;
        g_table.SetText = &ApiSetText;
        g_table.GetText = &ApiGetText;
        g_table.SetVisible = &ApiSetVisible;
        g_table.SetEnabled = &ApiSetEnabled;
        g_table.SetCaption = &ApiSetCaption;
        g_table.SetBounds = &ApiSetBounds;
        g_table.SetLogo = &ApiSetLogo;
        g_table.GetWindow = &ApiGetWindow;

        FORGE_LOG_INFO("Launcher interface installed");
        return true;
    }

    void LauncherApi::Uninstall()
    {
        g_initHook.Uninstall();
        g_commandHook.Uninstall();
        g_modalHook.Uninstall();
        g_hideHook.Uninstall();

        g_originalOnInit = nullptr;
        g_originalOnCommand = nullptr;
        g_originalRunModal = nullptr;
        g_originalHideOptional = nullptr;

        g_subscriptions.clear();
        g_object = nullptr;
        g_window = nullptr;
        g_table = {};
    }

    const AmodLauncherApi* LauncherApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void LauncherApi::ForgetMod(ModContext* owner)
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
