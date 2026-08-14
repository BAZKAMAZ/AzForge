#include <windows.h>

#include "src/Api/SavApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameFunctions.h"
#include "src/Game/GameModule.h"
#include "src/Hook/InlineHook.h"

#include <stdio.h>
#include <string.h>
#include <filesystem>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        AmodSavApi g_table = {};

        template <typename T>
        T Routine(uintptr_t address)
        {
            return reinterpret_cast<T>(GameModule::Resolve(address));
        }

        typedef void(__fastcall* WriteRoutine)(void* packs, void* unused, const char* path);
        typedef void(__fastcall* OpenLevelRoutine)(void* packs, void* unused, const char* levelId);

        struct Blocked
        {
            ModContext* owner;
            std::string id;
        };

        std::vector<Blocked> g_blocked;

        InlineHook g_openHook;
        OpenLevelRoutine g_originalOpen = nullptr;

        bool Refused(const char* levelId)
        {
            for (const Blocked& held : g_blocked)
            {
                if (levelId != nullptr && held.id == levelId)
                {
                    return true;
                }
            }

            return false;
        }

        int __cdecl ApiBlock(AmodMod mod, const char* levelId, int blocked);

        struct LevelChange
        {
            std::string owner;   // the mod's id, not its context: a slot may be loaded before the
            std::string level;   // mod that wrote it, or after it, and ids do not move
            std::string name;
            int value;
        };

        std::vector<LevelChange> g_changes;

        constexpr char kChangeSection[] = "azforge.levelchanges";

        LevelChange* FindChange(const ModContext* owner, const char* level, const char* name)
        {
            for (LevelChange& held : g_changes)
            {
                if (owner != nullptr && level != nullptr && name != nullptr && held.owner == owner->id && held.level == level && held.name == name)
                {
                    return &held;
                }
            }

            return nullptr;
        }

        void PackChanges(std::vector<uint8_t>& out);
        void UnpackChanges(const uint8_t* bytes, int size);

        void __fastcall OpenLevelDetour(void* packs, void* unused, const char* levelId)
        {
            if (Refused(levelId))
            {
                return;
            }

            if (g_originalOpen != nullptr)
            {
                g_originalOpen(packs, unused, levelId);
            }
        }

        uint8_t* Packs()
        {
            uint8_t* const packs = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kGamePacks));
            return Readable(packs, GameAddress::kGamePacksOpenCountOffset + sizeof(int)) ? packs : nullptr;
        }

        int OpenCount(const uint8_t* packs)
        {
            const int count = packs != nullptr
                ? Read<int>(packs, GameAddress::kGamePacksOpenCountOffset) : 0;

            return (count > 0 && count < 0x1000) ? count : 0;
        }

        uint8_t* OpenList(const uint8_t* packs)
        {
            uint8_t* const list = packs != nullptr
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(packs, GameAddress::kGamePacksOpenOffset))
                : nullptr;

            const size_t wanted = static_cast<size_t>(OpenCount(packs)) * GameAddress::kOpenLevelSize;
            return (list != nullptr && wanted != 0 && Readable(list, wanted)) ? list : nullptr;
        }

        const char* IdAt(const uint8_t* list, int index)
        {
            return reinterpret_cast<const char*>(list + static_cast<size_t>(index) * GameAddress::kOpenLevelSize);
        }

        int IndexOf(const uint8_t* packs, const char* levelId)
        {
            const uint8_t* const list = OpenList(packs);
            const int count = OpenCount(packs);

            if (list == nullptr || levelId == nullptr || levelId[0] == '\0')
            {
                return -1;
            }

            for (int at = 0; at < count; ++at)
            {
                if (strncmp(IdAt(list, at), levelId, GameAddress::kOpenLevelSize) == 0)
                {
                    return at;
                }
            }

            return -1;
        }

        int __cdecl ApiCount(AmodMod mod)
        {
            return OpenCount(Caller(mod) != nullptr ? Packs() : nullptr);
        }

        const char* __cdecl ApiNameAt(AmodMod mod, int index)
        {
            const uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            const uint8_t* const list = OpenList(packs);

            return (list != nullptr && index >= 0 && index < OpenCount(packs)) ? IdAt(list, index) : "";
        }

        int __cdecl ApiIsOpen(AmodMod mod, const char* levelId)
        {
            return (Caller(mod) != nullptr && IndexOf(Packs(), levelId) >= 0) ? 1 : 0;
        }

        int __cdecl ApiOpen(AmodMod mod, const char* levelId)
        {
            uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            const OpenLevelRoutine open = Routine<OpenLevelRoutine>(GameAddress::kGamePacksOpenLevel);

            if (packs == nullptr || open == nullptr || levelId == nullptr || levelId[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Open wants a level id");
            }

            // Opening lifts the block, or the detour would refuse the very call made here
            ApiBlock(mod, levelId, 0);

            const int already = IndexOf(packs, levelId);

            if (already >= 0)
            {
                return already;
            }

            open(packs, nullptr, levelId);

            return IndexOf(packs, levelId);
        }

        int __cdecl ApiBlock(AmodMod mod, const char* levelId, int blocked)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || levelId == nullptr || levelId[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Block wants a level id");
            }

            for (auto it = g_blocked.begin(); it != g_blocked.end(); ++it)
            {
                if (it->id == levelId)
                {
                    if (blocked == 0)
                    {
                        g_blocked.erase(it);
                    }
                    return 1;
                }
            }

            if (blocked != 0)
            {
                g_blocked.push_back(Blocked{ caller, levelId });
            }

            return 1;
        }

        int __cdecl ApiIsBlocked(AmodMod mod, const char* levelId)
        {
            return (Caller(mod) != nullptr && Refused(levelId)) ? 1 : 0;
        }

        int __cdecl ApiClose(AmodMod mod, const char* levelId)
        {
            uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            const int at = packs != nullptr ? IndexOf(packs, levelId) : -1;
            uint8_t* const list = OpenList(packs);

            ApiBlock(mod, levelId, 1);

            if (at < 0 || list == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' is not open", levelId != nullptr ? levelId : "");
            }

            const int count = OpenCount(packs);

            for (int which = at; which + 1 < count; ++which)
                {
                memcpy(list + static_cast<size_t>(which) * GameAddress::kOpenLevelSize, list + static_cast<size_t>(which + 1) * GameAddress::kOpenLevelSize,
                    GameAddress::kOpenLevelSize);
            }

            const int left = count - 1;
            memcpy(packs + GameAddress::kGamePacksOpenCountOffset, &left, sizeof(left));
            return 1;
        }

        int __cdecl ApiRemove(AmodMod mod, int index)
        {
            uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            uint8_t* const list = OpenList(packs);
            const int count = OpenCount(packs);

            if (list == nullptr || index < 0 || index >= count)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no open level %d", index);
            }

            // Blocked as well, or the CASTLES screen puts it straight back - it reopens everything
            // its $enabled_castles list names, every time it is built
            ApiBlock(mod, IdAt(list, index), 1);

            for (int at = index; at + 1 < count; ++at)
            {
                memcpy(list + static_cast<size_t>(at) * GameAddress::kOpenLevelSize, list + static_cast<size_t>(at + 1) * GameAddress::kOpenLevelSize,
                    GameAddress::kOpenLevelSize);
            }

            const int left = count - 1;
            memcpy(packs + GameAddress::kGamePacksOpenCountOffset, &left, sizeof(left));
            return 1;
        }

        int __cdecl ApiSetId(AmodMod mod, int index, const char* levelId)
        {
            uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            uint8_t* const list = OpenList(packs);

            if (list == nullptr || index < 0 || index >= OpenCount(packs) || levelId == nullptr || levelId[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no open level %d to name", index);
            }

            uint8_t* const at = list + static_cast<size_t>(index) * GameAddress::kOpenLevelSize;
            memset(at, 0, GameAddress::kOpenLevelSize);
            strncpy_s(reinterpret_cast<char*>(at), GameAddress::kOpenLevelSize, levelId, GameAddress::kOpenLevelSize - 1);

            return 1;
        }

        int __cdecl ApiCurrent(AmodMod mod)
        {
            const uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            return packs != nullptr ? Read<int>(packs, GameAddress::kGamePacksCurrentOffset) : 0;
        }

        int __cdecl ApiPackCount(AmodMod mod)
        {
            const uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            const int count = packs != nullptr ? Read<int>(packs, GameAddress::kGamePacksTotalOffset) : 0;

            return (count > 0 && count < 0x1000) ? count : 0;
        }

        int __cdecl ApiSetCurrent(AmodMod mod, int pack)
        {
            uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;

            if (packs == nullptr || pack < 0 || pack >= ApiPackCount(mod))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no pack %d", pack);
            }

            memcpy(packs + GameAddress::kGamePacksCurrentOffset, &pack, sizeof(pack));
            return 1;
        }

        std::filesystem::path FullPath(const char* path)
        {
            return Paths::GameDirectory() / (path != nullptr ? path : "");
        }

        bool LevelIsLoaded()
        {
            const uint8_t* const level = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kLevel));

            if (level == nullptr || !Readable(level, GameAddress::kLevelRoomCountOffset + sizeof(int)))
            {
                return false;
            }

            return Read<int>(level, GameAddress::kLevelRoomCountOffset) > 0 && Read<uintptr_t>(level, GameAddress::kLevelRoomsOffset) != 0;
        }

        typedef char(__cdecl* SaveToFileRoutine)(const char* path, const char* caption);
        typedef char(__cdecl* LoadFromFileRoutine)(const char* path);
        typedef int(__cdecl* ReadHeaderRoutine)(const char* path, char* out);

        char g_slotPath[64] = {};
        char g_slotText[GameAddress::kSaveHeaderSize] = {};

        const char* SlotPath(int slot)
        {
            snprintf(g_slotPath, sizeof(g_slotPath), "saves/save%03i.sav", slot);
            return g_slotPath;
        }

        bool ReadHeader(int slot, char* out)
        {
            const ReadHeaderRoutine read = Routine<ReadHeaderRoutine>(GameAddress::kGameReadSaveHeader);

            if (read == nullptr || slot < 0)
            {
                return false;
            }

            memset(out, 0, GameAddress::kSaveHeaderSize);
            return read(SlotPath(slot), out) != 0;
        }

        bool SlotIsWhole(int slot)
        {
            char header[GameAddress::kSaveHeaderSize] = {};

            if (!ReadHeader(slot, header))
            {
                return false;
            }

            std::error_code sizing;
            const auto size = std::filesystem::file_size(FullPath(SlotPath(slot)), sizing);

            return !sizing && size > GameAddress::kSaveHeaderSize;
        }

        int __cdecl ApiSlotExists(AmodMod mod, int slot)
        {
            return (Caller(mod) != nullptr && SlotIsWhole(slot)) ? 1 : 0;
        }

        const char* TextFromHeader(int slot, uintptr_t offset, size_t most)
        {
            char header[GameAddress::kSaveHeaderSize] = {};

            g_slotText[0] = '\0';

            if (!ReadHeader(slot, header))
            {
                return g_slotText;
            }

            size_t at = 0;

            while (at < most && at < sizeof(g_slotText) - 1 && header[offset + at] != '\0')
            {
                g_slotText[at] = header[offset + at];
                ++at;
            }

            g_slotText[at] = '\0';
            return g_slotText;
        }

        const char* __cdecl ApiSlotName(AmodMod mod, int slot)
        {
            return Caller(mod) != nullptr
                ? TextFromHeader(slot, GameAddress::kSaveCaptionOffset, GameAddress::kSaveCaptionSize)
                : "";
        }

        const char* __cdecl ApiSlotLevel(AmodMod mod, int slot)
        {
            return Caller(mod) != nullptr
                ? TextFromHeader(slot, GameAddress::kSaveLevelOffset, 0x100)
                : "";
        }

        bool GuardedSave(SaveToFileRoutine save, const char* path, const char* caption, char& written)
        {
            __try
            {
                written = save(path, caption);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool GuardedLoad(LoadFromFileRoutine load, const char* path)
        {
            __try
            {
                load(path);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        int __cdecl ApiSaveSlot(AmodMod mod, int slot, const char* caption)
        {
            const SaveToFileRoutine save = Routine<SaveToFileRoutine>(GameAddress::kGameSaveToFile);

            if (Caller(mod) == nullptr || save == nullptr || slot < 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no slot %d to save to", slot);
            }
            if (!LevelIsLoaded())
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE,
                    "there is no level being played, and a save with no level in it is not a save");
            }

            char written = 0;

            if (!GuardedSave(save, SlotPath(slot), caption != nullptr ? caption : "", written))
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "the game refused to save slot %d", slot);
            }

            return written != 0 ? 1 : Fail(mod, AMOD_FAIL_REFUSED, "slot %d would not be written", slot);
        }

        int __cdecl ApiLoadSlot(AmodMod mod, int slot)
        {
            const LoadFromFileRoutine load = Routine<LoadFromFileRoutine>(GameAddress::kGameLoadFromFile);
            char header[GameAddress::kSaveHeaderSize] = {};

            if (Caller(mod) == nullptr || load == nullptr || !ReadHeader(slot, header))
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no save in slot %d", slot);
            }

            // A header on its own is what a failed save leaves behind, and it passes every check
            // the game makes. Anything that short has no game in it.
            std::error_code sizing;
            const auto size = std::filesystem::file_size(FullPath(SlotPath(slot)), sizing);

            if (sizing || size <= GameAddress::kSaveHeaderSize)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "slot %d holds only a header - the save it came from never finished", slot);
            }

            if (!GuardedLoad(load, SlotPath(slot)))
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "the game refused to load slot %d", slot);
            }

            return 1;
        }

        int __cdecl ApiDeleteSlot(AmodMod mod, int slot)
        {
            if (Caller(mod) == nullptr || slot < 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no slot %d", slot);
            }

            const std::filesystem::path where = Paths::GameDirectory() / SlotPath(slot);
            std::error_code removal;

            return std::filesystem::remove(where, removal)
                ? 1
                : Fail(mod, AMOD_FAIL_NO_SUCH, "slot %d holds nothing to delete", slot);
        }

        constexpr char kModSectionMagic[8] = { 'A', 'M', 'O', 'D', 'S', 'A', 'V', 'E' };
        constexpr size_t kModIdSize = 64;

        struct SlotHandler
        {
            ModContext* owner;
            AmodToken token;
            AmodSlotSaveHandler save;
            AmodSlotLoadHandler load;
            void* user;
        };

        std::vector<SlotHandler> g_slotHandlers;
        AmodToken g_nextToken = 1;

        // Filled by WriteSlotData while one mod's handler runs
        std::vector<uint8_t> g_pending;
        bool g_collecting = false;

        InlineHook g_saveHook;
        InlineHook g_loadHook;
        SaveToFileRoutine g_originalSave = nullptr;
        LoadFromFileRoutine g_originalLoad = nullptr;

        int SlotOfPath(const char* path)
        {
            if (path == nullptr)
            {
                return -1;
            }

            const size_t length = strlen(path);
            size_t last = length;

            while (last > 0 && (path[last - 1] < '0' || path[last - 1] > '9'))
            {
                --last;
            }

            if (last == 0)
            {
                return -1;
            }

            size_t first = last;

            while (first > 0 && path[first - 1] >= '0' && path[first - 1] <= '9')
            {
                --first;
            }

            return atoi(path + first);
        }

        void AppendModSections(const char* path, int slot)
        {
            if (g_slotHandlers.empty())
            {
                return;
            }

            std::vector<uint8_t> trailer;
            uint32_t written = 0;

            for (const SlotHandler& held : g_slotHandlers)
            {
                if (held.save == nullptr)
                {
                    continue;
                }

                g_pending.clear();
                g_collecting = true;
                held.save(slot, held.user);
                g_collecting = false;

                if (g_pending.empty())
                {
                    continue;
                }

                char id[kModIdSize] = {};
                const std::string& name = held.owner->id;
                memcpy(id, name.c_str(), name.size() < kModIdSize - 1 ? name.size() : kModIdSize - 1);

                const uint32_t size = static_cast<uint32_t>(g_pending.size());

                trailer.insert(trailer.end(), id, id + kModIdSize);
                trailer.insert(trailer.end(), reinterpret_cast<const uint8_t*>(&size),
                    reinterpret_cast<const uint8_t*>(&size) + sizeof(size));
                trailer.insert(trailer.end(), g_pending.begin(), g_pending.end());
                ++written;
            }

            // AzForge's own section, so a mod gets remembered changes without registering a
            // handler and without spending the one section of its own
            std::vector<uint8_t> packed;
            PackChanges(packed);

            if (!packed.empty())
            {
                char id[kModIdSize] = {};
                memcpy(id, kChangeSection, sizeof(kChangeSection) - 1);

                const uint32_t size = static_cast<uint32_t>(packed.size());

                trailer.insert(trailer.end(), id, id + kModIdSize);
                trailer.insert(trailer.end(), reinterpret_cast<const uint8_t*>(&size), reinterpret_cast<const uint8_t*>(&size) + sizeof(size));
                trailer.insert(trailer.end(), packed.begin(), packed.end());
                ++written;
            }

            if (written == 0)
            {
                return;
            }

            std::vector<uint8_t> head(kModSectionMagic, kModSectionMagic + sizeof(kModSectionMagic));
            head.insert(head.end(), reinterpret_cast<const uint8_t*>(&written), reinterpret_cast<const uint8_t*>(&written) + sizeof(written));

            FILE* file = nullptr;

            if (fopen_s(&file, FullPath(path).string().c_str(), "ab") != 0 || file == nullptr)
            {
                FORGE_LOG_WARN("A save was written but no mod section could be added to '%s'", path);
                return;
            }

            fwrite(head.data(), 1, head.size(), file);
            fwrite(trailer.data(), 1, trailer.size(), file);
            fclose(file);
        }

        void HandModSections(const char* path, int slot)
        {
            std::vector<uint8_t> bytes;
            {
                FILE* file = nullptr;

                if (fopen_s(&file, FullPath(path).string().c_str(), "rb") == 0 && file != nullptr)
                {
                    fseek(file, 0, SEEK_END);
                    const long size = ftell(file);
                    fseek(file, 0, SEEK_SET);

                    if (size > 0 && size < 0x4000000)
                    {
                        bytes.resize(static_cast<size_t>(size));
                        fread(bytes.data(), 1, bytes.size(), file);
                    }

                    fclose(file);
                }
            }

            // From the end, because what the game wrote before it has no fixed length
            size_t at = 0;
            bool found = false;

            for (size_t back = bytes.size(); back >= sizeof(kModSectionMagic) + sizeof(uint32_t); --back)
            {
                const size_t start = back - sizeof(kModSectionMagic) - sizeof(uint32_t);

                if (memcmp(bytes.data() + start, kModSectionMagic, sizeof(kModSectionMagic)) == 0)
                {
                    at = start + sizeof(kModSectionMagic);
                    found = true;
                    break;
                }
            }

            uint32_t count = 0;

            if (found)
            {
                memcpy(&count, bytes.data() + at, sizeof(count));
                at += sizeof(count);
            }

            {
                bool restored = false;
                size_t walk = at;

                for (uint32_t which = 0; which < count; ++which)
                {
                    if (walk + kModIdSize + sizeof(uint32_t) > bytes.size())
                    {
                        break;
                    }

                    const char* const id = reinterpret_cast<const char*>(bytes.data() + walk);
                    uint32_t held_size = 0;
                    memcpy(&held_size, bytes.data() + walk + kModIdSize, sizeof(held_size));

                    const size_t body = walk + kModIdSize + sizeof(held_size);

                    if (body + held_size > bytes.size())
                    {
                        break;
                    }

                    if (strcmp(id, kChangeSection) == 0)
                    {
                        UnpackChanges(bytes.data() + body, static_cast<int>(held_size));
                        restored = true;
                        break;
                    }

                    walk = body + held_size;
                }

                if (!restored)
                {
                    g_changes.clear();
                }
            }

            for (const SlotHandler& held : g_slotHandlers)
            {
                if (held.load == nullptr)
                {
                    continue;
                }

                const uint8_t* mine = nullptr;
                uint32_t size = 0;
                size_t walk = at;

                for (uint32_t which = 0; which < count; ++which)
                {
                    if (walk + kModIdSize + sizeof(uint32_t) > bytes.size())
                    {
                        break;
                    }

                    const char* const id = reinterpret_cast<const char*>(bytes.data() + walk);
                    uint32_t held_size = 0;
                    memcpy(&held_size, bytes.data() + walk + kModIdSize, sizeof(held_size));

                    const size_t body = walk + kModIdSize + sizeof(held_size);

                    if (body + held_size > bytes.size())
                    {
                        break;
                    }

                    if (held.owner->id == id)
                    {
                        mine = bytes.data() + body;
                        size = held_size;
                        break;
                    }

                    walk = body + held_size;
                }

                held.load(slot, mine, static_cast<int>(size), held.user);
            }
        }

        char __cdecl SaveToFileDetour(const char* path, const char* caption)
        {
            const char written = g_originalSave != nullptr ? g_originalSave(path, caption) : 0;

            if (written != 0)
            {
                AppendModSections(path, SlotOfPath(path));
            }

            return written;
        }

        char __cdecl LoadFromFileDetour(const char* path)
        {
            const char read = g_originalLoad != nullptr ? g_originalLoad(path) : 0;

            // After the game, so a mod restoring its own state sees the level it belongs to
            HandModSections(path, SlotOfPath(path));
            return read;
        }

        AmodToken __cdecl ApiOnSlotSave(AmodMod mod, AmodSlotSaveHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnSlotSave wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_slotHandlers.push_back(SlotHandler{ caller, g_nextToken, handler, nullptr, user });
            return g_nextToken++;
        }

        AmodToken __cdecl ApiOnSlotLoad(AmodMod mod, AmodSlotLoadHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnSlotLoad wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_slotHandlers.push_back(SlotHandler{ caller, g_nextToken, nullptr, handler, user });
            return g_nextToken++;
        }

        void __cdecl ApiCancel(AmodMod mod, AmodToken token)
        {
            ModContext* const caller = Caller(mod);

            for (auto it = g_slotHandlers.begin(); it != g_slotHandlers.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_slotHandlers.erase(it);
                    return;
                }
            }
        }

        int __cdecl ApiSetLevelChange(AmodMod mod, const char* level, const char* name, int value)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || level == nullptr || level[0] == '\0' ||
                name == nullptr || name[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a change wants a level and a name");
            }

            LevelChange* const held = FindChange(caller, level, name);

            if (held != nullptr)
            {
                held->value = value;
                return 1;
            }

            g_changes.push_back(LevelChange{ caller->id, level, name, value });
            return 1;
        }

        int __cdecl ApiGetLevelChange(AmodMod mod, const char* level, const char* name, int fallback)
        {
            const LevelChange* const held = FindChange(Caller(mod), level, name);
            return held != nullptr ? held->value : fallback;
        }

        int __cdecl ApiHasLevelChange(AmodMod mod, const char* level, const char* name)
        {
            return FindChange(Caller(mod), level, name) != nullptr ? 1 : 0;
        }

        int __cdecl ApiClearLevelChange(AmodMod mod, const char* level, const char* name)
        {
            ModContext* const caller = Caller(mod);

            for (auto it = g_changes.begin(); it != g_changes.end(); ++it)
            {
                if (caller != nullptr && level != nullptr && name != nullptr &&
                    it->owner == caller->id && it->level == level && it->name == name)
                {
                    g_changes.erase(it);
                    return 1;
                }
            }

            return Fail(mod, AMOD_FAIL_NO_SUCH, "nothing was remembered for '%s'", name != nullptr ? name : "");
        }

        int __cdecl ApiLevelChangeCount(AmodMod mod)
        {
            ModContext* const caller = Caller(mod);
            int count = 0;

            for (const LevelChange& held : g_changes)
            {
                count += (caller != nullptr && held.owner == caller->id) ? 1 : 0;
            }

            return count;
        }

        int __cdecl ApiLevelChangeAt(AmodMod mod, int index, AmodLevelChange* out)
        {
            ModContext* const caller = Caller(mod);
            int at = 0;

            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "LevelChangeAt wants somewhere to answer");
            }

            for (const LevelChange& held : g_changes)
            {
                if (caller == nullptr || held.owner != caller->id)
                {
                    continue;
                }
                if (at == index)
                {
                    AmodLevelChange filled = {};
                    filled.structSize = sizeof(filled);
                    filled.level = held.level.c_str();
                    filled.name = held.name.c_str();
                    filled.value = held.value;

                    return CopyOut(out, out->structSize, &filled, sizeof(filled));
                }

                ++at;
            }

            return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no remembered change %d", index);
        }

        void PackChanges(std::vector<uint8_t>& out)
        {
            for (const LevelChange& held : g_changes)
            {
                char line[512] = {};
                const int size = snprintf(line, sizeof(line), "%s\t%s\t%s\t%d\n",
                    held.owner.c_str(), held.level.c_str(),
                    held.name.c_str(), held.value);

                if (size > 0)
                {
                    out.insert(out.end(), line, line + size);
                }
            }
        }

        void UnpackChanges(const uint8_t* bytes, int size)
        {
            // A slot decides what is remembered: what was set before loading it is not this save's
            g_changes.clear();

            std::string text(reinterpret_cast<const char*>(bytes), static_cast<size_t>(size));
            size_t at = 0;

            while (at < text.size())
            {
                const size_t end = text.find('\n', at);
                const std::string line = text.substr(at, end == std::string::npos ? end : end - at);
                at = end == std::string::npos ? text.size() : end + 1;

                const size_t first = line.find('\t');
                const size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
                const size_t third = second == std::string::npos ? second : line.find('\t', second + 1);

                if (third == std::string::npos)
                {
                    continue;
                }

                LevelChange held = {};
                held.owner = line.substr(0, first);
                held.level = line.substr(first + 1, second - first - 1);
                held.name = line.substr(second + 1, third - second - 1);
                held.value = atoi(line.c_str() + third + 1);

                g_changes.push_back(held);
            }
        }

        int __cdecl ApiWriteSlotData(AmodMod mod, const void* bytes, int size)
        {
            if (Caller(mod) == nullptr || !g_collecting)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "WriteSlotData only answers inside a save handler");
            }
            if (bytes == nullptr || size <= 0 || size > 0x100000)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "WriteSlotData wants between 1 byte and a megabyte");
            }

            g_pending.assign(static_cast<const uint8_t*>(bytes),
                static_cast<const uint8_t*>(bytes) + size);
            return 1;
        }

        int __cdecl ApiSave(AmodMod mod)
        {
            uint8_t* const packs = Caller(mod) != nullptr ? Packs() : nullptr;
            const WriteRoutine write = Routine<WriteRoutine>(GameAddress::kGamePacksWrite);

            if (packs == nullptr || write == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the pack list is not up yet");
            }

            write(packs, nullptr, GameAddress::kGamePacksFile);
            return 1;
        }
    }

    bool SavApi::Install()
    {
        if (GameModule::Resolve(GameAddress::kGamePacks) == nullptr ||
            Routine<WriteRoutine>(GameAddress::kGamePacksWrite) == nullptr)
        {
            FORGE_LOG_ERROR("The pack list is not where it was expected, progress is not offered");
            return false;
        }

        g_table.structSize = sizeof(AmodSavApi);
        g_table.Count = &ApiCount;
        g_table.NameAt = &ApiNameAt;
        g_table.IsOpen = &ApiIsOpen;
        g_table.Open = &ApiOpen;
        g_table.Close = &ApiClose;
        g_table.Current = &ApiCurrent;
        g_table.SetCurrent = &ApiSetCurrent;
        g_table.PackCount = &ApiPackCount;
        g_table.Save = &ApiSave;
        g_table.Block = &ApiBlock;
        g_table.IsBlocked = &ApiIsBlocked;
        g_table.Remove = &ApiRemove;
        g_table.SetId = &ApiSetId;
        g_table.SlotExists = &ApiSlotExists;
        g_table.SlotName = &ApiSlotName;
        g_table.SlotLevel = &ApiSlotLevel;
        g_table.SaveSlot = &ApiSaveSlot;
        g_table.LoadSlot = &ApiLoadSlot;
        g_table.DeleteSlot = &ApiDeleteSlot;
        g_table.OnSlotSave = &ApiOnSlotSave;
        g_table.OnSlotLoad = &ApiOnSlotLoad;
        g_table.Cancel = &ApiCancel;
        g_table.WriteSlotData = &ApiWriteSlotData;
        g_table.SetLevelChange = &ApiSetLevelChange;
        g_table.GetLevelChange = &ApiGetLevelChange;
        g_table.HasLevelChange = &ApiHasLevelChange;
        g_table.ClearLevelChange = &ApiClearLevelChange;
        g_table.LevelChangeCount = &ApiLevelChangeCount;
        g_table.LevelChangeAt = &ApiLevelChangeAt;

        void* const target = GameModule::Resolve(GameAddress::kGamePacksOpenLevel);
        std::string error;
        std::string mismatch;

        if (target != nullptr && GameModule::VerifyBytes(target, kGamePacksOpenLevelPrologue, sizeof(kGamePacksOpenLevelPrologue), mismatch) &&
            g_openHook.Install(target, reinterpret_cast<void*>(&OpenLevelDetour), kGamePacksOpenLevelPrologue, sizeof(kGamePacksOpenLevelPrologue), error))
        {
            g_originalOpen = reinterpret_cast<OpenLevelRoutine>(g_openHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("GamePacks_OpenLevel is not hooked, so a closed level will not stay closed: %s", error.empty() ? mismatch.c_str() : error.c_str());
        }

        void* const saveTarget = GameModule::Resolve(GameAddress::kGameSaveToFile);
        void* const loadTarget = GameModule::Resolve(GameAddress::kGameLoadFromFile);
        std::string saveError;
        std::string loadError;
        std::string saveMismatch;
        std::string loadMismatch;

        if (saveTarget != nullptr &&
            GameModule::VerifyBytes(saveTarget, kGameSaveLoadPrologue, sizeof(kGameSaveLoadPrologue), saveMismatch) &&
            g_saveHook.Install(saveTarget, reinterpret_cast<void*>(&SaveToFileDetour),
                kGameSaveLoadPrologue, sizeof(kGameSaveLoadPrologue), saveError) &&
            loadTarget != nullptr &&

            GameModule::VerifyBytes(loadTarget, kGameSaveLoadPrologue, sizeof(kGameSaveLoadPrologue), loadMismatch) &&
            g_loadHook.Install(loadTarget, reinterpret_cast<void*>(&LoadFromFileDetour),
                kGameSaveLoadPrologue, sizeof(kGameSaveLoadPrologue), loadError))
        {
            g_originalSave = reinterpret_cast<SaveToFileRoutine>(g_saveHook.Trampoline());
            g_originalLoad = reinterpret_cast<LoadFromFileRoutine>(g_loadHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("The save file is not hooked, so a mod cannot keep state in one: %s",
                saveError.empty() ? saveMismatch.c_str() : saveError.c_str());
        }

        FORGE_LOG_INFO("Progress interface installed");
        return true;
    }

    void SavApi::Uninstall()
    {
        if (g_openHook.IsInstalled())
        {
            g_openHook.Uninstall();
        }
        if (g_saveHook.IsInstalled())
        {
            g_saveHook.Uninstall();
        }
        if (g_loadHook.IsInstalled())
        {
            g_loadHook.Uninstall();
        }

        g_originalOpen = nullptr;
        g_originalSave = nullptr;
        g_originalLoad = nullptr;
        g_blocked.clear();
        g_slotHandlers.clear();
        g_changes.clear();
        g_table = AmodSavApi{};
    }

    void SavApi::ForgetMod(ModContext* owner)
    {
        // A level a mod opened stays open - that is the player's progress. A block does not: it is
        // the mod's own refusal, and it goes when the mod does.
        for (auto it = g_blocked.begin(); it != g_blocked.end();)
        {
            it = it->owner == owner ? g_blocked.erase(it) : ++it;
        }

        // Its handlers point into the DLL about to go
        for (auto it = g_slotHandlers.begin(); it != g_slotHandlers.end();)
        {
            it = it->owner == owner ? g_slotHandlers.erase(it) : ++it;
        }
    }

    const AmodSavApi* SavApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
