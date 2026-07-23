#include "pch.h"

#include "src/Zip/Crc32.h"

namespace Forge
{
    namespace
    {
        struct Crc32Table
        {
            uint32_t value[256];

            constexpr Crc32Table()
                : value()
            {
                for (uint32_t i = 0; i < 256; ++i)
                {
                    uint32_t entry = i;
                    for (int bit = 0; bit < 8; ++bit)
                    {
                        entry = (entry & 1u) != 0 ? (0xEDB88320u ^ (entry >> 1)) : (entry >> 1);
                    }
                    value[i] = entry;
                }
            }
        };

        constexpr Crc32Table g_table;
    }

    uint32_t Crc32(const void* data, size_t size)
    {
        const uint8_t* cursor = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFFu;

        for (size_t i = 0; i < size; ++i)
        {
            crc = g_table.value[(crc ^ cursor[i]) & 0xFFu] ^ (crc >> 8);
        }

        return crc ^ 0xFFFFFFFFu;
    }
}
