#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    // Hands a mod's DLL to whatever antivirus the player has installed, before it is written
    // out and mapped.
    //
    // This is worth being clear about, because it is easy to mistake for more than it is. A
    // mod is a native DLL and its DllMain runs before the loader gets any say, so nothing
    // AzForge does after loading can contain it - there is no sandbox here and there cannot
    // be one. What this catches is a mod carrying malware the player's antivirus already
    // recognises, which is the common case and not the clever one.
    //
    // AMSI is bound at run time. A machine without it, or with no scanner behind it, is not a
    // refusal: the answer is simply unknown and the mod is treated as it was before.
    class ModScan
    {
    public:
        static void                                                 Initialize();
        static void                                                 Shutdown();

        // False when the scanner identified the payload as malware, with what it said in
        // reason. True covers both a clean result and no scanner being there to ask.
        static bool                                                 IsClean(const std::string& modId, const std::vector<uint8_t>& payload, std::string& reason);
    };
}
