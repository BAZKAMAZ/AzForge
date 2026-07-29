#pragma once

#include <string>

namespace Forge
{
    // Which parts of the interface came up on this run.
    //
    // A subsystem that could not be installed is not a failure of the loader: its table is
    // handed to mods as null, which the interface documents, and everything else carries on.
    // That degradation is deliberate - one missing subsystem should not cost a player every
    // mod they installed - but it used to be silent from a mod's point of view, leaving a mod
    // to run half-working against a null it never checked.
    //
    // So the outcome is recorded here. It is written to the log as a summary, and a mod that
    // cannot do anything useful without a subsystem says so in its manifest and is refused
    // with a reason instead of loading into a world it cannot work in.
    //
    // The names are the ones a manifest uses, and they match the fields of AmodApi:
    // launcher, objects_menu, box_menu, block_expression, console, frame, render, world.
    class Subsystems
    {
    public:
        static void                                                 Record(const char* name, bool available);

        // False for a subsystem that failed and for one this loader has never heard of - a
        // mod asking for a name from a newer build is asking for something not here.
        static bool                                                 IsAvailable(const std::string& name);

        static void                                                 WriteSummary();
        static void                                                 Clear();
    };
}
