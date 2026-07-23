#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    struct MemoryRange
    {
        const uint8_t* begin = nullptr;
        size_t size = 0;
    };

    // Byte pattern with wildcards, written the way disassemblers print them:
    //
    //   "56 8B 74 24 08 E8 ?? ?? ?? ??"
    //
    // A pattern must wildcard every byte that depends on where the code sits: the operand
    // of a call or jump that reaches outside the pattern, and any absolute address of a
    // global. Short jumps within a function are relative to the following instruction and
    // stay the same when the function moves, so they belong in the pattern as literals -
    // and they are worth keeping, because they carry the shape of the control flow.
    class Signature
    {
    public:
        bool                                                        Parse(const char* pattern, std::string& error);

        bool                                                        MatchesAt(const uint8_t* candidate) const;
        size_t                                                      Length() const;

        // Collects matches inside the range, stopping once limit have been found so an
        // ambiguous pattern can be reported without scanning to the end.
        size_t                                                      FindAll(const MemoryRange& range, std::vector<const uint8_t*>& hits, size_t limit) const;

    private:
        std::vector<uint8_t> m_bytes;
        std::vector<uint8_t> m_mask;    // 0xFF where the byte must match, 0x00 for a wildcard
    };
}
