#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Forge
{
    // CRC-32 as used by the zip format: reflected polynomial 0xEDB88320, seeded and
    // finalised with 0xFFFFFFFF.
    uint32_t                                                        Crc32(const void* data, size_t size);
}
