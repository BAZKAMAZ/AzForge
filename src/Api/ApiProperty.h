#pragma once

#include <ForgeTypes.h>

#include <stdint.h>

namespace Forge
{
    namespace ApiProperty
    {
        enum class Kind
        {
            Int,
            Byte,
            Float,
            Floats,
            Text,
            Flag,
            Resource
        };

        struct Field
        {
            const char* key;
            uintptr_t offset;
            Kind kind;
            uint32_t count;         // Floats; Text: the width, and 0 means it cannot be written
            uint32_t flag;
            uintptr_t resolver;     // Resource: the slot of the game routine that names it
            bool writable;
        };

        const Field*                                                Find(const Field* table, size_t count, const char* key);

        int                                                         Get(const uint8_t* record, const Field& field, AmodValue* out);
        int                                                         Set(uint8_t* record, const Field& field, const AmodValue& value);

        int                                                         Restore(uint8_t* record, const Field& field, const uint8_t* snapshot);
    }
}
