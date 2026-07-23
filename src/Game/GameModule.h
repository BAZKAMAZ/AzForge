#pragma once

#include "src/Game/Signature.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    // Maps the static addresses in GameAddresses.h onto the running process.
    //
    // game.exe is a Visual Studio 2008 build without /DYNAMICBASE, so in practice it loads
    // at its preferred base and the shift is zero. The shift is computed anyway rather than
    // assumed, and reported in the log when it is not zero.
    class GameModule
    {
    public:
        static bool                                                 Initialize();
        static bool                                                 IsReady();

        static uintptr_t                                            Base();
        static intptr_t                                             Shift();

        // Rebases a static address onto the loaded module.
        static void*                                                Resolve(uintptr_t staticAddress);

        // Compares the bytes at an address against what the reversed build contains. Fills
        // mismatch with both byte sequences when they differ.
        static bool                                                 VerifyBytes(const void* address, const uint8_t* expected, size_t length, std::string& mismatch);

        // Sections of the host module that are marked executable, which is where code
        // signatures are searched for.
        static const std::vector<MemoryRange>&                      ExecutableSections();
    };
}
