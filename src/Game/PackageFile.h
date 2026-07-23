#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Forge
{
#pragma pack(push, 1)
    // The block Package_OpenFile @0x00422f00 returns. Allocated as 0x10c bytes through
    // Mem_AllocTracked @0x00421df0 and released by Package_Free @0x00422200, which frees
    // the payload first and then the block itself.
    //
    // Package_ReadFromDisk @0x00422660 allocates size + 1 bytes for the payload and writes
    // a terminating zero at [size], while size itself stays the true file length. Anything
    // AzForge hands back has to be built the same way: parsers in the game rely on the text
    // being null-terminated.
    struct GamePackageFile
    {
        uint32_t reserved;      // +0x000 zeroed by the allocator, never written by the game
        char name[0x100];       // +0x004 assigned through NameBuffer_Assign @0x004022b0
        uint32_t size;          // +0x104 file length without the terminating zero
        void* data;             // +0x108 payload, size + 1 bytes
    };
#pragma pack(pop)

    static_assert(sizeof(GamePackageFile) == 0x10C, "GamePackageFile must match the game layout");
    static_assert(offsetof(GamePackageFile, name) == 0x004, "name must sit at +0x004");
    static_assert(offsetof(GamePackageFile, size) == 0x104, "size must sit at +0x104");
    static_assert(offsetof(GamePackageFile, data) == 0x108, "data must sit at +0x108");
}
