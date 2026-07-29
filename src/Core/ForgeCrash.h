#pragma once

namespace Forge
{
    // Writes what a fatal exception was and where it happened into the log, then lets it take
    // its course. A game with mods in it dies in places the game alone never did, and "it
    // crashed" is not something anyone can act on.
    namespace Crash
    {
        bool                                                        Install();
        void                                                        Uninstall();
    }
}
