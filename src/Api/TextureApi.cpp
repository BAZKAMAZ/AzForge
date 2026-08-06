#include <windows.h>

#include "src/Api/TextureApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameFunctions.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        AmodTextureApi g_table = {};

        // LoadOrFindTexture: __thiscall → __fastcall (spare EDX); RET 0xC.
        typedef uint8_t*(__fastcall* LoadOrFindRoutine)(void* manager, void* unused, const char* name, int mipmapped, int hasAlpha);
        typedef void(__fastcall* ReleaseRoutine)(void* manager, void* unused, uint8_t* texture);
        typedef char(__stdcall* BuildRoutine)(uint8_t* texture);
        typedef void(__fastcall* ConstructRoutine)(uint8_t* texture);

        template <typename T>
        T Routine(uintptr_t address)
        {
            return reinterpret_cast<T>(GameModule::Resolve(address));
        }

        InlineHook g_loadHook;
        LoadOrFindRoutine g_originalLoad = nullptr;

        constexpr int kWalkLimit = 0x4000;

        void WriteInt(uint8_t* record, uintptr_t offset, int value)
        {
            memcpy(record + offset, &value, sizeof(value));
        }

        void WritePointer(uint8_t* record, uintptr_t offset, const void* value)
        {
            const uintptr_t written = reinterpret_cast<uintptr_t>(value);
            memcpy(record + offset, &written, sizeof(written));
        }

        void* Manager()
        {
            return GameModule::Resolve(GameAddress::kTextureManager);
        }

        uint8_t* ListHead()
        {
            uint8_t** const head = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kTextureListHead));
            return head != nullptr ? *head : nullptr;
        }

        bool IsTexture(const uint8_t* texture)
        {
            return texture != nullptr && Readable(texture, GameAddress::kTextureSize) &&
                   Read<uint32_t>(texture, GameAddress::kTextureMagicOffset) ==
                       GameAddress::kTextureMagic;
        }

        uint8_t* NextTexture(const uint8_t* texture)
        {
            return reinterpret_cast<uint8_t*>(
                Read<uintptr_t>(texture, GameAddress::kTextureNextOffset));
        }

        bool Listed(const uint8_t* texture)
        {
            int seen = 0;
            for (uint8_t* walk = ListHead(); IsTexture(walk) && seen < kWalkLimit;
                 walk = NextTexture(walk), ++seen)
            {
                if (walk == texture)
                {
                    return true;
                }
            }
            return false;
        }

        uint8_t* Resolve(AmodTextureId id)
        {
            uint8_t* const texture = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(id));
            return (id != AMOD_INVALID_TEXTURE && IsTexture(texture) && Listed(texture)) ? texture : nullptr;
        }

        AmodTextureId IdOf(const uint8_t* texture)
        {
            return static_cast<AmodTextureId>(reinterpret_cast<uintptr_t>(texture));
        }

        bool MadeFromPixels(const uint8_t* texture)
        {
            return texture[GameAddress::kTextureFromPixelsOffset] != 0;
        }

        std::string Normalised(const char* path)
        {
            std::string text = path != nullptr ? path : "";
            for (char& letter : text)
            {
                letter = letter == '\\'
                             ? '/'
                             : static_cast<char>(tolower(static_cast<unsigned char>(letter)));
            }
            return text;
        }

        struct Made
        {
            ModContext* owner;
            uint8_t* texture;
        };

        struct Held
        {
            ModContext* owner;
            uint8_t* texture;
        };

        struct Substitute
        {
            ModContext* owner;
            std::string path;
            uint8_t* texture;
        };

        // Repaint restore: file-backed rebuilds from disk; pixel-only keeps original pixels.
        struct Repainted
        {
            ModContext* owner;
            uint8_t* texture;
            uint8_t* pixels;    // null when the texture has a file to come back from
            int width;
            int height;
        };

        std::vector<Made> g_made;
        std::vector<Held> g_held;
        std::vector<Substitute> g_substitutes;
        std::vector<Repainted> g_repainted;

        bool AlreadyRepainted(const uint8_t* texture)
        {
            for (const Repainted& entry : g_repainted)
            {
                if (entry.texture == texture)
                {
                    return true;
                }
            }
            return false;
        }

        uint8_t* SubstituteFor(const char* path)
        {
            const std::string wanted = Normalised(path);
            if (wanted.empty())
            {
                return nullptr;
            }

            for (const Substitute& entry : g_substitutes)
            {
                if (entry.path == wanted && IsTexture(entry.texture) && Listed(entry.texture))
                {
                    return entry.texture;
                }
            }
            return nullptr;
        }

        uint8_t* __fastcall LoadOrFindDetour(void* manager, void* unused, const char* name, int mipmapped, int hasAlpha)
        {
            uint8_t* const standing = SubstituteFor(name);
            if (standing != nullptr)
            {
                // Bump refcount as the game does on a cache hit.
                WriteInt(standing, GameAddress::kTextureReferencesOffset, Read<int>(standing, GameAddress::kTextureReferencesOffset) + 1);
                return standing;
            }

            return g_originalLoad != nullptr
                       ? g_originalLoad(manager, unused, name, mipmapped, hasAlpha)
                       : nullptr;
        }

        // Texture_Build may throw; SEH so a failed create does not tear down the process.
        bool BuildGuarded(BuildRoutine build, uint8_t* texture)
        {
            __try
            {
                return build(texture) != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void ReleaseSurface(uint8_t* texture)
        {
            void* const surface = reinterpret_cast<void*>(Read<uintptr_t>(texture, GameAddress::kTextureSurfaceOffset));
            if (surface == nullptr)
            {
                return;
            }

            // IUnknown::Release (vtable[2]).
            void** const vtable = *static_cast<void***>(surface);
            const auto release = reinterpret_cast<uint32_t(__stdcall*)(void*)>(vtable[2]);
            release(surface);

            WritePointer(texture, GameAddress::kTextureSurfaceOffset, nullptr);
        }

        uint8_t* CopyPixels(int width, int height, const void* pixels)
        {
            const int bytes = width * height * 4;
            uint8_t* const buffer = static_cast<uint8_t*>(GameFunctions::Allocate(bytes, ".\\TexManager.cpp", 0x188));

            if (buffer != nullptr)
            {
                memcpy(buffer, pixels, static_cast<size_t>(bytes));
            }
            return buffer;
        }

        bool SaneSize(int width, int height, const void* pixels)
        {
            return pixels != nullptr && width > 0 && height > 0 && width <= 4096 &&
                   height <= 4096;
        }

        void LinkIntoList(uint8_t* texture)
        {
            uint8_t** const head = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kTextureListHead));
            if (head == nullptr)
            {
                return;
            }

            if (*head != nullptr)
            {
                WritePointer(*head, GameAddress::kTexturePreviousOffset, texture);
            }
            WritePointer(texture, GameAddress::kTextureNextOffset, *head);
            *head = texture;
        }

        int __cdecl ApiGetCount(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int total = 0;
            for (uint8_t* walk = ListHead(); IsTexture(walk) && total < kWalkLimit;
                 walk = NextTexture(walk))
            {
                ++total;
            }
            return total;
        }

        AmodTextureId __cdecl ApiGetAt(AmodMod mod, int index)
        {
            if (Caller(mod) == nullptr || index < 0)
            {
                return AMOD_INVALID_TEXTURE;
            }

            int at = 0;
            for (uint8_t* walk = ListHead(); IsTexture(walk) && at < kWalkLimit;
                 walk = NextTexture(walk), ++at)
            {
                if (at == index)
                {
                    return IdOf(walk);
                }
            }
            return AMOD_INVALID_TEXTURE;
        }

        AmodTextureId __cdecl ApiFind(AmodMod mod, const char* name)
        {
            const std::string wanted = Normalised(name);
            if (Caller(mod) == nullptr || wanted.empty())
            {
                return AMOD_INVALID_TEXTURE;
            }

            int seen = 0;
            for (uint8_t* walk = ListHead(); IsTexture(walk) && seen < kWalkLimit;
                 walk = NextTexture(walk), ++seen)
            {
                if (!MadeFromPixels(walk) &&
                    Normalised(ReadText(walk, GameAddress::kTextureNameOffset)) == wanted)
                {
                    return IdOf(walk);
                }
            }
            return AMOD_INVALID_TEXTURE;
        }

        int __cdecl ApiGet(AmodMod mod, AmodTextureId id, AmodTexture* out)
        {
            ModContext* const context = Caller(mod);
            const uint8_t* const texture = context != nullptr ? Resolve(id) : nullptr;
            if (texture == nullptr || out == nullptr)
            {
                return 0;
            }

            const bool made = MadeFromPixels(texture);

            AmodTexture filled = {};
            filled.structSize = sizeof(filled);
            filled.name = made ? "" : ReadText(texture, GameAddress::kTextureNameOffset);
            filled.width = Read<int>(texture, GameAddress::kTextureWidthOffset);
            filled.height = Read<int>(texture, GameAddress::kTextureHeightOffset);
            filled.mipmapped = texture[GameAddress::kTextureMipOffset] != 0 ? 1 : 0;
            filled.hasAlpha = Read<int>(texture, GameAddress::kTextureAlphaOffset) != 0 ? 1 : 0;
            filled.madeHere = made ? 1 : 0;
            filled.references = Read<int>(texture, GameAddress::kTextureReferencesOffset);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiIsValid(AmodMod mod, AmodTextureId id)
        {
            return (Caller(mod) != nullptr && Resolve(id) != nullptr) ? 1 : 0;
        }

        AmodTextureId __cdecl ApiLoad(AmodMod mod, const char* path, int mipmapped, int hasAlpha)
        {
            ModContext* const context = Caller(mod);
            const LoadOrFindRoutine load = Routine<LoadOrFindRoutine>(GameAddress::kLoadOrFindTexture);

            if (context == nullptr || load == nullptr || path == nullptr || path[0] == '\0' || Manager() == nullptr)
            {
                return AMOD_INVALID_TEXTURE;
            }

            uint8_t* const texture = load(Manager(), nullptr, path, mipmapped, hasAlpha);
            if (!IsTexture(texture))
            {
                return AMOD_INVALID_TEXTURE;
            }

            Held held;
            held.owner = context;
            held.texture = texture;
            g_held.push_back(held);
            return IdOf(texture);
        }

        void GiveBack(uint8_t* texture)
        {
            const ReleaseRoutine release = Routine<ReleaseRoutine>(GameAddress::kTextureRelease);
            if (release != nullptr && Manager() != nullptr && IsTexture(texture) && Listed(texture))
            {
                release(Manager(), nullptr, texture);
            }
        }

        int __cdecl ApiRelease(AmodMod mod, AmodTextureId id)
        {
            ModContext* const context = Caller(mod);
            uint8_t* const texture = context != nullptr ? Resolve(id) : nullptr;
            if (texture == nullptr)
            {
                return 0;
            }

            for (auto it = g_held.begin(); it != g_held.end(); ++it)
            {
                if (it->owner == context && it->texture == texture)
                {
                    g_held.erase(it);
                    GiveBack(texture);
                    return 1;
                }
            }
            return 0;
        }

        AmodTextureId __cdecl ApiCreate(AmodMod mod, int width, int height, const void* pixels, int mipmapped, int hasAlpha)
        {
            ModContext* const context = Caller(mod);
            const BuildRoutine build = Routine<BuildRoutine>(GameAddress::kTextureBuild);
            const ConstructRoutine construct = Routine<ConstructRoutine>(GameAddress::kTextureConstruct);

            if (context == nullptr || build == nullptr || construct == nullptr ||
                !SaneSize(width, height, pixels))
            {
                return AMOD_INVALID_TEXTURE;
            }

            uint8_t* const texture = static_cast<uint8_t*>(GameFunctions::Allocate(static_cast<int>(GameAddress::kTextureSize), ".\\TexManager.cpp", 0x188));
            uint8_t* const buffer = texture != nullptr ? CopyPixels(width, height, pixels) : nullptr;

            if (texture == nullptr || buffer == nullptr)
            {
                GameFunctions::Free(buffer);
                GameFunctions::Free(texture);
                return AMOD_INVALID_TEXTURE;
            }

            construct(texture);
            WriteInt(texture, GameAddress::kTextureMagicOffset, static_cast<int>(GameAddress::kTextureMagic));
            texture[GameAddress::kTextureFromPixelsOffset] = 1;
            texture[GameAddress::kTextureMipOffset] = mipmapped != 0 ? 1 : 0;

            WritePointer(texture, GameAddress::kTexturePixelsOffset, buffer);
            WriteInt(texture, GameAddress::kTextureAlphaOffset, hasAlpha != 0 ? 1 : 0);
            WriteInt(texture, GameAddress::kTextureWidthOffset, width);
            WriteInt(texture, GameAddress::kTextureHeightOffset, height);

            if (!BuildGuarded(build, texture))
            {
                GameFunctions::Free(buffer);
                GameFunctions::Free(texture);
                FORGE_LOG_ERROR("A %dx%d texture would not be made", width, height);
                return AMOD_INVALID_TEXTURE;
            }

            LinkIntoList(texture);

            Made made;
            made.owner = context;
            made.texture = texture;
            g_made.push_back(made);
            return IdOf(texture);
        }

        int __cdecl ApiReplace(AmodMod mod, AmodTextureId id, int width, int height, const void* pixels)
        {
            ModContext* const context = Caller(mod);
            uint8_t* const texture = context != nullptr ? Resolve(id) : nullptr;
            const BuildRoutine build = Routine<BuildRoutine>(GameAddress::kTextureBuild);

            if (texture == nullptr || build == nullptr || !SaneSize(width, height, pixels))
            {
                return 0;
            }

            uint8_t* const buffer = CopyPixels(width, height, pixels);
            if (buffer == nullptr)
            {
                return 0;
            }

            uint8_t* const previous = reinterpret_cast<uint8_t*>(Read<uintptr_t>(texture, GameAddress::kTexturePixelsOffset));
            bool taken = false;     // whether g_repainted owns previous

            if (!AlreadyRepainted(texture))
            {
                const bool fromPixels = MadeFromPixels(texture);

                Repainted entry;
                entry.owner = context;
                entry.texture = texture;
                entry.pixels = fromPixels ? previous : nullptr;
                entry.width = Read<int>(texture, GameAddress::kTextureWidthOffset);
                entry.height = Read<int>(texture, GameAddress::kTextureHeightOffset);
                g_repainted.push_back(entry);
                taken = entry.pixels != nullptr;

                if (!fromPixels)
                {
                    texture[GameAddress::kTextureFromPixelsOffset] = 1;
                }
            }

            // Build creates a new surface without releasing the old one.
            ReleaseSurface(texture);

            WritePointer(texture, GameAddress::kTexturePixelsOffset, buffer);
            WriteInt(texture, GameAddress::kTextureWidthOffset, width);
            WriteInt(texture, GameAddress::kTextureHeightOffset, height);

            if (!BuildGuarded(build, texture))
            {
                WritePointer(texture, GameAddress::kTexturePixelsOffset, previous);
                GameFunctions::Free(buffer);
                return 0;
            }

            if (!taken)
            {
                GameFunctions::Free(previous);
            }
            return 1;
        }

        int __cdecl ApiReadPixels(AmodMod mod, AmodTextureId id, void* buffer, int capacity)
        {
            const uint8_t* const texture = Caller(mod) != nullptr ? Resolve(id) : nullptr;
            if (texture == nullptr)
            {
                return 0;
            }

            const uint8_t* const pixels = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(texture, GameAddress::kTexturePixelsOffset));
            const int width = Read<int>(texture, GameAddress::kTextureWidthOffset);
            const int height = Read<int>(texture, GameAddress::kTextureHeightOffset);

            if (pixels == nullptr || width <= 0 || height <= 0 || width > 4096 || height > 4096)
            {
                return 0;
            }

            const int bytes = width * height * 4;
            if (buffer == nullptr)
            {
                return bytes;
            }
            if (capacity <= 0)
            {
                return 0;
            }

            const int copied = capacity < bytes ? capacity : bytes;
            memcpy(buffer, pixels, static_cast<size_t>(copied));
            return copied;
        }

        int __cdecl ApiOverride(AmodMod mod, const char* path, AmodTextureId id)
        {
            ModContext* const context = Caller(mod);
            uint8_t* const texture = context != nullptr ? Resolve(id) : nullptr;
            const std::string wanted = Normalised(path);

            if (texture == nullptr || wanted.empty() || g_originalLoad == nullptr)
            {
                return 0;
            }

            for (Substitute& entry : g_substitutes)
            {
                if (entry.path == wanted)
                {
                    entry.owner = context;
                    entry.texture = texture;
                    return 1;
                }
            }

            Substitute entry;
            entry.owner = context;
            entry.path = wanted;
            entry.texture = texture;
            g_substitutes.push_back(entry);
            return 1;
        }

        int __cdecl ApiClearOverride(AmodMod mod, const char* path)
        {
            ModContext* const context = Caller(mod);
            const std::string wanted = Normalised(path);

            for (auto it = g_substitutes.begin(); it != g_substitutes.end(); ++it)
            {
                if (it->owner == context && it->path == wanted)
                {
                    g_substitutes.erase(it);
                    return 1;
                }
            }
            return 0;
        }
    }

    bool TextureApi::Install()
    {
        if (Manager() == nullptr)
        {
            return false;
        }

        g_table.structSize = sizeof(AmodTextureApi);
        g_table.GetCount = &ApiGetCount;
        g_table.GetAt = &ApiGetAt;
        g_table.Find = &ApiFind;
        g_table.Get = &ApiGet;
        g_table.IsValid = &ApiIsValid;
        g_table.Load = &ApiLoad;
        g_table.Release = &ApiRelease;
        g_table.Create = &ApiCreate;
        g_table.Replace = &ApiReplace;
        g_table.ReadPixels = &ApiReadPixels;

        void* const load = GameSymbols::Address(GameSymbol::LoadOrFindTexture);
        std::string error;

        if (load != nullptr && g_loadHook.Install(load, reinterpret_cast<void*>(&LoadOrFindDetour), kLoadOrFindTexturePrologue, sizeof(kLoadOrFindTexturePrologue), error))
        {
            g_originalLoad = reinterpret_cast<LoadOrFindRoutine>(g_loadHook.Trampoline());
            g_table.Override = &ApiOverride;
            g_table.ClearOverride = &ApiClearOverride;
        }
        else
        {
            FORGE_LOG_WARN("LoadOrFindTexture is not hooked, so a texture cannot stand in for a "
                           "file: %s", error.c_str());
        }

        return true;
    }

    void TextureApi::Uninstall()
    {
        g_loadHook.Uninstall();
        g_originalLoad = nullptr;

        g_made.clear();
        g_held.clear();
        g_substitutes.clear();
        g_repainted.clear();
        g_table = AmodTextureApi{};
    }

    const AmodTextureApi* TextureApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    AmodTextureId TextureApi::IdFor(const void* texture)
    {
        const uint8_t* const walk = static_cast<const uint8_t*>(texture);
        return (walk != nullptr && IsTexture(walk) && Listed(walk)) ? IdOf(walk) : AMOD_INVALID_TEXTURE;
    }

    void* TextureApi::TextureFor(AmodTextureId id)
    {
        return Resolve(id);
    }

    void TextureApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_substitutes.begin(); it != g_substitutes.end();)
        {
            it = it->owner == owner ? g_substitutes.erase(it) : ++it;
        }

        const BuildRoutine build = Routine<BuildRoutine>(GameAddress::kTextureBuild);
        for (auto it = g_repainted.begin(); it != g_repainted.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const texture = it->texture;
            const bool standing = build != nullptr && IsTexture(texture) && Listed(texture);

            if (standing && it->pixels != nullptr)
            {
                // Pixel-only texture: restore kept pixels.
                uint8_t* const painted = reinterpret_cast<uint8_t*>(Read<uintptr_t>(texture, GameAddress::kTexturePixelsOffset));

                ReleaseSurface(texture);
                WritePointer(texture, GameAddress::kTexturePixelsOffset, it->pixels);
                WriteInt(texture, GameAddress::kTextureWidthOffset, it->width);
                WriteInt(texture, GameAddress::kTextureHeightOffset, it->height);

                if (BuildGuarded(build, texture))
                {
                    GameFunctions::Free(painted);
                }
                else
                {
                    // Old pixels failed to build; keep mod's.
                    WritePointer(texture, GameAddress::kTexturePixelsOffset, painted);
                    GameFunctions::Free(it->pixels);
                    BuildGuarded(build, texture);
                }
            }
            else if (standing)
            {
                uint8_t* const pixels = reinterpret_cast<uint8_t*>(Read<uintptr_t>(texture, GameAddress::kTexturePixelsOffset));

                texture[GameAddress::kTextureFromPixelsOffset] = 0;
                WritePointer(texture, GameAddress::kTexturePixelsOffset, nullptr);
                ReleaseSurface(texture);

                if (BuildGuarded(build, texture))
                {
                    GameFunctions::Free(pixels);
                }
                else
                {
                    // File rebuild failed; leave painted rather than blank.
                    texture[GameAddress::kTextureFromPixelsOffset] = 1;
                    WritePointer(texture, GameAddress::kTexturePixelsOffset, pixels);
                    BuildGuarded(build, texture);
                }
            }
            else
            {
                GameFunctions::Free(it->pixels);
            }
            it = g_repainted.erase(it);
        }

        // Collect ids first: release may free while the list is walked.
        std::vector<uint8_t*> giveBack;
        for (auto it = g_held.begin(); it != g_held.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }
            giveBack.push_back(it->texture);
            it = g_held.erase(it);
        }

        for (auto it = g_made.begin(); it != g_made.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }
            giveBack.push_back(it->texture);
            it = g_made.erase(it);
        }

        // Refcount 0 destroy now; else live until last holder releases.
        for (uint8_t* const texture : giveBack)
        {
            GiveBack(texture);
        }
    }
}
