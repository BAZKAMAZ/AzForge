#pragma once

#include "src/Game/Signature.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    // Maps the static addresses in GameAddresses.h onto the running process
    class GameModule
    {
    public:
        static bool                                                 Initialize();
        static bool                                                 IsReady();

        static uintptr_t                                            Base();
        static intptr_t                                             Shift();

        static void*                                                Resolve(uintptr_t staticAddress);
        static bool                                                 VerifyBytes(const void* address, const uint8_t* expected, size_t length, std::string& mismatch);
        static const std::vector<MemoryRange>&                      ExecutableSections();
    };
}
