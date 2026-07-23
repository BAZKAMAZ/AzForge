#pragma once

namespace Forge
{
    // Detour on Package_OpenFile @0x00422f00, the single point through which the game opens
    // every resource: scripts, rooms, models, textures and the pak itself.
    //
    // The original tries the disk first and data.pak second. The detour inserts the mods in
    // front of both, so the resolution order becomes mods -> disk -> data.pak, and hands
    // control to the untouched original whenever no mod claims the resource.
    //
    // If a mod does claim a resource but its archive cannot be read, the original still
    // runs so the game keeps working on its own data, and the failure is written to the log
    // as an error. Every extraction is CRC-checked, so reaching that path means the archive
    // is damaged or was replaced underneath a running game.
    class PackageHook
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();
        static bool                                                 IsInstalled();

        static unsigned long long                                   ServedCount();
    };
}
