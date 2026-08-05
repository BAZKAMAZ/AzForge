#pragma once

#include <ForgeApi.h>

#include "src/Api/ModOwnership.h"

#include <string>
#include <vector>

namespace Forge
{
    constexpr uint32_t kModContextMagic = 0x444F4D41; // 'AMOD'

    struct ModContext
    {
        uint32_t magic = kModContextMagic;
        AmodApi table = {};
        std::string id;
        std::string version;

        std::vector<std::string> capabilities;

        bool privileged = false;

        // Everything this mod registered, released in reverse when it is unloaded.
        ModOwnership owned;

        // Why the mod's last call refused. Cleared by Caller, so it never outlives one call.
        int failCode = AMOD_FAIL_NONE;
        std::string failText;

        bool                                                        Declares(const char* capability) const;
    };

    void                                                            BuildApiTable(ModContext& context);
}
