#include "pch.h"

#include "src/Api/InteropApi.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameModule.h"
#include "src/Game/Signature.h"
#include "src/Hook/InlineHook.h"

#include <memory>
#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        AmodInteropApi g_table = {};

        // A hook belongs to the loader, not to the mod that asked for it: the bytes have to
        // come back out of the game's code before the mod's DLL is unmapped, and a mod that
        // crashed or simply forgot cannot be relied on to do that.
        struct Detour
        {
            uint32_t id;
            ModContext* owner;
            uintptr_t target;
            std::unique_ptr<InlineHook> hook;
        };

        std::vector<Detour> g_detours;
        uint32_t g_nextDetourId = 1;

        // Two matches are enough to call a pattern ambiguous, the same limit the loader's own
        // symbol resolution uses.
        constexpr size_t kAmbiguityLimit = 2;

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        // Whether the whole range is committed and carries at least the protection asked for.
        // A range that crosses into a second region is walked region by region, because
        // VirtualQuery answers about one at a time and a structure can straddle two.
        bool RangeHasAccess(uintptr_t address, uint32_t size, DWORD accepted)
        {
            if (size == 0)
            {
                return false;
            }

            // A range that wraps is not a range.
            if (address + size < address)
            {
                return false;
            }

            uintptr_t at = address;
            const uintptr_t end = address + size;

            while (at < end)
            {
                MEMORY_BASIC_INFORMATION info = {};
                if (VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) == 0)
                {
                    return false;
                }

                if (info.State != MEM_COMMIT || (info.Protect & accepted) == 0 ||
                    (info.Protect & PAGE_GUARD) != 0)
                {
                    return false;
                }

                const uintptr_t regionEnd =
                    reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
                if (regionEnd <= at)
                {
                    return false;
                }
                at = regionEnd;
            }

            return true;
        }

        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                    PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                    PAGE_EXECUTE_WRITECOPY;

        constexpr DWORD kAnyAccess = kReadable | PAGE_EXECUTE;

        // --- the table the mods call through ------------------------------------------------

        uintptr_t __cdecl ApiModuleBase(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? GameModule::Base() : 0;
        }

        uintptr_t __cdecl ApiModuleSize(AmodMod mod)
        {
            if (Resolve(mod) == nullptr || !GameModule::IsReady())
            {
                return 0;
            }

            const uintptr_t base = GameModule::Base();
            const IMAGE_DOS_HEADER* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const IMAGE_NT_HEADERS* const nt =
                reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            return nt->OptionalHeader.SizeOfImage;
        }

        AmodFindResult __cdecl ApiFind(AmodMod mod, const char* pattern, uintptr_t* address)
        {
            if (Resolve(mod) == nullptr || pattern == nullptr || address == nullptr ||
                !GameModule::IsReady())
            {
                return AMOD_FIND_MALFORMED;
            }

            Signature signature;
            std::string error;
            if (!signature.Parse(pattern, error))
            {
                FORGE_LOG_WARN("A mod asked for a malformed pattern: %s", error.c_str());
                return AMOD_FIND_MALFORMED;
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
                return AMOD_FIND_NOT_FOUND;
            }

            // Refused rather than guessed at, for the same reason the loader refuses one of
            // its own: taking the first hit binds to whichever came first in memory.
            if (found.size() > 1)
            {
                return AMOD_FIND_AMBIGUOUS;
            }

            *address = reinterpret_cast<uintptr_t>(found.front());
            return AMOD_FIND_OK;
        }

        int __cdecl ApiRead(AmodMod mod, uintptr_t address, void* buffer, uint32_t size)
        {
            if (Resolve(mod) == nullptr || buffer == nullptr || size == 0 ||
                !RangeHasAccess(address, size, kReadable))
            {
                return 0;
            }

            memcpy(buffer, reinterpret_cast<const void*>(address), size);
            return 1;
        }

        int __cdecl ApiWrite(AmodMod mod, uintptr_t address, const void* data, uint32_t size)
        {
            if (Resolve(mod) == nullptr || data == nullptr || size == 0 ||
                !RangeHasAccess(address, size, kAnyAccess))
            {
                return 0;
            }

            // Made writable for the length of the write and put back afterwards, so patching
            // the game's code works the same way patching its data does.
            void* const at = reinterpret_cast<void*>(address);
            DWORD previous = 0;
            if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &previous))
            {
                return 0;
            }

            memcpy(at, data, size);

            DWORD restored = 0;
            VirtualProtect(at, size, previous, &restored);
            FlushInstructionCache(GetCurrentProcess(), at, size);
            return 1;
        }

        void ReleaseDetour(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_detours.begin(); it != g_detours.end(); ++it)
            {
                if (it->id == id)
                {
                    // The destructor of InlineHook puts the original bytes back.
                    g_detours.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiHook(AmodMod mod, uintptr_t target, void* detour,
                                  const void* expectedPrologue, uint32_t prologueLength,
                                  void** trampoline)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || target == 0 || detour == nullptr ||
                expectedPrologue == nullptr || trampoline == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            // The target has to be code that is actually there before its bytes are compared,
            // so a wrong address is reported rather than faulting inside the comparison.
            if (!RangeHasAccess(target, prologueLength, kAnyAccess))
            {
                FORGE_LOG_WARN("[%s] asked to hook 0x%08X, which is not mapped code",
                               context->id.c_str(), static_cast<unsigned>(target));
                return AMOD_INVALID_TOKEN;
            }

            // One detour per site. Two hooks on one routine would each save the other's
            // patched bytes as the original, and removing them in the wrong order would leave
            // the game jumping into a trampoline that no longer exists.
            for (const Detour& existing : g_detours)
            {
                if (existing.target == target)
                {
                    FORGE_LOG_WARN("[%s] asked to hook 0x%08X, which is already hooked by "
                                   "another mod", context->id.c_str(),
                                   static_cast<unsigned>(target));
                    return AMOD_INVALID_TOKEN;
                }
            }

            Detour record;
            record.id = g_nextDetourId++;
            record.owner = context;
            record.target = target;
            record.hook = std::make_unique<InlineHook>();

            std::string error;
            if (!record.hook->Install(reinterpret_cast<void*>(target), detour,
                                      static_cast<const uint8_t*>(expectedPrologue),
                                      prologueLength, error))
            {
                FORGE_LOG_WARN("[%s] could not hook 0x%08X: %s", context->id.c_str(),
                               static_cast<unsigned>(target), error.c_str());
                return AMOD_INVALID_TOKEN;
            }

            *trampoline = record.hook->Trampoline();

            const uint32_t id = record.id;
            g_detours.push_back(std::move(record));

            FORGE_LOG_INFO("[%s] hooked 0x%08X", context->id.c_str(),
                           static_cast<unsigned>(target));

            return context->owned.Add(&ReleaseDetour,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
        }

        void __cdecl ApiUnhook(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Resolve(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }
    }

    bool InteropApi::Install()
    {
        if (!GameModule::IsReady())
        {
            FORGE_LOG_WARN("The host module is not mapped, so the direct interface is not "
                           "offered");
            return false;
        }

        g_table.structSize = sizeof(AmodInteropApi);
        g_table.ModuleBase = &ApiModuleBase;
        g_table.ModuleSize = &ApiModuleSize;
        g_table.Find = &ApiFind;
        g_table.Read = &ApiRead;
        g_table.Write = &ApiWrite;
        g_table.Hook = &ApiHook;
        g_table.Unhook = &ApiUnhook;

        FORGE_LOG_INFO("Direct interface installed");
        return true;
    }

    void InteropApi::Uninstall()
    {
        // Every remaining detour comes out of the game's code here.
        g_detours.clear();
        g_table = {};
    }

    const AmodInteropApi* InteropApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void InteropApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_detours.begin(); it != g_detours.end();)
        {
            it = it->owner == owner ? g_detours.erase(it) : ++it;
        }
    }
}
