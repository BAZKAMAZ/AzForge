#pragma once

#include <stdint.h>
#include <vector>

namespace Forge
{
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
