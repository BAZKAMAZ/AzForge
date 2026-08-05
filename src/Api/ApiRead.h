#pragma once

#include "src/Api/ModContext.h"
#include "src/Game/GameAddresses.h"

#include <stdint.h>
#include <string.h>

namespace Forge
{
    namespace ApiRead
    {
        inline ModContext*                                          Caller(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            if (context == nullptr || context->magic != kModContextMagic)
            {
                return nullptr;
            }

            context->failCode = AMOD_FAIL_NONE;
            context->failText.clear();
            return context;
        }

        int                                                         Fail(AmodMod mod, int code, const char* format, ...);

        template <typename T>
        T                                                           Read(const uint8_t* base, uintptr_t offset)
        {
            T value;
            memcpy(&value, base + offset, sizeof(T));
            return value;
        }

        inline const char*                                          ReadText(const uint8_t* base, uintptr_t offset)
        {
            return reinterpret_cast<const char*>(base + offset);
        }

        bool                                                        Readable(const void* address, size_t size);

        bool                                                        NameAt(const uint8_t* record, uintptr_t offset);

        int                                                         CopyOut(void* out, uint32_t requested, const void* filled, size_t produced);
    }
}
