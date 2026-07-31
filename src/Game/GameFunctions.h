#pragma once

namespace Forge
{
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
