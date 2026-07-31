#include <windows.h>

#include "src/Game/GameSymbols.h"

#include "src/Core/ForgeLog.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbolTable.h"
#include "src/Game/Signature.h"

#include <vector>

namespace Forge
{
    namespace
    {
        void* g_addresses[static_cast<size_t>(GameSymbol::Count)] = {};
        bool g_resolved = false;

        // Two matches are enough to call a signature ambiguous, so the scan stops there.
        constexpr size_t kAmbiguityLimit = 2;

        bool ResolveOne(size_t index)
        {
            const GameSymbolDefinition& definition = kGameSymbolTable[index];

            Signature signature;
            std::string error;
            if (!signature.Parse(definition.pattern, error))
            {
                FORGE_LOG_ERROR("%s: the signature is malformed: %s", definition.name, error.c_str());
                return false;
            }

            uint8_t* const hint = static_cast<uint8_t*>(GameModule::Resolve(definition.hint));

            MEMORY_BASIC_INFORMATION info = {};
            if (VirtualQuery(hint, &info, sizeof(info)) != 0 && info.State == MEM_COMMIT &&
                signature.MatchesAt(hint))
            {
                g_addresses[index] = hint;
                FORGE_LOG_INFO("  %-18s 0x%08X  (at the expected address)",
                               definition.name, static_cast<unsigned>(reinterpret_cast<uintptr_t>(hint)));
                return true;
            }

            std::vector<const uint8_t*> hits;
            std::vector<const uint8_t*> found;
            for (const MemoryRange& range : GameModule::ExecutableSections())
            {
                signature.FindAll(range, hits, kAmbiguityLimit);
                found.insert(found.end(), hits.begin(), hits.end());
                if (found.size() >= kAmbiguityLimit)
                {
                    break;
                }
            }

            if (found.empty())
            {
                FORGE_LOG_ERROR("%s: the signature matches nowhere in the executable; this is not "
                                "the build AzForge was reversed against", definition.name);
                return false;
            }

            if (found.size() > 1)
            {
                FORGE_LOG_ERROR("%s: the signature matches more than once, so it cannot identify "
                                "the routine; the signature needs to be made more specific",
                                definition.name);
                return false;
            }

            g_addresses[index] = const_cast<uint8_t*>(found.front());
            FORGE_LOG_WARN("  %-18s 0x%08X  (found by scanning, expected 0x%08X)",
                           definition.name,
                           static_cast<unsigned>(reinterpret_cast<uintptr_t>(found.front())),
                           static_cast<unsigned>(reinterpret_cast<uintptr_t>(hint)));
            return true;
        }
    }

    bool GameSymbols::Resolve()
    {
        if (g_resolved)
        {
            return true;
        }

        if (!GameModule::IsReady())
        {
            FORGE_LOG_ERROR("Symbol resolution needs the host module first");
            return false;
        }

        FORGE_LOG_INFO("Resolving game symbols:");

        bool complete = true;
        for (size_t i = 0; i < static_cast<size_t>(GameSymbol::Count); ++i)
        {
            if (!ResolveOne(i))
            {
                complete = false;
            }
        }

        g_resolved = complete;
        return complete;
    }

    bool GameSymbols::IsResolved()
    {
        return g_resolved;
    }

    void* GameSymbols::Address(GameSymbol symbol)
    {
        return g_addresses[static_cast<size_t>(symbol)];
    }

    const char* GameSymbols::Name(GameSymbol symbol)
    {
        return kGameSymbolTable[static_cast<size_t>(symbol)].name;
    }
}
