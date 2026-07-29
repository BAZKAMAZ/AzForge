#pragma once

#include <ForgeApi.h>

#include "src/Api/ModOwnership.h"

#include <string>
#include <vector>

namespace Forge
{
    constexpr uint32_t kModContextMagic = 0x444F4D41; // 'AMOD'

    // One of these exists per loaded mod. The AmodApi table inside it is what the mod
    // receives, and table.self points back at the context, so every call arrives already
    // attributed to its mod without the mod having to identify itself.
    //
    // The magic is checked on the way in: self is a value that lives in the mod's address
    // space and a damaged mod could pass anything, so the loader treats it as untrusted.
    struct ModContext
    {
        uint32_t magic = kModContextMagic;
        AmodApi table = {};
        std::string id;
        std::string version;

        // What the manifest declared this mod would do. The table is built to match, so a
        // mod that did not ask for something is not handed it.
        std::vector<std::string> capabilities;

        // Loaded from beside AzForge.dll rather than from the mods folder, which is the
        // whole of what earns it the mod-manager table.
        bool privileged = false;

        // Everything this mod registered, released in reverse when it is unloaded.
        ModOwnership owned;

        bool                                                        Declares(const char* capability) const;
    };

    // Fills the table with the entry points of this API version, and with nothing the mod
    // did not declare.
    void                                                            BuildApiTable(ModContext& context);
}
