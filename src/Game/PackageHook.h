#pragma once

namespace Forge
{
    // Detour on Package_OpenFile @0x00422f00
    class PackageHook
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();
        static bool                                                 IsInstalled();

        static unsigned long long                                   ServedCount();
    };
}
