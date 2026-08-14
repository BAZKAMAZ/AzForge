#pragma once

#include <FilesApi.h>

#include <stdint.h>
#include <vector>

namespace Forge
{
    struct ModContext;

    class FilesApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static const AmodFilesApi*                                  Table();

        // What PackageHook asks before it lets a request through to the game
        static bool                                                 Served(const char* path, std::vector<uint8_t>& out, const char*& byWhom);
    };
}
