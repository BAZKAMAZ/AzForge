#pragma once

namespace Forge
{
    namespace Crash
    {
        bool                                                        Install();
        void                                                        Uninstall();

        bool                                                        Guarded(void(__cdecl* call)(void* context), void* context);
    }
}
