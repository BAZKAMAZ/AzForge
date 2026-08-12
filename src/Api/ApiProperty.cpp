#include "src/Api/ApiProperty.h"

#include "src/Api/ApiRead.h"
#include "src/Game/GameModule.h"

#include <string.h>

namespace Forge
{
    namespace ApiProperty
    {
        namespace
        {
            typedef uint32_t (__cdecl* ResolveRoutine)(const char* name);

            uint32_t Resolve(uintptr_t slot, const char* name)
            {
                void** const at = static_cast<void**>(GameModule::Resolve(slot));
                const ResolveRoutine routine =
                    at != nullptr ? reinterpret_cast<ResolveRoutine>(*at) : nullptr;

                return (routine != nullptr && name != nullptr) ? routine(name) : 0;
            }
        }

        const Field* Find(const Field* table, size_t count, const char* key)
        {
            if (table == nullptr || key == nullptr)
            {
                return nullptr;
            }

            for (size_t at = 0; at < count; ++at)
            {
                if (strcmp(table[at].key, key) == 0)
                {
                    return &table[at];
                }
            }
            return nullptr;
        }

        int Get(const uint8_t* record, const Field& field, AmodValue* out)
        {
            if (record == nullptr || out == nullptr)
            {
                return 0;
            }

            AmodValue value = {};
            switch (field.kind)
            {
            case Kind::Int:
                value.kind = AMOD_VALUE_INT;
                value.integer = ApiRead::Read<int>(record, field.offset);
                break;

            case Kind::Byte:
                value.kind = AMOD_VALUE_INT;
                value.integer = ApiRead::Read<uint8_t>(record, field.offset);
                break;

            case Kind::Float:
            case Kind::Floats:
            {
                const uint32_t count = field.kind == Kind::Float ? 1u : field.count;
                value.kind = AMOD_VALUE_FLOAT;
                value.count = count > 4u ? 4u : count;
                for (uint32_t at = 0; at < value.count; ++at)
                {
                    value.numbers[at] =
                        ApiRead::Read<float>(record, field.offset + at * sizeof(float));
                }
                break;
            }

            case Kind::Flag:
                value.kind = AMOD_VALUE_BOOL;
                value.integer =
                    (ApiRead::Read<uint32_t>(record, field.offset) & field.flag) != 0 ? 1 : 0;
                break;

            case Kind::Text:
                if (!ApiRead::NameAt(record, field.offset))
                {
                    return 0;
                }
                value.kind = AMOD_VALUE_STRING;
                value.text = ApiRead::ReadText(record, field.offset);
                break;

            case Kind::Resource:
                value.kind = AMOD_VALUE_INT;
                value.integer = static_cast<int>(ApiRead::Read<uint32_t>(record, field.offset));
                break;
            }

            *out = value;
            return 1;
        }

        int Set(uint8_t* record, const Field& field, const AmodValue& value)
        {
            if (record == nullptr || !field.writable)
            {
                return 0;
            }

            switch (field.kind)
            {
            case Kind::Int:
            {
                if (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL)
                {
                    return 0;
                }
                const int written = value.integer;
                memcpy(record + field.offset, &written, sizeof(written));
                return 1;
            }

            case Kind::Byte:
            {
                if (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL)
                {
                    return 0;
                }
                record[field.offset] = static_cast<uint8_t>(value.integer);
                return 1;
            }

            case Kind::Float:
            case Kind::Floats:
            {
                const uint32_t needed = field.kind == Kind::Float ? 1u : field.count;
                if (value.kind != AMOD_VALUE_FLOAT || value.count < needed)
                {
                    return 0;
                }
                memcpy(record + field.offset, value.numbers, needed * sizeof(float));
                return 1;
            }

            case Kind::Flag:
            {
                if (value.kind != AMOD_VALUE_BOOL && value.kind != AMOD_VALUE_INT)
                {
                    return 0;
                }

                uint32_t flags = ApiRead::Read<uint32_t>(record, field.offset);
                flags = value.integer != 0 ? (flags | field.flag) : (flags & ~field.flag);
                memcpy(record + field.offset, &flags, sizeof(flags));
                return 1;
            }

            case Kind::Resource:
            {
                if (value.kind != AMOD_VALUE_STRING)
                {
                    return 0;
                }

                const uint32_t resolved = Resolve(field.resolver, value.text);
                if (resolved == 0)
                {
                    return 0;
                }
                memcpy(record + field.offset, &resolved, sizeof(resolved));
                return 1;
            }

            case Kind::Text:
            {
                if (value.kind != AMOD_VALUE_STRING || value.text == nullptr || field.count == 0)
                {
                    return 0;
                }

                const size_t room = field.count;
                const size_t length = strlen(value.text);

                memset(record + field.offset, 0, room);
                memcpy(record + field.offset, value.text, length < room - 1 ? length : room - 1);
                return 1;
            }
            }

            return 0;
        }

        int Restore(uint8_t* record, const Field& field, const uint8_t* snapshot)
        {
            if (record == nullptr || snapshot == nullptr || !field.writable)
            {
                return 0;
            }

            switch (field.kind)
            {
            case Kind::Flag:
            {
                // One bit only — rest of the int is the game's.
                const uint32_t was = ApiRead::Read<uint32_t>(snapshot, field.offset);
                uint32_t flags = ApiRead::Read<uint32_t>(record, field.offset);

                flags = (flags & ~field.flag) | (was & field.flag);
                memcpy(record + field.offset, &flags, sizeof(flags));
                return 1;
            }

            case Kind::Floats:
                memcpy(record + field.offset, snapshot + field.offset, field.count * sizeof(float));
                return 1;

            case Kind::Int:
            case Kind::Float:
            case Kind::Resource:
                memcpy(record + field.offset, snapshot + field.offset, sizeof(uint32_t));
                return 1;

            case Kind::Byte:
                record[field.offset] = snapshot[field.offset];
                return 1;

            case Kind::Text:
                return 0;
            }

            return 0;
        }
    }
}
