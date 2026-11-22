#include "src/Api/ConsoleApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"

#include <stdlib.h>
#include <map>
#include <set>
#include <string>

namespace Forge
{
    namespace
    {
        typedef void(__cdecl* ConsoleWriteRoutine)(int level, const char* fmt, ...);
        typedef void(__fastcall* CmdRegisterRoutine)(void* config, void* unused, const char* name, void* handler);
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

        std::map<std::string, Command> g_commands;
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
                       ? reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(config) + GameAddress::kConfigCommandsOffset)
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
                SetField(reinterpret_cast<void*>(next), GameAddress::kCmdNodePrevOffset, reinterpret_cast<uint32_t>(previous));
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

            uint32_t* const table = CommandTable();
            const int* const taken = (table != nullptr && g_cmdFind != nullptr)
                ? static_cast<const int*>(g_cmdFind(table, nullptr, name.c_str()))
                : nullptr;

            if (taken != nullptr && *taken != 0)
            {
                FORGE_LOG_WARN("The console already answers to '%s', so a mod's command of that "
                    "name will not run - pick another", name.c_str());
                g_gameRegistered.insert(name);
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
            if (Resolve(mod) != nullptr && name != nullptr)
            {
                g_commands.erase(name);
            }
        }

        // nsConsole is a static of its own at kConsole, and the line it shows is an
        // nsEditString: 256 bytes of text then m_cursorPos. Input_Update reads the open flag
        // off this object too, which is the only honest way to know whether the console is down.
        uint8_t* ConsoleObject()
        {
            return static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kConsole));
        }

        int __cdecl ApiIsOpen(AmodMod mod)
        {
            uint8_t* const console = Resolve(mod) != nullptr ? ConsoleObject() : nullptr;

            return (console != nullptr && console[GameAddress::kConsoleOpenOffset] != 0) ? 1 : 0;
        }

        void __cdecl ApiSetOpen(AmodMod mod, int open)
        {
            uint8_t* const console = Resolve(mod) != nullptr ? ConsoleObject() : nullptr;

            if (console != nullptr)
            {
                console[GameAddress::kConsoleOpenOffset] = open != 0 ? 1 : 0;
            }
        }

        void __cdecl ApiSetLine(AmodMod mod, const char* text)
        {
            uint8_t* const console = Resolve(mod) != nullptr ? ConsoleObject() : nullptr;

            if (console == nullptr || text == nullptr)
            {
                return;
            }

            char* const line = reinterpret_cast<char*>(console + GameAddress::kConsoleEditOffset);
            int at = 0;

            for (; text[at] != '\0' && at < 255; ++at)
            {
                line[at] = text[at];
            }

            line[at] = '\0';
            memcpy(console + GameAddress::kConsoleCaretOffset, &at, sizeof(at));
        }

        // com_time_scale 0 stops the game's clock dead, and the game does not survive it. A
        // mod asking for zero wants the world held still, not a crash, so the smallest value
        // that still ticks is handed over instead. Said out loud in the log the first time,
        // because a loader that quietly runs something other than what it was given is the
        // worst kind to debug.
        bool TimeScaleZero(const char* line, std::string& fixed)
        {
            static const char kKey[] = "com_time_scale";
            const size_t keyLength = sizeof(kKey) - 1;

            if (_strnicmp(line, kKey, keyLength) != 0)
            {
                return false;
            }

            const char* value = line + keyLength;

            while (*value == ' ' || *value == '\t')
            {
                ++value;
            }

            if (*value == '\0' || atof(value) != 0.0)
            {
                return false;       // reading it, or setting it to something that ticks
            }

            fixed = std::string(kKey) + " 0.0001";
            return true;
        }

        void __cdecl ApiExecute(AmodMod mod, const char* line)
        {
            if (Resolve(mod) == nullptr || g_execute == nullptr || line == nullptr)
            {
                return;
            }

            std::string instead;

            if (TimeScaleZero(line, instead))
            {
                static bool said = false;

                if (!said)
                {
                    said = true;
                    FORGE_LOG_INFO("com_time_scale 0 would stop the game dead; running '%s'",
                                   instead.c_str());
                }

                line = instead.c_str();
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
        g_write = reinterpret_cast<ConsoleWriteRoutine>(GameSymbols::Address(GameSymbol::GameConsoleWrite));
        g_cmdFind = reinterpret_cast<CmdFindRoutine>(GameModule::Resolve(GameAddress::kCmdFind));
        g_cmdRegister = reinterpret_cast<CmdRegisterRoutine>(GameSymbols::Address(GameSymbol::CmdRegister));
        g_execute = reinterpret_cast<ConsoleExecuteRoutine>(GameSymbols::Address(GameSymbol::ConsoleExecute));
        g_freeTracked = reinterpret_cast<FreeTrackedRoutine>(GameSymbols::Address(GameSymbol::MemFreeTracked));

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
        g_table.IsOpen = &ApiIsOpen;
        g_table.SetOpen = &ApiSetOpen;
        g_table.SetLine = &ApiSetLine;

        FrameApi::AddInternalTick(&PumpPending);

        FORGE_LOG_INFO("Console interface installed");
        return true;
    }

    void ConsoleApi::Uninstall()
    {
        for (const auto& entry : g_commands)
        {
            UnregisterFromGame(entry.first);
        }

        g_commands.clear();
        g_table = {};

    }

    const AmodConsoleApi* ConsoleApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void ConsoleApi::AddInternalCommand(const char* name, AmodConsoleCommand handler, void* user)
    {
        if (name == nullptr || name[0] == '\0' || handler == nullptr)
        {
            return;
        }

        g_commands[name] = Command{ nullptr, handler, user };
        EnsureGameRegistered(name); // or later, on a frame, if the console is not up yet
    }

    void ConsoleApi::Print(const char* text)
    {
        if (g_write != nullptr && text != nullptr)
        {
            g_write(0, "%s\n", text);
        }
    }

    void ConsoleApi::ForgetMod(ModContext* owner)
    {
        if (owner == nullptr)
        {
            return;
        }

        for (auto it = g_commands.begin(); it != g_commands.end();)
        {
            if (it->second.owner != owner)
            {
                ++it;
                continue;
            }

            UnregisterFromGame(it->first);
            it = g_commands.erase(it);
        }
    }
}
