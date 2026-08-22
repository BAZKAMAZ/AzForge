#pragma once

namespace Forge
{
    class ModScreen
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static bool                                                 IsOpen();
    };
}
