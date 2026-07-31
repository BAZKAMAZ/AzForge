#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Forge
{
#pragma pack(push, 1)
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
