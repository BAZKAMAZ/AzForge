#pragma once

#include <stdint.h>
#include <vector>

namespace Forge
{
    // Everything a mod creates through the API is recorded here against that mod.
    //
    // The point is that a mod cannot leave something behind that outlives it. Unloading a
    // mod releases its registrations in the reverse of the order they were made, so a
    // registration that depends on an earlier one is always undone first, and a mod author
    // never has to write teardown code that mirrors their setup code.
    //
    // A token is only ever handed back to the mod that owns it, so releasing one by hand
    // cannot touch another mod's registration.
    class ModOwnership
    {
    public:
        typedef void (*ReleaseRoutine)(void* payload);

        // Returns a token that is unique within this mod, never zero.
        uint32_t                                                    Add(ReleaseRoutine release, void* payload);

        // Releases one registration. Returns false when the token is not this mod's.
        bool                                                        Remove(uint32_t token);

        void                                                        ReleaseAll();
        size_t                                                      Count() const;

    private:
        struct Entry
        {
            uint32_t token;
            ReleaseRoutine release;
            void* payload;
        };

        std::vector<Entry> m_entries;
        uint32_t m_nextToken = 1;
    };
}
