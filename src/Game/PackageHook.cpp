#include "src/Game/PackageHook.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameFunctions.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Game/PackageFile.h"
#include "src/Hook/InlineHook.h"
#include "src/Mods/ModFileSystem.h"

#include <string.h>

namespace Forge
{
    namespace
    {
        // Package_OpenFile is __thiscall: ECX = self, path on stack, RET 4
        typedef void* (__fastcall* PackageOpenFileRoutine)(void* self, void* unused, const char* path);

        constexpr char kAllocationTag[] = ".\\AzForge\\PackageHook.cpp";

        InlineHook g_hook;
        PackageOpenFileRoutine g_original = nullptr;
        unsigned long long g_served = 0;

        // Same layout as Package_ReadFromDisk: name[0x100], size, data of size+1 (NUL-terminated).
        GamePackageFile* BuildPackageFile(const char* path, const std::vector<uint8_t>& bytes)
        {
            if (bytes.size() > 0x7FFFFFFEu)
            {
                return nullptr;
            }

            GamePackageFile* file = static_cast<GamePackageFile*>(GameFunctions::Allocate(static_cast<int>(sizeof(GamePackageFile)), kAllocationTag, __LINE__));
            if (file == nullptr)
            {
                return nullptr;
            }

            GameFunctions::AssignName(file->name, path);
            file->size = static_cast<uint32_t>(bytes.size());

            void* payload = GameFunctions::Allocate(static_cast<int>(bytes.size()) + 1, kAllocationTag, __LINE__);
            if (payload == nullptr)
            {
                GameFunctions::Free(file);
                return nullptr;
            }

            if (!bytes.empty())
            {
                memcpy(payload, bytes.data(), bytes.size());
            }
            static_cast<uint8_t*>(payload)[bytes.size()] = 0;

            file->data = payload;
            return file;
        }

        void* __fastcall PackageOpenFileDetour(void* self, void* unused, const char* path)
        {
            std::vector<uint8_t> bytes;
            std::string providerId;
            std::string error;

            const ModFileResult result = ModFileSystem::Read(path, bytes, providerId, error);

            if (result == ModFileResult::Loaded)
            {
                GamePackageFile* file = BuildPackageFile(path, bytes);
                if (file != nullptr)
                {
                    ++g_served;
                    FORGE_LOG_INFO("'%s' served by '%s' (%zu bytes)",
                        path, providerId.c_str(), bytes.size());
                    return file;
                }

                FORGE_LOG_ERROR("'%s' from '%s' could not be allocated through the game allocator, "
                    "the game's own copy is used instead", path, providerId.c_str());
            }
            else if (result == ModFileResult::Failed)
            {
                FORGE_LOG_ERROR("'%s' from '%s': %s; the game's own copy is used instead",
                    path, providerId.c_str(), error.c_str());
            }

            return g_original(self, unused, path);
        }
    }

    bool PackageHook::Install()
    {
        if (g_hook.IsInstalled())
        {
            return true;
        }

        if (!GameFunctions::IsBound())
        {
            FORGE_LOG_ERROR("The resource hook needs the game routines bound first");
            return false;
        }

        void* const target = GameSymbols::Address(GameSymbol::PackageOpenFile);
        if (target == nullptr)
        {
            FORGE_LOG_ERROR("Package_OpenFile was not located, the resource hook is not installed");
            return false;
        }

        // Verify the five bytes the trampoline will steal.
        std::string mismatch;
        if (!GameModule::VerifyBytes(target, kPackageOpenFilePrologue, sizeof(kPackageOpenFilePrologue), mismatch))
        {
            FORGE_LOG_ERROR("Package_OpenFile does not open with the instructions AzForge steals (%s); "
                "the resource hook is not installed", mismatch.c_str());
            return false;
        }

        std::string error;
        if (!g_hook.Install(target, reinterpret_cast<void*>(&PackageOpenFileDetour), kPackageOpenFilePrologue, sizeof(kPackageOpenFilePrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook Package_OpenFile: %s", error.c_str());
            return false;
        }

        g_original = reinterpret_cast<PackageOpenFileRoutine>(g_hook.Trampoline());

        FORGE_LOG_INFO("Package_OpenFile hooked at 0x%08X, original reachable at 0x%08X",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)),
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(g_original)));
        return true;
    }

    void PackageHook::Uninstall()
    {
        if (!g_hook.IsInstalled())
        {
            return;
        }

        g_hook.Uninstall();
        g_original = nullptr;

        FORGE_LOG_INFO("Package_OpenFile restored, %llu request(s) were served by mods", g_served);
        g_served = 0;
    }

    bool PackageHook::IsInstalled()
    {
        return g_hook.IsInstalled();
    }

    unsigned long long PackageHook::ServedCount()
    {
        return g_served;
    }
}