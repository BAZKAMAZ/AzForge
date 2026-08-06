#include <windows.h>

#include "src/Game/GameModule.h"

#include "src/Core/ForgeLog.h"
#include "src/Core/ForgeText.h"
#include "src/Game/GameAddresses.h"

#include <stdio.h>

namespace Forge
{
    namespace
    {
        uintptr_t g_base = 0;
        intptr_t g_shift = 0;
        bool g_ready = false;
        std::vector<MemoryRange> g_executable;

        // Walks the section table of the mapped image and records what is executable.
        void CollectExecutableSections(uintptr_t base)
        {
            g_executable.clear();

            const uint8_t* image = reinterpret_cast<const uint8_t*>(base);
            const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return;
            }

            const IMAGE_NT_HEADERS32* nt =
                reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
            {
                return;
            }

            const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
            {
                if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                {
                    continue;
                }

                MemoryRange range;
                range.begin = image + section->VirtualAddress;
                range.size = section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize
                                                            : section->SizeOfRawData;
                g_executable.push_back(range);
            }
        }

        std::string FormatBytes(const uint8_t* bytes, size_t length)
        {
            std::string text;
            char item[4];
            for (size_t i = 0; i < length; ++i)
            {
                if (i != 0)
                {
                    text += ' ';
                }
                snprintf(item, sizeof(item), "%02X", bytes[i]);
                text += item;
            }
            return text;
        }
    }

    bool GameModule::Initialize()
    {
        if (g_ready)
        {
            return true;
        }

        const HMODULE module = GetModuleHandleW(nullptr);
        if (module == nullptr)
        {
            FORGE_LOG_ERROR("Cannot obtain the handle of the host module");
            return false;
        }

        g_base = reinterpret_cast<uintptr_t>(module);
        g_shift = static_cast<intptr_t>(g_base) - static_cast<intptr_t>(GameAddress::kPreferredImageBase);
        CollectExecutableSections(g_base);
        g_ready = true;

        if (g_executable.empty())
        {
            FORGE_LOG_ERROR("The host module exposes no executable section");
            g_ready = false;
            return false;
        }

        if (g_shift != 0)
        {
            FORGE_LOG_WARN("Host module is loaded at 0x%08X instead of 0x%08X, addresses are shifted by 0x%X",
                           static_cast<unsigned>(g_base),
                           static_cast<unsigned>(GameAddress::kPreferredImageBase),
                           static_cast<unsigned>(g_shift));
        }

        return true;
    }

    bool GameModule::IsReady()
    {
        return g_ready;
    }

    uintptr_t GameModule::Base()
    {
        return g_base;
    }

    intptr_t GameModule::Shift()
    {
        return g_shift;
    }

    void* GameModule::Resolve(uintptr_t staticAddress)
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<intptr_t>(staticAddress) + g_shift));
    }

    const std::vector<MemoryRange>& GameModule::ExecutableSections()
    {
        return g_executable;
    }

    bool GameModule::VerifyBytes(const void* address, const uint8_t* expected, size_t length, std::string& mismatch)
    {
        mismatch.clear();

        const uint8_t* actual = static_cast<const uint8_t*>(address);

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(actual, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT)
        {
            mismatch = "the address is not mapped";
            return false;
        }

        for (size_t i = 0; i < length; ++i)
        {
            if (actual[i] != expected[i])
            {
                mismatch = "expected " + FormatBytes(expected, length) +
                           ", found " + FormatBytes(actual, length);
                return false;
            }
        }

        return true;
    }
}
