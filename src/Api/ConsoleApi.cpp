#include "pch.h"

#include "src/Api/ConsoleApi.h"

#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"

#include <map>
#include <set>
#include <string>

namespace Forge
{
    namespace
    {
        typedef void(__cdecl* ConsoleWriteRoutine)(int level, const char* fmt, ...);
        typedef void(__fastcall* CmdRegisterRoutine)(void* config, void* unused,
                                                     const char* name, void* handler);
        typedef void(__fastcall* ConsoleExecuteRoutine)(void* config, void* unused, const char* line);

        // The signature of a command handler the game calls: the words of the line.
        typedef void(__cdecl* GameCommandHandler)(int argc, char** argv);

        ConsoleWriteRoutine g_write = nullptr;
        CmdRegisterRoutine g_cmdRegister = nullptr;
        ConsoleExecuteRoutine g_execute = nullptr;

        AmodConsoleApi g_table = {};

        struct Command
        {
            ModContext* owner;
            AmodConsoleCommand handler;
            void* user;
        };

        // All mod commands share one game handler; argv[0] selects the owner from this map.
        std::map<std::string, Command> g_commands;

        // Names already in the game's table. The console does not exist until the game starts
        // (a mod loads while the launcher is up), so early commands wait here for PumpPending.
        std::set<std::string> g_gameRegistered;

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        void* Config()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kConfig));
            return slot != nullptr ? *slot : nullptr;
        }

        // The single handler the game calls for every mod command; argv[0] names the owner.
        void __cdecl Dispatch(int argc, char** argv)
        {
            if (argc < 1 || argv == nullptr || argv[0] == nullptr)
            {
                return;
            }

            const auto found = g_commands.find(argv[0]);
            if (found == g_commands.end())
            {
                if (g_write != nullptr)
                {
                    g_write(1, "%s is not available: the mod that added it is gone\n", argv[0]);
                }
                return;
            }

            const Command command = found->second;
            command.handler(argc, const_cast<const char**>(argv), command.user);
        }

        void __cdecl ApiPrint(AmodMod mod, int level, const char* text)
        {
            if (Resolve(mod) == nullptr || g_write == nullptr || text == nullptr)
            {
                return;
            }
            g_write(level, "%s", text);
        }

        // Registers a name with the game's table once the console exists.
        bool EnsureGameRegistered(const std::string& name)
        {
            if (g_gameRegistered.count(name) != 0)
            {
                return true;
            }

            void* const config = Config();
            if (config == nullptr || g_cmdRegister == nullptr)
            {
                return false;
            }

            g_cmdRegister(config, nullptr, name.c_str(), reinterpret_cast<void*>(&Dispatch));
            g_gameRegistered.insert(name);
            return true;
        }

        // Each frame, hands over commands that were added before the console existed.
        void __cdecl PumpPending()
        {
            if (Config() == nullptr)
            {
                return;
            }
            for (const auto& entry : g_commands)
            {
                EnsureGameRegistered(entry.first);
            }
        }

        int __cdecl ApiAddCommand(AmodMod mod, const char* name, AmodConsoleCommand handler, void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || name == nullptr || name[0] == '\0' || handler == nullptr ||
                g_cmdRegister == nullptr)
            {
                return 0;
            }

            g_commands[name] = Command{ context, handler, user };
            EnsureGameRegistered(name); // or later, on a frame, if the console is not up yet
            return 1;
        }

        void __cdecl ApiRemoveCommand(AmodMod mod, const char* name)
        {
            // The game has no unregister, so the name stays in its table; dropping it from the
            // owner map is what stops it, and the dispatch then reports it gone.
            if (Resolve(mod) != nullptr && name != nullptr)
            {
                g_commands.erase(name);
            }
        }

        void __cdecl ApiExecute(AmodMod mod, const char* line)
        {
            if (Resolve(mod) == nullptr || g_execute == nullptr || line == nullptr)
            {
                return;
            }
            void* const config = Config();
            if (config != nullptr)
            {
                g_execute(config, nullptr, line);
            }
        }
    }

    bool ConsoleApi::Install()
    {
        g_write = reinterpret_cast<ConsoleWriteRoutine>(
            GameSymbols::Address(GameSymbol::GameConsoleWrite));
        g_cmdRegister = reinterpret_cast<CmdRegisterRoutine>(
            GameSymbols::Address(GameSymbol::CmdRegister));
        g_execute = reinterpret_cast<ConsoleExecuteRoutine>(
            GameSymbols::Address(GameSymbol::ConsoleExecute));

        if (g_write == nullptr || g_cmdRegister == nullptr || g_execute == nullptr)
        {
            FORGE_LOG_WARN("The console routines were not all located, so the console "
                           "interface is not offered");
            return false;
        }

        g_table.structSize = sizeof(AmodConsoleApi);
        g_table.Print = &ApiPrint;
        g_table.AddCommand = &ApiAddCommand;
        g_table.RemoveCommand = &ApiRemoveCommand;
        g_table.Execute = &ApiExecute;

        // The console does not exist yet - the game builds it after the launcher - so a
        // per-frame pump hands over commands added early, once it is there.
        FrameApi::AddInternalTick(&PumpPending);

        FORGE_LOG_INFO("Console interface installed");
        return true;
    }

    const AmodConsoleApi* ConsoleApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void ConsoleApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_commands.begin(); it != g_commands.end();)
        {
            it = it->second.owner == owner ? g_commands.erase(it) : ++it;
        }
    }
}
