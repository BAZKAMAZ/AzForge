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
        typedef void*(__fastcall* CmdFindRoutine)(void* table, void* unused, const char* name);
        typedef void(__cdecl* FreeTrackedRoutine)(void* block);

        // The signature of a command handler the game calls: the words of the line.
        typedef void(__cdecl* GameCommandHandler)(int argc, char** argv);

        ConsoleWriteRoutine g_write = nullptr;
        CmdRegisterRoutine g_cmdRegister = nullptr;
        ConsoleExecuteRoutine g_execute = nullptr;
        CmdFindRoutine g_cmdFind = nullptr;
        FreeTrackedRoutine g_freeTracked = nullptr;

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

        // The game's command table: 256 buckets hanging off the config singleton.
        uint32_t* CommandTable()
        {
            void* const config = Config();
            return config != nullptr
                       ? reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(config) +
                                                     GameAddress::kConfigCommandsOffset)
                       : nullptr;
        }

        uint32_t Field(void* node, uintptr_t offset)
        {
            return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(node) + offset);
        }

        void SetField(void* node, uintptr_t offset, uint32_t value)
        {
            *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(node) + offset) = value;
        }

        // Takes one command back out of the game's table. Nothing in the game does this, so it
        // is done here: unlink the node from its bucket and give back what registering it
        // allocated. Mem_FreeTracked tests its own header magic, so a pointer that is not one
        // of its blocks is refused rather than turned into a corrupted heap.
        void UnregisterFromGame(const std::string& name)
        {
            uint32_t* const table = CommandTable();
            void* const node = (table != nullptr && g_cmdFind != nullptr)
                                   ? g_cmdFind(table, nullptr, name.c_str())
                                   : nullptr;
            if (node == nullptr)
            {
                return;
            }

            void* const previous = reinterpret_cast<void*>(Field(node, GameAddress::kCmdNodePrevOffset));
            const uint32_t next = Field(node, GameAddress::kCmdNodeNextOffset);

            if (previous != nullptr)
            {
                SetField(previous, GameAddress::kCmdNodeNextOffset, next);
            }
            else
            {
                // The head of its bucket. Which bucket that is takes the game's hash to work
                // out, so it is looked for instead - there are only 256 of them.
                for (size_t bucket = 0; bucket < GameAddress::kCmdBuckets; ++bucket)
                {
                    if (table[bucket] == reinterpret_cast<uint32_t>(node))
                    {
                        table[bucket] = next;
                        break;
                    }
                }
            }

            if (next != 0)
            {
                SetField(reinterpret_cast<void*>(next), GameAddress::kCmdNodePrevOffset,
                         reinterpret_cast<uint32_t>(previous));
            }

            if (g_freeTracked != nullptr)
            {
                void* const command = reinterpret_cast<void*>(Field(node, GameAddress::kCmdNodeCommandOffset));
                if (command != nullptr)
                {
                    g_freeTracked(reinterpret_cast<void*>(Field(command, GameAddress::kCmdCommandNameOffset)));
                    g_freeTracked(command);
                }
                g_freeTracked(reinterpret_cast<void*>(Field(node, GameAddress::kCmdNodeNameOffset)));
                g_freeTracked(node);
            }

            g_gameRegistered.erase(name);
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

        // Neither is signature-resolved: they are only ever reached from here, and the loader
        // has already proved by the symbols above that this is the build they belong to.
        g_cmdFind = reinterpret_cast<CmdFindRoutine>(GameModule::Resolve(GameAddress::kCmdFind));
        g_freeTracked = reinterpret_cast<FreeTrackedRoutine>(
            GameSymbols::Address(GameSymbol::MemFreeTracked));

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

    void ConsoleApi::Uninstall()
    {
        // Taken out of the game's table rather than left in it: Dispatch lives in AzForge, and
        // a name still standing after the loader has gone would call into an unmapped page.
        for (const auto& entry : g_commands)
        {
            UnregisterFromGame(entry.first);
        }

        g_commands.clear();
        g_table = {};

        // The console routines point into game.exe rather than into AzForge, so they are left
        // as they are; g_gameRegistered is emptied by the unregistering above.
    }

    const AmodConsoleApi* ConsoleApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void ConsoleApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_commands.begin(); it != g_commands.end();)
        {
            if (it->second.owner != owner)
            {
                ++it;
                continue;
            }

            UnregisterFromGame(it->first);
            FORGE_LOG_INFO("Console command '%s' was taken out of the game's table",
                           it->first.c_str());
            it = g_commands.erase(it);
        }
    }
}
