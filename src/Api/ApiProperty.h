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
            Float,
            Floats,     // count of them, one AmodValue
            Text,       // in the record, fixed width
            Flag,       // one bit of an int
            Resource    // a texture, a model or a sound: written by name, and not readable back
        };

        struct Field
        {
            const char* key;
            uintptr_t offset;
            Kind kind;
            uint32_t count;         // Floats
            uint32_t flag;          // Flag
            uintptr_t resolver;     // Resource: the slot of the game routine that names it
            bool writable;
        };

        const Field*                                                Find(const Field* table, size_t count, const char* key);

        int                                                         Get(const uint8_t* record, const Field& field, AmodValue* out);
        int                                                         Set(uint8_t* record, const Field& field, const AmodValue& value);

        int                                                         Restore(uint8_t* record, const Field& field, const uint8_t* snapshot);
    }
}
