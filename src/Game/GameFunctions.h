#pragma once

namespace Forge
{
    // Typed bindings to the game routines AzForge has to call rather than reimplement.
    //
    // Memory handed to the game must come from its own tracked allocator: Mem_FreeTracked
    // @0x00421ce0 checks the 0xAFAF magic in the 0x118-byte header and unlinks the block
    // from a global list, so a CRT allocation released through it would be rejected and
    // leaked.
    class GameFunctions
    {
    public:
        static bool                                                 Bind();
        static bool                                                 IsBound();

        static void*                                                Allocate(int size, const char* sourceFile, int sourceLine);
        static void                                                 Free(void* block);
        static void                                                 AssignName(char* destination, const char* text);
    };
}
