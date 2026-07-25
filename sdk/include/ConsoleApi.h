/*
 * ConsoleApi.h - the developer console.
 *
 * The game carries a Quake-style console: named commands, variables, and text output. A mod
 * can print to it, run a line through it, and add commands of its own that the player - or
 * the mod - can call by name.
 *
 * A command's handler receives the words of the line: argv[0] is the command, argv[1..] its
 * arguments, argc how many there are.
 *
 * Reached through AmodApi::console, null on a loader older than API version 9.
 */

#ifndef AZFORGE_CONSOLE_API_H
#define AZFORGE_CONSOLE_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A command the player typed. argv[0] is the command name, argv[1..argc-1] its arguments;
 * the strings and the array are valid only for the call. */
typedef void (__cdecl* AmodConsoleCommand)(int argc, const char** argv, void* user);

typedef struct AmodConsoleApi
{
    uint32_t structSize;

    /* One line into the console. level 0 shows only while the 'developer' variable is set,
     * level 1 always. The text is taken as data, not a format string. */
    void (__cdecl* Print)(AmodMod mod, int level, const char* text);

    /* Adds a command under this name, replacing a mod command of the same name. Non-zero on
     * success. The command lives until the mod is unloaded. */
    int (__cdecl* AddCommand)(AmodMod mod, const char* name, AmodConsoleCommand handler, void* user);

    /* Drops a command this mod added. */
    void (__cdecl* RemoveCommand)(AmodMod mod, const char* name);

    /* Runs a line as though it had been typed: a command with its arguments, or a variable
     * to read or set. */
    void (__cdecl* Execute)(AmodMod mod, const char* line);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodConsoleApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

#include <stdarg.h>
#include <stdio.h>

namespace Amod
{
    namespace Console
    {
        inline const AmodConsoleApi* g_console = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodConsoleApi* api, AmodMod self)
        {
            g_console = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_console != nullptr;
        }

        inline void                                                 Print(const char* text, int level = 1)
        {
            if (g_console != nullptr)
            {
                g_console->Print(g_self, level, text);
            }
        }

        inline void                                                 Printf(const char* format, ...)
        {
            if (g_console == nullptr)
            {
                return;
            }

            char message[1024];
            va_list arguments;
            va_start(arguments, format);
            const int produced = vsnprintf(message, sizeof(message), format, arguments);
            va_end(arguments);

            if (produced >= 0)
            {
                g_console->Print(g_self, 1, message);
            }
        }

        inline bool                                                 AddCommand(const char* name, AmodConsoleCommand handler, void* user = nullptr)
        {
            return g_console != nullptr && g_console->AddCommand(g_self, name, handler, user) != 0;
        }

        inline void                                                 RemoveCommand(const char* name)
        {
            if (g_console != nullptr)
            {
                g_console->RemoveCommand(g_self, name);
            }
        }

        inline void                                                 Execute(const char* line)
        {
            if (g_console != nullptr)
            {
                g_console->Execute(g_self, line);
            }
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_CONSOLE_API_H */
