#include <windows.h>

#include "src/Api/ConfigApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        AmodConfigApi g_table = {};

        template <typename T>
        T Routine(uintptr_t address)
        {
            return reinterpret_cast<T>(GameModule::Resolve(address));
        }

        typedef void*(__fastcall* RegisterCvarRoutine)(void* config, void* unused, const char* name, const char* value, uint32_t flags);
        typedef void*(__fastcall* FindCvarRoutine)(void* config, void* unused, const char* name);
        typedef void(__fastcall* SetStringRoutine)(void* cvar, void* unused, const char* text);
        typedef void(__fastcall* SetFloatRoutine)(void* cvar, void* unused, float value);
        typedef void(__fastcall* WriteRoutine)(void* config, void* unused, const char* path, char force);

        // kConfig holds a POINTER to the object, not the object
        uint8_t* Config()
        {
            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kConfig));
            return at != nullptr ? static_cast<uint8_t*>(*at) : nullptr;
        }

        uint8_t* Find(const char* name)
        {
            uint8_t* const config = Config();
            const FindCvarRoutine find = Routine<FindCvarRoutine>(GameAddress::kCvarFind);

            if (config == nullptr || find == nullptr || name == nullptr || name[0] == '\0')
            {
                return nullptr;
            }

            return static_cast<uint8_t*>(find(config, nullptr, name));
        }

        const char* TextOf(const uint8_t* cvar)
        {
            const char* const text = cvar != nullptr
                ? reinterpret_cast<const char*>(Read<uintptr_t>(cvar, GameAddress::kCvarValueTextOffset))
                : nullptr;

            return text != nullptr ? text : "";
        }

        struct CvarWas
        {
            ModContext* owner;
            std::string name;
            std::string text;
        };

        std::vector<CvarWas> g_written;

        void Remember(ModContext* owner, const char* name, const uint8_t* cvar)
        {
            for (const CvarWas& held : g_written)
            {
                if (held.name == name)
                {
                    return;
                }
            }

            g_written.push_back(CvarWas{ owner, name, TextOf(cvar) });
        }

        void PutBack(const CvarWas& held)
        {
            uint8_t* const cvar = Find(held.name.c_str());
            const SetStringRoutine set = Routine<SetStringRoutine>(GameAddress::kCvarSetString);

            if (cvar != nullptr && set != nullptr)
            {
                set(cvar, nullptr, held.text.c_str());
            }
        }

        int __cdecl ApiHas(AmodMod mod, const char* name)
        {
            return (Caller(mod) != nullptr && Find(name) != nullptr) ? 1 : 0;
        }

        int __cdecl ApiGet(AmodMod mod, const char* name, AmodConfigKey* out)
        {
            const uint8_t* const cvar = Caller(mod) != nullptr ? Find(name) : nullptr;

            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Get wants somewhere to answer");
            }
            if (cvar == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no config key '%s'", name != nullptr ? name : "");
            }

            AmodConfigKey filled = {};
            filled.structSize = sizeof(filled);
            filled.name = reinterpret_cast<const char*>(Read<uintptr_t>(cvar, GameAddress::kCvarNameOffset));
            filled.value = TextOf(cvar);
            filled.fallback = reinterpret_cast<const char*>(Read<uintptr_t>(cvar, GameAddress::kCvarDefaultOffset));
            filled.number = Read<float>(cvar, GameAddress::kCvarFloatOffset);
            filled.flags = Read<uint32_t>(cvar, GameAddress::kCvarFlagsOffset);

            if (filled.name == nullptr)
            {
                filled.name = "";
            }
            if (filled.fallback == nullptr)
            {
                filled.fallback = "";
            }

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        const char* __cdecl ApiGetText(AmodMod mod, const char* name, const char* fallback)
        {
            const uint8_t* const cvar = Caller(mod) != nullptr ? Find(name) : nullptr;
            return cvar != nullptr ? TextOf(cvar) : (fallback != nullptr ? fallback : "");
        }

        float __cdecl ApiGetNumber(AmodMod mod, const char* name, float fallback)
        {
            const uint8_t* const cvar = Caller(mod) != nullptr ? Find(name) : nullptr;
            return cvar != nullptr ? Read<float>(cvar, GameAddress::kCvarFloatOffset) : fallback;
        }

        int __cdecl ApiSetText(AmodMod mod, const char* name, const char* value)
        {
            uint8_t* const cvar = Caller(mod) != nullptr ? Find(name) : nullptr;
            const SetStringRoutine set = Routine<SetStringRoutine>(GameAddress::kCvarSetString);

            if (cvar == nullptr || set == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no config key '%s' to set - Add it first", name != nullptr ? name : "");
            }

            Remember(Caller(mod), name, cvar);
            set(cvar, nullptr, value != nullptr ? value : "");
            return 1;
        }

        int __cdecl ApiSetNumber(AmodMod mod, const char* name, float value)
        {
            uint8_t* const cvar = Caller(mod) != nullptr ? Find(name) : nullptr;
            const SetFloatRoutine set = Routine<SetFloatRoutine>(GameAddress::kCvarSetFloat);

            if (cvar == nullptr || set == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no config key '%s' to set - Add it first",
                    name != nullptr ? name : "");
            }

            Remember(Caller(mod), name, cvar);
            set(cvar, nullptr, value);
            return 1;
        }

        int __cdecl ApiAdd(AmodMod mod, const char* name, const char* value, uint32_t flags)
        {
            uint8_t* const config = Caller(mod) != nullptr ? Config() : nullptr;
            const RegisterCvarRoutine add = Routine<RegisterCvarRoutine>(GameAddress::kConfigRegisterCvar);

            if (config == nullptr || add == nullptr || name == nullptr || name[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Add wants a name");
            }

            return add(config, nullptr, name, value != nullptr ? value : "", flags) != nullptr ? 1 : 0;
        }

        int __cdecl ApiCount(AmodMod mod)
        {
            const uint8_t* const config = Caller(mod) != nullptr ? Config() : nullptr;
            const uint8_t* walk = config != nullptr
                ? reinterpret_cast<const uint8_t*>(Read<uintptr_t>(config, GameAddress::kConfigCvarsOffset))
                : nullptr;

            int count = 0;

            // Bounded: a broken link would otherwise walk for ever
            while (walk != nullptr && Readable(walk, GameAddress::kCvarSize) && count < 0x4000)
            {
                ++count;
                walk = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(walk, GameAddress::kCvarNextOffset));
            }

            return count;
        }

        const char* __cdecl ApiNameAt(AmodMod mod, int index)
        {
            const uint8_t* const config = Caller(mod) != nullptr ? Config() : nullptr;
            const uint8_t* walk = config != nullptr
                ? reinterpret_cast<const uint8_t*>(Read<uintptr_t>(config, GameAddress::kConfigCvarsOffset))
                : nullptr;

            for (int at = 0; walk != nullptr && Readable(walk, GameAddress::kCvarSize); ++at)
            {
                if (at == index)
                {
                    const char* const name = reinterpret_cast<const char*>(
                        Read<uintptr_t>(walk, GameAddress::kCvarNameOffset));
                    return name != nullptr ? name : "";
                }

                walk = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(walk, GameAddress::kCvarNextOffset));
            }

            return "";
        }

        int __cdecl ApiSave(AmodMod mod, const char* path)
        {
            uint8_t* const config = Caller(mod) != nullptr ? Config() : nullptr;
            const WriteRoutine write = Routine<WriteRoutine>(GameAddress::kConfigWrite);

            if (config == nullptr || write == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the config object is not up yet");
            }

            // A null path makes the game build its own name, which is what it saves with normally
            write(config, nullptr, path, 1);
            return 1;
        }
    }

    bool ConfigApi::Install()
    {
        if (GameModule::Resolve(GameAddress::kConfig) == nullptr || Routine<FindCvarRoutine>(GameAddress::kCvarFind) == nullptr)
        {
            FORGE_LOG_ERROR("Config is not where it was expected, the config interface is not installed");
            return false;
        }

        g_table.structSize = sizeof(AmodConfigApi);
        g_table.Has = &ApiHas;
        g_table.Get = &ApiGet;
        g_table.GetText = &ApiGetText;
        g_table.GetNumber = &ApiGetNumber;
        g_table.SetText = &ApiSetText;
        g_table.SetNumber = &ApiSetNumber;
        g_table.Add = &ApiAdd;
        g_table.Count = &ApiCount;
        g_table.NameAt = &ApiNameAt;
        g_table.Save = &ApiSave;

        FORGE_LOG_INFO("Config interface installed");
        return true;
    }

    void ConfigApi::Uninstall()
    {
        for (const CvarWas& held : g_written)
        {
            PutBack(held);
        }

        g_written.clear();
        g_table = AmodConfigApi{};
    }

    void ConfigApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_written.begin(); it != g_written.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            PutBack(*it);
            it = g_written.erase(it);
        }
    }

    const AmodConfigApi* ConfigApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
