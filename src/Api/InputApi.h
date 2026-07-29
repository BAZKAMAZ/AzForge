#pragma once

#include <InputApi.h>

namespace Forge
{
    // Loader side of the game's own input.
    //
    // It hooks nothing. Input_Update fills a table of one byte per virtual key every frame and
    // this reads it, so a mod sees exactly what the game sees - including the game deciding not
    // to see something. The only state of its own is a copy of the previous frame, because the
    // game keeps only what is down now and a press is an edge.
    class InputApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodInputApi*                                  Table();
    };
}
