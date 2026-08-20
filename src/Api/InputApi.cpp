#include <windows.h>

#include "src/Api/InputApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Mods/ModStore.h"

#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace GameAddress;

        // Both RET 8 → __stdcall.
        typedef void(__stdcall* GetCursorRoutine)(float* x, float* y);
        typedef void(__stdcall* SetCursorRoutine)(float x, float y);

        AmodInputApi g_table = {};

        uint8_t* g_input = nullptr;
        GetCursorRoutine g_getCursor = nullptr;
        SetCursorRoutine g_setCursor = nullptr;

        constexpr int kJoystickFirst = static_cast<int>(kInputKeyRecordCount);
        constexpr int kKeyTotal = kJoystickFirst + static_cast<int>(kInputJoyRecordCount);

        typedef void*(__fastcall* FindCvarRoutine)(void* config, void* unused, const char* name);
        typedef void*(__fastcall* FindCommandRoutine)(void* table, void* unused, const char* name);
        typedef void(__stdcall* UnbindRoutine)(void* record);
        typedef void(__stdcall* BindCvarRoutine)(void* record, void* cvar, uint32_t held);
        typedef void(__stdcall* BindCommandRoutine)(void* record, void* command, int argc, const char** argv, uint32_t held);

        struct BindWas
        {
            ModContext* owner;
            int key;
            int kind;
            std::string target;
            std::vector<std::string> params;
            bool held;
        };

        std::vector<BindWas> g_rebound;
        std::vector<BindWas> g_whileWriting;

        uint8_t g_previous[kKeyTotal] = {};

        typedef SHORT(WINAPI* AsyncKeyRoutine)(int key);

        struct Forced
        {
            ModContext* owner;
            bool tap;
            bool asked;
            bool exclusive;
            int ticks;
        };

        AsyncKeyRoutine* g_asyncSlot = nullptr;
        AsyncKeyRoutine g_originalAsync = nullptr;
        Forced g_forced[kInputKeyCount] = {};
        int g_exclusive = 0;

        bool IsForced(int key)
        {
            return key >= 0 && key < static_cast<int>(kInputKeyCount) &&
                   g_forced[key].owner != nullptr;
        }

        // One counter rather than a walk of 255 entries: this is asked for every key every frame.
        void Force(int key, const Forced& wanted)
        {
            g_exclusive -= g_forced[key].exclusive ? 1 : 0;
            g_exclusive += wanted.exclusive ? 1 : 0;
            g_forced[key] = wanted;
        }

        bool Swallowing()
        {
            return g_input != nullptr && *(g_input + kInputSwallowingOffset) != 0;
        }

        SHORT WINAPI AsyncKeyDetour(int key)
        {
            if (Swallowing())
            {
                return g_originalAsync != nullptr ? g_originalAsync(key) : 0;
            }

            if (IsForced(key))
            {
                g_forced[key].asked = true;
                return static_cast<SHORT>(0x8000);
            }

            return (g_exclusive == 0 && g_originalAsync != nullptr) ? g_originalAsync(key) : 0;
        }

        bool SwapAsyncKey(AsyncKeyRoutine routine)
        {
            DWORD guard = 0;
            if (g_asyncSlot == nullptr ||
                VirtualProtect(g_asyncSlot, sizeof(void*), PAGE_READWRITE, &guard) == 0)
            {
                return false;
            }

            *g_asyncSlot = routine;

            DWORD ignored = 0;
            VirtualProtect(g_asyncSlot, sizeof(void*), guard, &ignored);
            return true;
        }

        void LetGo(int key)
        {
            Force(key, Forced{});
        }

        HWND g_window = nullptr;
        WNDPROC g_originalProc = nullptr;
        int g_wheel = 0;
        int g_wheelFrame = 0;

        LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            if (message == WM_MOUSEWHEEL)
            {
                g_wheel += GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            }
            return CallWindowProcA(g_originalProc, window, message, wparam, lparam);
        }

        void AttachToWindow()
        {
            HWND* const slot = static_cast<HWND*>(GameModule::Resolve(kGameWindow));
            HWND const window = slot != nullptr ? *slot : nullptr;

            if (window == nullptr || window == g_window)
            {
                return;
            }

            // Windows reuses HWND values; a recreated window may already use our proc, causing recursive chaining.
            const LONG_PTR standing = GetWindowLongPtrA(window, GWLP_WNDPROC);

            if (standing == reinterpret_cast<LONG_PTR>(&WindowProc))
            {
                g_window = window;
                return;
            }

            const LONG_PTR previous = SetWindowLongPtrA(window, GWLP_WNDPROC,
                                                        reinterpret_cast<LONG_PTR>(&WindowProc));
            if (previous == 0)
            {
                return;
            }

            g_originalProc = reinterpret_cast<WNDPROC>(previous);
            g_window = window;
            FORGE_LOG_INFO("The game's window is watched for the mouse wheel, which the game "
                           "itself does not read");
        }

        void DetachFromWindow()
        {
            if (g_window != nullptr && g_originalProc != nullptr)
            {
                SetWindowLongPtrA(g_window, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(g_originalProc));
            }

            g_window = nullptr;
            g_originalProc = nullptr;
        }

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        bool KeyInRange(int key)
        {
            return key >= 0 && key < kKeyTotal;
        }

        // The joystick keys keep their up and down apart from the keyboard's.
        const uint8_t* StateOf(int key)
        {
            if (g_input == nullptr || !KeyInRange(key))
            {
                return nullptr;
            }

            return key < kJoystickFirst
                ? g_input + kInputKeyStateOffset + key
                : g_input + kInputJoyStateOffset + (key - kJoystickFirst);
        }

        bool DownNow(int key)
        {
            const uint8_t* const state = StateOf(key);
            return state != nullptr && *state != 0;
        }

        uint8_t* RecordOf(int key)
        {
            if (g_input == nullptr || !KeyInRange(key))
            {
                return nullptr;
            }

            return key < kJoystickFirst
                ? g_input + kInputKeyRecordsOffset + key * kInputKeyRecordStride
                : g_input + kInputJoyRecordsOffset + (key - kJoystickFirst) * kInputKeyRecordStride;
        }

        template <typename T>
        T Field(const uint8_t* record, uintptr_t offset)
        {
            T value;
            memcpy(&value, record + offset, sizeof(T));
            return value;
        }

        template <typename T>
        T Routine(uintptr_t address)
        {
            return reinterpret_cast<T>(GameModule::Resolve(address));
        }

        const char* BoundName(const uint8_t* record)
        {
            const int kind = Field<int>(record, kKeyBindKindOffset);
            const uint8_t* const target = Field<const uint8_t*>(record, kKeyBindTargetOffset);

            if (target == nullptr)
            {
                return nullptr;
            }
            if (kind == kKeyBindCvar)
            {
                return Field<const char*>(target, kCvarNameOffset);
            }
            if (kind == kKeyBindCommand)
            {
                return Field<const char*>(target, kCmdCommandNameOffset);
            }
            return nullptr;
        }

        bool BoundHeld(const uint8_t* record)
        {
            const int kind = Field<int>(record, kKeyBindKindOffset);
            if (kind == kKeyBindCvar)
            {
                return *(record + kKeyCvarHeldOffset) != 0;
            }
            return kind == kKeyBindCommand && *(record + kKeyCommandHeldOffset) != 0;
        }

        const char* ParamAt(const uint8_t* record, int param)
        {
            if (Field<int>(record, kKeyBindKindOffset) != kKeyBindCommand || param < 0 ||
                param >= Field<int>(record, kKeyParamCountOffset))
            {
                return nullptr;
            }

            const char** const params = Field<const char**>(record, kKeyParamsOffset);
            return params != nullptr ? params[param] : nullptr;
        }

        void* FindCvar(const char* name)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(kConfig));
            const FindCvarRoutine find = Routine<FindCvarRoutine>(kCvarFind);

            return (at != nullptr && *at != nullptr && find != nullptr && name != nullptr)
                ? find(*at, nullptr, name)
                : nullptr;
        }

        void* FindCommand(const char* name)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(kConfig));
            const FindCommandRoutine find = Routine<FindCommandRoutine>(kCmdFind);

            if (at == nullptr || *at == nullptr || find == nullptr || name == nullptr)
            {
                return nullptr;
            }

            uint8_t* const table = static_cast<uint8_t*>(*at) + kConfigCommandsOffset;
            const uint8_t* const node = static_cast<uint8_t*>(find(table, nullptr, name));

            return node != nullptr ? Field<void*>(node, kCmdNodeCommandOffset) : nullptr;
        }

        BindWas Capture(int key)
        {
            BindWas held = {};
            held.key = key;

            const uint8_t* const record = RecordOf(key);
            if (record == nullptr)
            {
                return held;
            }

            held.kind = Field<int>(record, kKeyBindKindOffset);
            held.held = BoundHeld(record);

            const char* const name = BoundName(record);
            if (name != nullptr)
            {
                held.target = name;
            }

            if (held.kind == kKeyBindCommand)
            {
                const int count = Field<int>(record, kKeyParamCountOffset);
                for (int i = 0; i < count; ++i)
                {
                    const char* const param = ParamAt(record, i);
                    held.params.push_back(param != nullptr ? param : "");
                }
            }

            return held;
        }

        const char* KindName(int kind)
        {
            switch (kind)
            {
            case kKeyBindCvar:    return "cvar";
            case kKeyBindCommand: return "command";
            }
            return "none";
        }

        StoreRecord AsRecord(int key, const BindWas& held)
        {
            StoreRecord record;
            record.kind = "input";
            record.key = std::to_string(key);
            record.values.push_back(KindName(held.kind));
            record.values.push_back(held.target);
            record.values.push_back(held.held ? "1" : "0");

            for (const std::string& param : held.params)
            {
                record.values.push_back(param);
            }

            return record;
        }

        BindWas FromRecord(const StoreRecord& record)
        {
            BindWas held = {};
            held.key = atoi(record.key.c_str());

            if (record.values.size() >= 3)
            {
                held.kind = record.values[0] == "cvar" ? kKeyBindCvar
                    : record.values[0] == "command" ? kKeyBindCommand : kKeyBindNone;
                held.target = record.values[1];
                held.held = record.values[2] == "1";

                for (size_t i = 3; i < record.values.size(); ++i)
                {
                    held.params.push_back(record.values[i]);
                }
            }

            return held;
        }

        void Remember(ModContext* owner, int key)
        {
            for (const BindWas& held : g_rebound)
            {
                if (held.owner == owner && held.key == key)
                {
                    return;
                }
            }

            if (RecordOf(key) == nullptr)
            {
                return;
            }

            BindWas held = Capture(key);
            held.owner = owner;
            g_rebound.push_back(held);
        }

        bool ApplyCvar(int key, const char* cvar, bool held)
        {
            uint8_t* const record = RecordOf(key);
            void* const node = FindCvar(cvar);
            const BindCvarRoutine bind = Routine<BindCvarRoutine>(kInputBindCvar);

            if (record == nullptr || node == nullptr || bind == nullptr)
            {
                return false;
            }

            bind(record, node, held ? 1 : 0);
            return true;
        }

        // The game keeps the token that named the command as the first parameter, '+' and all.
        bool ApplyCommand(int key, const char* command, int argc, const char** argv, bool held)
        {
            uint8_t* const record = RecordOf(key);
            void* const target = FindCommand(command);
            const BindCommandRoutine bind = Routine<BindCommandRoutine>(kInputBindCommand);

            if (record == nullptr || target == nullptr || bind == nullptr)
            {
                return false;
            }

            std::string first = held ? "+" : "";
            first += command;

            std::vector<const char*> params;
            params.push_back(first.c_str());
            for (int i = 0; i < argc && argv != nullptr; ++i)
            {
                params.push_back(argv[i] != nullptr ? argv[i] : "");
            }

            bind(record, target, static_cast<int>(params.size()), params.data(), held ? 1 : 0);
            return true;
        }

        void Apply(const BindWas& held)
        {
            uint8_t* const record = RecordOf(held.key);
            const UnbindRoutine unbind = Routine<UnbindRoutine>(kInputUnbindKey);

            if (record == nullptr || unbind == nullptr)
            {
                return;
            }

            unbind(record);

            if (held.kind == kKeyBindCvar)
            {
                ApplyCvar(held.key, held.target.c_str(), held.held);
                return;
            }
            if (held.kind != kKeyBindCommand)
            {
                return;
            }

            // The first parameter is the token, which the command is named by and not given.
            std::vector<const char*> extra;
            for (size_t i = 1; i < held.params.size(); ++i)
            {
                extra.push_back(held.params[i].c_str());
            }

            ApplyCommand(held.key, held.target.c_str(), static_cast<int>(extra.size()), extra.empty() ? nullptr : extra.data(), held.held);
        }

        void __cdecl RememberFrame()
        {
            AttachToWindow();

            g_wheelFrame = g_wheel;
            g_wheel = 0;

            if (g_input != nullptr)
            {
                memcpy(g_previous, g_input + kInputKeyStateOffset, kInputKeyCount);
                memcpy(g_previous + kJoystickFirst, g_input + kInputJoyStateOffset,
                       kInputJoyRecordCount);
            }

            if (Swallowing())
            {
                return;
            }

            for (int key = 0; key < static_cast<int>(kInputKeyCount); ++key)
            {
                Forced& forced = g_forced[key];

                if (forced.owner != nullptr && forced.tap && (forced.asked || ++forced.ticks >= 2))
                {
                    LetGo(key);
                }
            }
        }

        int __cdecl ApiIsKeyDown(AmodMod mod, int key)
        {
            return (Resolve(mod) != nullptr && DownNow(key)) ? 1 : 0;
        }

        int __cdecl ApiWasKeyPressed(AmodMod mod, int key)
        {
            if (Resolve(mod) == nullptr || !KeyInRange(key))
            {
                return 0;
            }
            return (DownNow(key) && g_previous[key] == 0) ? 1 : 0;
        }

        int __cdecl ApiWasKeyReleased(AmodMod mod, int key)
        {
            if (Resolve(mod) == nullptr || !KeyInRange(key))
            {
                return 0;
            }
            return (!DownNow(key) && g_previous[key] != 0) ? 1 : 0;
        }

        void __cdecl ApiGetCursor(AmodMod mod, float* x, float* y)
        {
            if (Resolve(mod) == nullptr || g_getCursor == nullptr)
            {
                return;
            }

            float cursorX = 0.0f;
            float cursorY = 0.0f;
            g_getCursor(&cursorX, &cursorY);

            if (x != nullptr)
            {
                *x = cursorX;
            }
            if (y != nullptr)
            {
                *y = cursorY;
            }
        }

        void __cdecl ApiSetCursor(AmodMod mod, float x, float y)
        {
            if (Resolve(mod) != nullptr && g_setCursor != nullptr)
            {
                g_setCursor(x, y);
            }
        }

        int __cdecl ApiIsCursorInWindow(AmodMod mod)
        {
            return (Resolve(mod) != nullptr && g_input != nullptr &&
                    *(g_input + kInputCursorInWindowOffset) != 0) ? 1 : 0;
        }

        int __cdecl ApiIsSwallowed(AmodMod mod)
        {
            return (Resolve(mod) != nullptr && g_input != nullptr &&
                    *(g_input + kInputSwallowingOffset) != 0) ? 1 : 0;
        }

        int __cdecl ApiWheelDelta(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? g_wheelFrame : 0;
        }

        int __cdecl ApiKeyCount(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? kKeyTotal : 0;
        }

        int __cdecl ApiGetKey(AmodMod mod, int key, AmodKeyInfo* out)
        {
            const uint8_t* const record = RecordOf(key);
            if (Resolve(mod) == nullptr || out == nullptr || record == nullptr)
            {
                return 0;
            }

            AmodKeyInfo filled = {};
            filled.structSize = sizeof(filled);
            filled.name = Field<const char*>(record, kKeyNameOffset);
            filled.isJoystick = key >= kJoystickFirst ? 1 : 0;
            filled.bind = Field<int>(record, kKeyBindKindOffset);
            filled.target = BoundName(record);
            filled.held = BoundHeld(record) ? 1 : 0;
            filled.paramCount = filled.bind == kKeyBindCommand
                ? Field<int>(record, kKeyParamCountOffset)
                : 0;

            const uint32_t wanted = out->structSize;
            memcpy(out, &filled, wanted < sizeof(filled) ? wanted : sizeof(filled));
            out->structSize = wanted;
            return 1;
        }

        int __cdecl ApiFindKey(AmodMod mod, const char* name)
        {
            if (Resolve(mod) == nullptr || name == nullptr || name[0] == '\0')
            {
                return -1;
            }

            for (int key = 0; key < kKeyTotal; ++key)
            {
                const uint8_t* const record = RecordOf(key);
                const char* const held = record != nullptr
                    ? Field<const char*>(record, kKeyNameOffset)
                    : nullptr;

                if (held != nullptr && _stricmp(held, name) == 0)
                {
                    return key;
                }
            }

            return -1;
        }

        const char* __cdecl ApiGetKeyParam(AmodMod mod, int key, int param)
        {
            const uint8_t* const record = RecordOf(key);
            return (Resolve(mod) != nullptr && record != nullptr) ? ParamAt(record, param) : nullptr;
        }

        int __cdecl ApiBindCommand(AmodMod mod, int key, const char* command, int argc, const char** argv, int held)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || RecordOf(key) == nullptr || command == nullptr)
            {
                return 0;
            }

            Remember(context, key);

            const UnbindRoutine unbind = Routine<UnbindRoutine>(kInputUnbindKey);
            if (unbind != nullptr)
            {
                unbind(RecordOf(key));
            }

            return ApplyCommand(key, command, argc, argv, held != 0) ? 1 : 0;
        }

        int __cdecl ApiBindCvar(AmodMod mod, int key, const char* cvar, int held)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || RecordOf(key) == nullptr || cvar == nullptr)
            {
                return 0;
            }

            Remember(context, key);

            return ApplyCvar(key, cvar, held != 0) ? 1 : 0;
        }

        int __cdecl ApiUnbind(AmodMod mod, int key)
        {
            ModContext* const context = Resolve(mod);
            uint8_t* const record = RecordOf(key);
            const UnbindRoutine unbind = Routine<UnbindRoutine>(kInputUnbindKey);

            if (context == nullptr || record == nullptr || unbind == nullptr)
            {
                return 0;
            }

            Remember(context, key);
            unbind(record);
            return 1;
        }

        int __cdecl ApiKeep(AmodMod mod, int key, int keep, int protect)
        {
            ModContext* const context = Resolve(mod);

            if (context == nullptr || RecordOf(key) == nullptr)
            {
                return 0;
            }

            if (keep == 0)
            {
                ModStore::Remove(context, "input", std::to_string(key).c_str());
                return 1;
            }

            ModStore::Put(context, AsRecord(key, Capture(key)));
            ModStore::Protect(context, "input", std::to_string(key).c_str(), protect != 0);
            return 1;
        }

        int __cdecl ApiPressKey(AmodMod mod, int key)
        {
            ModContext* const context = Resolve(mod);

            if (context == nullptr || g_originalAsync == nullptr || key < 0 || key >= static_cast<int>(kInputKeyCount))
            {
                return 0;
            }

            Force(key, Forced{ context, true, false, false, 0 });
            return 1;
        }

        int __cdecl ApiHoldKey(AmodMod mod, int key, int down, int exclusive)
        {
            ModContext* const context = Resolve(mod);

            if (context == nullptr || g_originalAsync == nullptr || key < 0 || key >= static_cast<int>(kInputKeyCount))
            {
                return 0;
            }

            if (down == 0)
            {
                LetGo(key);
                return 1;
            }

            Force(key, Forced{ context, false, false, exclusive != 0, 0 });
            return 1;
        }

        int __cdecl ApiIsKeyForced(AmodMod mod, int key)
        {
            return (Resolve(mod) != nullptr && IsForced(key)) ? 1 : 0;
        }

        int __cdecl ApiHasJoystick(AmodMod mod)
        {
            return (Resolve(mod) != nullptr && g_input != nullptr &&
                    *(g_input + kInputJoyCapturedOffset) != 0) ? 1 : 0;
        }
    }

    bool InputApi::Install()
    {
        g_input = static_cast<uint8_t*>(GameModule::Resolve(kInput));
        g_getCursor = reinterpret_cast<GetCursorRoutine>(GameModule::Resolve(kInputGetCursor));
        g_setCursor = reinterpret_cast<SetCursorRoutine>(GameModule::Resolve(kInputSetCursor));

        if (g_input == nullptr || g_getCursor == nullptr || g_setCursor == nullptr)
        {
            FORGE_LOG_WARN("The game's input was not located, so the input interface is not "
                           "offered");
            return false;
        }

        FrameApi::AddInternalTick(&RememberFrame, true);

        g_asyncSlot = static_cast<AsyncKeyRoutine*>(GameModule::Resolve(kGetAsyncKeyStateSlot));
        g_originalAsync = g_asyncSlot != nullptr ? *g_asyncSlot : nullptr;

        if (g_originalAsync == nullptr || !SwapAsyncKey(&AsyncKeyDetour))
        {
            g_originalAsync = nullptr;
            FORGE_LOG_WARN("The keyboard poll was not taken, so a mod cannot press a key on the player's behalf");
        }

        g_table.structSize = sizeof(AmodInputApi);
        g_table.IsKeyDown = &ApiIsKeyDown;
        g_table.WasKeyPressed = &ApiWasKeyPressed;
        g_table.WasKeyReleased = &ApiWasKeyReleased;
        g_table.GetCursor = &ApiGetCursor;
        g_table.SetCursor = &ApiSetCursor;
        g_table.IsCursorInWindow = &ApiIsCursorInWindow;
        g_table.IsSwallowed = &ApiIsSwallowed;
        g_table.WheelDelta = &ApiWheelDelta;
        g_table.KeyCount = &ApiKeyCount;
        g_table.GetKey = &ApiGetKey;
        g_table.FindKey = &ApiFindKey;
        g_table.GetKeyParam = &ApiGetKeyParam;
        g_table.BindCommand = &ApiBindCommand;
        g_table.BindCvar = &ApiBindCvar;
        g_table.Unbind = &ApiUnbind;
        g_table.HasJoystick = &ApiHasJoystick;
        g_table.Keep = &ApiKeep;
        g_table.PressKey = &ApiPressKey;
        g_table.HoldKey = &ApiHoldKey;
        g_table.IsKeyForced = &ApiIsKeyForced;

        FORGE_LOG_INFO("Input interface installed");
        return true;
    }

    void InputApi::Uninstall()
    {
        DetachFromWindow();

        if (g_originalAsync != nullptr)
        {
            SwapAsyncKey(g_originalAsync);
        }
        g_asyncSlot = nullptr;
        g_originalAsync = nullptr;
        memset(g_forced, 0, sizeof(g_forced));
        g_exclusive = 0;

        g_wheel = 0;
        g_wheelFrame = 0;

        g_input = nullptr;
        g_getCursor = nullptr;
        g_setCursor = nullptr;
        memset(g_previous, 0, sizeof(g_previous));
        g_rebound.clear();
        g_table = AmodInputApi{};
    }

    void InputApi::ReplayStore(ModContext* owner)
    {
        for (const StoreRecord& kept : ModStore::Records(owner, "input"))
        {
            const BindWas held = FromRecord(kept);
            if (RecordOf(held.key) == nullptr)
            {
                continue;
            }

            Remember(owner, held.key);
            Apply(held);
        }
    }

    void InputApi::ForgetMod(ModContext* owner)
    {
        for (int key = 0; key < static_cast<int>(kInputKeyCount); ++key)
        {
            if (g_forced[key].owner == owner)
            {
                LetGo(key);
            }
        }

        for (const StoreRecord& kept : ModStore::Records(owner, "input"))
        {
            const int key = atoi(kept.key.c_str());
            if (RecordOf(key) != nullptr)
            {
                ModStore::Put(owner, AsRecord(key, Capture(key)));
            }
        }

        for (auto it = g_rebound.rbegin(); it != g_rebound.rend(); ++it)
        {
            if (it->owner == owner)
            {
                Apply(*it);
            }
        }

        for (auto it = g_rebound.begin(); it != g_rebound.end();)
        {
            it = it->owner == owner ? g_rebound.erase(it) : ++it;
        }
    }

    void InputApi::BeforeConfigWrite()
    {
        g_whileWriting.clear();

        for (const BindWas& held : g_rebound)
        {
            g_whileWriting.push_back(Capture(held.key));
            Apply(held);
        }
    }

    void InputApi::AfterConfigWrite()
    {
        for (const BindWas& held : g_whileWriting)
        {
            Apply(held);
        }
        g_whileWriting.clear();
    }

    const AmodInputApi* InputApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
