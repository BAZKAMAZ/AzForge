#ifndef AZFORGE_CONSOLE_API_H
#define AZFORGE_CONSOLE_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* argv[0] = command, argv[1..] = args. Words live in the console scratch; copy before any game call. */
typedef void (__cdecl* AmodConsoleCommand)(int argc, const char** argv, void* user);

typedef struct AmodConsoleApi
{
    uint32_t structSize;

    /* level 0 only when 'developer' is set; level 1 always. Text is data, not a format. */
    void (__cdecl* Print)(AmodMod mod, int level, const char* text);

    int (__cdecl* AddCommand)(AmodMod mod, const char* name, AmodConsoleCommand handler, void* user);
    void (__cdecl* RemoveCommand)(AmodMod mod, const char* name);

    /* Run a line as if typed. */
    void (__cdecl* Execute)(AmodMod mod, const char* line);

    /* Whether the console is down, and putting it up or down. */
    int (__cdecl* IsOpen)(AmodMod mod);
    void (__cdecl* SetOpen)(AmodMod mod, int open);

    /* The line the console is showing, as if it had been typed into it. Nothing is run: the
       text simply stands there with the caret after it, which is what lets a mod type a
       command out a letter at a time and then call Execute. */
    void (__cdecl* SetLine)(AmodMod mod, const char* text);
} AmodConsoleApi;

AMOD_LAST_FIELD(AmodConsoleApi, SetLine, 0x1C);

#ifdef __cplusplus
}
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

        inline bool                                                 IsOpen()
        {
            return g_console != nullptr && g_console->structSize >= sizeof(AmodConsoleApi) &&
                   g_console->IsOpen(g_self) != 0;
        }

        inline void                                                 SetOpen(bool open)
        {
            if (g_console != nullptr && g_console->structSize >= sizeof(AmodConsoleApi))
            {
                g_console->SetOpen(g_self, open ? 1 : 0);
            }
        }

        /* Puts text on the console's own line without running it. Typing a command out one
           letter a frame and then calling Execute is what this is for. */
        inline void                                                 SetLine(const char* text)
        {
            if (g_console != nullptr && g_console->structSize >= sizeof(AmodConsoleApi))
            {
                g_console->SetLine(g_self, text);
            }
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_CONSOLE_API_H */
