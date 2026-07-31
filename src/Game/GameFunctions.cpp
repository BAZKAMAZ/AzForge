#include "src/Game/GameFunctions.h"

#include "src/Core/ForgeLog.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"

namespace Forge
{
    namespace
    {
        typedef void*(__cdecl* AllocateRoutine)(int size, const char* sourceFile, int sourceLine);
        typedef void(__cdecl* FreeRoutine)(void* block);
        typedef char*(__fastcall* AssignNameRoutine)(char* destination, void* unused, const char* text);

        AllocateRoutine g_allocate = nullptr;
        FreeRoutine g_free = nullptr;
        AssignNameRoutine g_assignName = nullptr;
        bool g_bound = false;
    }

    bool GameFunctions::Bind()
    {
        if (g_bound)
        {
            return true;
        }

        if (!GameSymbols::IsResolved())
        {
            FORGE_LOG_ERROR("GameFunctions::Bind called before the game symbols were resolved");
            return false;
        }

        g_allocate = reinterpret_cast<AllocateRoutine>(GameSymbols::Address(GameSymbol::MemAllocTracked));
        g_free = reinterpret_cast<FreeRoutine>(GameSymbols::Address(GameSymbol::MemFreeTracked));
        g_assignName = reinterpret_cast<AssignNameRoutine>(GameSymbols::Address(GameSymbol::NameBufferAssign));

        if (g_allocate == nullptr || g_free == nullptr || g_assignName == nullptr)
        {
            FORGE_LOG_ERROR("The game memory routines are not available, nothing can be handed to the game");
            return false;
        }

        g_bound = true;
        return true;
    }

    bool GameFunctions::IsBound()
    {
        return g_bound;
    }

    void* GameFunctions::Allocate(int size, const char* sourceFile, int sourceLine)
    {
        return g_allocate(size, sourceFile, sourceLine);
    }

    void GameFunctions::Free(void* block)
    {
        g_free(block);
    }

    void GameFunctions::AssignName(char* destination, const char* text)
    {
        g_assignName(destination, nullptr, text);
    }
}
