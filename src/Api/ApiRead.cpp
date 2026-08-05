#include <windows.h>

#include "src/Api/ApiRead.h"

#include <stdarg.h>
#include <stdio.h>

namespace Forge
{
    namespace ApiRead
    {
        int Fail(AmodMod mod, int code, const char* format, ...)
        {
            ModContext* const context = static_cast<ModContext*>(mod);
            if (context == nullptr || context->magic != kModContextMagic || format == nullptr)
            {
                return 0;
            }

            char text[512];
            va_list arguments;
            va_start(arguments, format);
            const int produced = vsnprintf(text, sizeof(text), format, arguments);
            va_end(arguments);

            context->failCode = code;
            context->failText = produced >= 0 ? text : "";
            return 0;
        }

        bool Readable(const void* address, size_t size)
        {
            if (address == nullptr)
            {
                return false;
            }

            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region) ||
                region.State != MEM_COMMIT)
            {
                return false;
            }

            const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
            return reinterpret_cast<uintptr_t>(address) + size <= end;
        }

        bool NameAt(const uint8_t* record, uintptr_t offset)
        {
            if (!Readable(record + offset, GameAddress::kSpawnNameLength))
            {
                return false;
            }

            const char* const name = reinterpret_cast<const char*>(record + offset);
            for (size_t at = 0; at < GameAddress::kSpawnNameLength; ++at)
            {
                if (name[at] == '\0')
                {
                    return at >= 2;
                }
                if (static_cast<unsigned char>(name[at]) < 0x20 ||
                    static_cast<unsigned char>(name[at]) > 0x7E)
                {
                    return false;
                }
            }
            return false;
        }

        int CopyOut(void* out, uint32_t requested, const void* filled, size_t produced)
        {
            if (out == nullptr || requested < sizeof(uint32_t))
            {
                return 0;
            }

            const uint32_t copied = requested < produced ? requested
                                                         : static_cast<uint32_t>(produced);
            memcpy(out, filled, copied);
            memcpy(out, &copied, sizeof(copied));
            return 1;
        }
    }
}
