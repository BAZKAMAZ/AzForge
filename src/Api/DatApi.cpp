#include <windows.h>

#include "src/Api/DatApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Mods/ModStore.h"
#include "src/Mods/ModUndo.h"
#include "src/Hook/InlineHook.h"
#include "src/Api/FrameApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"

#include <string.h>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        AmodDatApi g_table = {};

        template <typename T>
        T Routine(uintptr_t address)
        {
            return reinterpret_cast<T>(GameModule::Resolve(address));
        }

        typedef void(__fastcall* SaveRoutine)(void* manager);
        typedef int(__fastcall* CastleOfLevelRoutine)(void* castleList, void* unused, const char* level);
        typedef void*(__fastcall* InsertSlotRoutine)(void* manager, void* unused, int score, int castle);

        uint8_t* CastleList()
        {
            return static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kCastleList));
        }

        uint8_t* Manager()
        {
            uint8_t* const manager = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScoreManager));
            return Readable(manager, GameAddress::kScoreCastleCountOffset + sizeof(int)) ? manager : nullptr;
        }

        int CastleCountOf(const uint8_t* manager)
        {
            const int count = manager != nullptr
                ? Read<int>(manager, GameAddress::kScoreCastleCountOffset) : 0;

            return (count > 0 && count < 0x1000) ? count : 0;
        }

        uint8_t* CastleAt(const uint8_t* manager, int castle)
        {
            if (manager == nullptr || castle < 0 || castle >= CastleCountOf(manager))
            {
                return nullptr;
            }

            const uint8_t* const* const castles = reinterpret_cast<const uint8_t* const*>(
                Read<uintptr_t>(manager, GameAddress::kScoreCastlesOffset));

            if (castles == nullptr || !Readable(castles, static_cast<size_t>(castle + 1) * sizeof(void*)))
            {
                return nullptr;
            }

            uint8_t* const record = const_cast<uint8_t*>(castles[castle]);
            return Readable(record, GameAddress::kCastleEntryCountOffset + sizeof(int)) ? record : nullptr;
        }

        int EntriesIn(const uint8_t* castle)
        {
            const int count = castle != nullptr
                ? Read<int>(castle, GameAddress::kCastleEntryCountOffset) : 0;

            return (count > 0 && count < 0x10000) ? count : 0;
        }

        uint8_t* EntryAt(int index, int& castle, int& inCastle)
        {
            const uint8_t* const manager = Manager();
            const int castles = CastleCountOf(manager);

            int seen = 0;

            for (int at = 0; at < castles; ++at)
            {
                uint8_t* const record = CastleAt(manager, at);
                const int held = EntriesIn(record);

                if (index < seen + held)
                {
                    uint8_t* const entries = reinterpret_cast<uint8_t*>(Read<uintptr_t>(record, GameAddress::kCastleEntriesOffset));

                    const size_t wanted = static_cast<size_t>(index - seen + 1) * GameAddress::kScoreEntrySize;

                    if (entries == nullptr || !Readable(entries, wanted))
                    {
                        return nullptr;
                    }

                    castle = at;
                    inCastle = index - seen;
                    return entries + static_cast<size_t>(inCastle) * GameAddress::kScoreEntrySize;
                }

                seen += held;
            }

            return nullptr;
        }

        void Keep(char* out, size_t capacity, const uint8_t* from, uintptr_t offset)
        {
            const char* const text = reinterpret_cast<const char*>(from + offset);
            size_t at = 0;

            while (at < capacity - 1 && at < GameAddress::kScoreTextSize && text[at] != '\0')
            {
                out[at] = text[at];
                ++at;
            }

            out[at] = '\0';
        }

        char g_name[GameAddress::kScoreTextSize + 1] = {};
        char g_level[GameAddress::kScoreTextSize + 1] = {};

        int __cdecl ApiCount(AmodMod mod)
        {
            const uint8_t* const manager = Caller(mod) != nullptr ? Manager() : nullptr;
            const int castles = CastleCountOf(manager);

            int total = 0;

            for (int at = 0; at < castles; ++at)
            {
                total += EntriesIn(CastleAt(manager, at));
            }

            return total;
        }

        int __cdecl ApiGet(AmodMod mod, int index, AmodScoreEntry* out)
        {
            int castle = -1;
            int inCastle = -1;

            const uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;

            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Get wants somewhere to answer");
            }
            if (entry == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no score %d", index);
            }

            Keep(g_name, sizeof(g_name), entry, GameAddress::kScoreEntryNameOffset);
            Keep(g_level, sizeof(g_level), entry, GameAddress::kScoreEntryLevelOffset);

            AmodScoreEntry filled = {};
            filled.structSize = sizeof(filled);
            filled.name = g_name;
            filled.level = g_level;
            filled.time = Read<float>(entry, GameAddress::kScoreEntryTimeOffset);
            filled.score = Read<int>(entry, GameAddress::kScoreEntryScoreOffset);
            filled.castle = castle;
            filled.inCastle = inCastle;
            filled.filled = entry[GameAddress::kScoreEntryFilledOffset] != 0 ? 1 : 0;

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        struct ScoreWas
        {
            ModContext* owner;
            int kind;               // 0 the mod added it, 1 it changed one, 2 it took one away
            std::string level;
            std::string name;       // how to find it now
            std::string wasName;
            float wasTime;
            int wasScore;
        };

        std::vector<ScoreWas> g_touched;

        bool g_undoing = false;

        typedef void(__fastcall* ScoreSaveRoutine)(void* manager, void* unused);

        InlineHook g_saveHook;
        ScoreSaveRoutine g_originalSave = nullptr;

        std::vector<ModContext*> g_pendingScores;

        int FindScore(AmodMod mod, const char* level, const char* name)
        {
            const int count = ApiCount(mod);

            for (int i = 0; i < count; ++i)
            {
                AmodScoreEntry entry = {};
                entry.structSize = sizeof(entry);

                if (ApiGet(mod, i, &entry) == 0 || entry.level == nullptr || entry.name == nullptr)
                {
                    continue;
                }
                if (strcmp(entry.level, level) == 0 && strcmp(entry.name, name) == 0)
                {
                    return i;
                }
            }

            return -1;
        }

        bool IsKept(ModContext* owner, const std::string& key)
        {
            for (const StoreRecord& kept : ModStore::Records(owner, "scores"))
            {
                if (kept.key == key)
                {
                    return true;
                }
            }
            return false;
        }

        std::string KeyOf(const std::string& level, const std::string& name)
        {
            return level + "|" + name;
        }

        int __cdecl ApiSet(AmodMod mod, int index, const char* name, float time, int score);
        int __cdecl ApiAdd(AmodMod mod, const char* level, const char* name, float time, int score);
        int __cdecl ApiRemove(AmodMod mod, int index);

        void PutEntry(AmodMod mod, const char* level, const char* name, float time, int score)
        {
            const int at = FindScore(mod, level, name);

            if (at >= 0)
            {
                ApiSet(mod, at, name, time, score);
            }
            else
            {
                ApiAdd(mod, level, name, time, score);
            }
        }

        void DropEntry(AmodMod mod, const char* level, const char* name)
        {
            const int at = FindScore(mod, level, name);

            if (at >= 0)
            {
                ApiRemove(mod, at);
            }
        }

        // The table as the game had it before this mod wrote over it.
        void ToOriginal(AmodMod mod, const ScoreWas& held)
        {
            DropEntry(mod, held.level.c_str(), held.name.c_str());

            if (held.kind != 0)
            {
                PutEntry(mod, held.level.c_str(), held.wasName.c_str(), held.wasTime,
                         held.wasScore);
            }
        }

        void RememberScore(AmodMod mod, int index, int kind)
        {
            ModContext* const owner = Caller(mod);
            AmodScoreEntry entry = {};
            entry.structSize = sizeof(entry);

            if (g_undoing || owner == nullptr || (kind != 0 && ApiGet(mod, index, &entry) == 0))
            {
                return;
            }

            ScoreWas held = {};
            held.owner = owner;
            held.kind = kind;
            held.level = entry.level != nullptr ? entry.level : "";
            held.name = entry.name != nullptr ? entry.name : "";
            held.wasName = held.name;
            held.wasTime = entry.time;
            held.wasScore = entry.score;

            g_touched.push_back(held);
        }

        // Once the game has read scores.dat, whoever was waiting gets its kept scores.
        void __cdecl ScoresTick()
        {
            if (g_pendingScores.empty() || CastleCountOf(Manager()) <= 0)
            {
                return;
            }

            const std::vector<ModContext*> waiting = g_pendingScores;
            g_pendingScores.clear();

            for (ModContext* const owner : waiting)
            {
                DatApi::ReplayStore(owner);
            }
        }

        void __fastcall ScoreSaveDetour(void* manager, void* unused)
        {
            AmodMod const any = g_touched.empty() ? nullptr
                : static_cast<AmodMod>(g_touched.front().owner);

            std::vector<ScoreWas> standing;

            const UndoGuard undoing(g_undoing);

            if (any != nullptr)
            {
                for (auto it = g_touched.rbegin(); it != g_touched.rend(); ++it)
                {
                    AmodMod const mod = static_cast<AmodMod>(it->owner);
                    const int at = FindScore(mod, it->level.c_str(), it->name.c_str());

                    ScoreWas now = *it;
                    now.kind = at >= 0 ? 1 : 0;

                    AmodScoreEntry entry = {};
                    entry.structSize = sizeof(entry);

                    if (at >= 0 && ApiGet(mod, at, &entry) != 0)
                    {
                        now.wasName = entry.name != nullptr ? entry.name : it->name;
                        now.wasTime = entry.time;
                        now.wasScore = entry.score;
                    }

                    standing.push_back(now);
                    ToOriginal(mod, *it);
                }
            }

            g_originalSave(manager, unused);

            for (auto it = standing.rbegin(); it != standing.rend(); ++it)
            {
                AmodMod const mod = static_cast<AmodMod>(it->owner);

                if (it->kind == 0)
                {
                    DropEntry(mod, it->level.c_str(), it->name.c_str());
                }
                else
                {
                    PutEntry(mod, it->level.c_str(), it->wasName.c_str(), it->wasTime,
                             it->wasScore);
                }
            }
        }

        int __cdecl ApiKeep(AmodMod mod, int index, int keep, int protect)
        {
            ModContext* const context = Caller(mod);
            AmodScoreEntry entry = {};
            entry.structSize = sizeof(entry);

            if (context == nullptr || ApiGet(mod, index, &entry) == 0 ||
                entry.level == nullptr || entry.name == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no score %d", index);
            }

            const std::string key = KeyOf(entry.level, entry.name);

            if (keep == 0)
            {
                ModStore::Remove(context, "scores", key.c_str());
                return 1;
            }

            StoreRecord record;
            record.kind = "scores";
            record.key = key;
            record.values.push_back(entry.level);
            record.values.push_back(entry.name);
            record.values.push_back(std::to_string(entry.time));
            record.values.push_back(std::to_string(entry.score));
            ModStore::Put(context, record);
            ModStore::Protect(context, "scores", key.c_str(), protect != 0);
            return 1;
        }

        int __cdecl ApiSet(AmodMod mod, int index, const char* name, float time, int score)
        {
            int castle = -1;
            int inCastle = -1;

            uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;

            if (entry != nullptr)
            {
                RememberScore(mod, index, 1);
            }

            if (entry == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no score %d to write", index);
            }

            if (name != nullptr)
            {
                char* const field = reinterpret_cast<char*>(entry + GameAddress::kScoreEntryNameOffset);
                memset(field, 0, GameAddress::kScoreTextSize);
                strncpy_s(field, GameAddress::kScoreTextSize, name, GameAddress::kScoreTextSize - 1);
            }

            memcpy(entry + GameAddress::kScoreEntryTimeOffset, &time, sizeof(time));
            memcpy(entry + GameAddress::kScoreEntryScoreOffset, &score, sizeof(score));

            // Written on, so it counts as filled and the next save keeps it
            entry[GameAddress::kScoreEntryFilledOffset] = 1;
            return 1;
        }

        int __cdecl ApiCastleCount(AmodMod mod)
        {
            return CastleCountOf(Caller(mod) != nullptr ? Manager() : nullptr);
        }

        int __cdecl ApiCastleEntryCount(AmodMod mod, int castle)
        {
            return EntriesIn(CastleAt(Caller(mod) != nullptr ? Manager() : nullptr, castle));
        }

        void Flatten(char* out, size_t capacity, const char* text)
        {
            size_t at = 0;
            size_t put = 0;

            while (text != nullptr && text[at] == ' ')
            {
                ++at;
            }

            if (text != nullptr && (text[at] == '"' || text[at] == '\''))
            {
                ++at;
            }

            while (text != nullptr && text[at] != '\0' && put < capacity - 1)
            {
                char letter = text[at];

                if (letter == '"' || letter == '\'')
                {
                    break;
                }

                if (letter == '\\')
                {
                    letter = '/';
                }
                if (letter >= 'A' && letter <= 'Z')
                {
                    letter = static_cast<char>(letter - 'A' + 'a');
                }

                out[put] = letter;
                ++put;
                ++at;
            }

            while (put > 0 && out[put - 1] == ' ')
            {
                --put;
            }

            out[put] = '\0';
        }

        int CastleOf(const char* level)
        {
            const uint8_t* const manager = Manager();
            const int castles = CastleCountOf(manager);

            if (level == nullptr || level[0] == '\0')
            {
                return -1;
            }

            char wanted[GameAddress::kScoreTextSize + 1] = {};
            Flatten(wanted, sizeof(wanted), level);

            for (int at = 0; at < castles; ++at)
            {
                const uint8_t* const record = CastleAt(manager, at);
                const int held = EntriesIn(record);

                const uint8_t* const entries = record != nullptr
                    ? reinterpret_cast<const uint8_t*>(Read<uintptr_t>(record, GameAddress::kCastleEntriesOffset))
                    : nullptr;

                if (entries == nullptr || !Readable(entries, static_cast<size_t>(held) * GameAddress::kScoreEntrySize))
                {
                    continue;
                }

                for (int which = 0; which < held; ++which)
                {
                    const uint8_t* const entry = entries +
                        static_cast<size_t>(which) * GameAddress::kScoreEntrySize;

                    char held_level[GameAddress::kScoreTextSize + 1] = {};
                    Keep(held_level, sizeof(held_level), entry, GameAddress::kScoreEntryLevelOffset);

                    char flat[GameAddress::kScoreTextSize + 1] = {};
                    Flatten(flat, sizeof(flat), held_level);

                    if (strcmp(flat, wanted) == 0)
                    {
                        return at;
                    }
                }
            }

            return -1;
        }

        int __cdecl ApiCastleOfLevel(AmodMod mod, const char* level)
        {
            uint8_t* const castles = Caller(mod) != nullptr ? CastleList() : nullptr;
            const CastleOfLevelRoutine ask = Routine<CastleOfLevelRoutine>(GameAddress::kCastleIndexOfLevel);

            if (castles == nullptr || ask == nullptr || level == nullptr || level[0] == '\0')
            {
                return -1;
            }

            char wanted[GameAddress::kScoreTextSize + 1] = {};
            Flatten(wanted, sizeof(wanted), level);

            const int said = ask(castles, nullptr, wanted);
            return said >= 0 ? said : CastleOf(level);
        }

        int __cdecl ApiAdd(AmodMod mod, const char* level, const char* name, float time, int score)
        {
            uint8_t* const manager = Caller(mod) != nullptr ? Manager() : nullptr;
            const InsertSlotRoutine insert = Routine<InsertSlotRoutine>(GameAddress::kScoreInsertSlot);
            const int castle = ApiCastleOfLevel(mod, level);

            if (manager == nullptr || insert == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "the score table is not up yet");
                return -1;
            }
            if (castle < 0)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "no castle owns the level '%s'", level != nullptr ? level : "");
                return -1;
            }

            uint8_t* const slot = static_cast<uint8_t*>(insert(manager, nullptr, score, castle));

            if (slot == nullptr)
            {
                Fail(mod, AMOD_FAIL_REFUSED, "%d earns no place in castle %d", score, castle);
                return -1;
            }

            memset(slot, 0, GameAddress::kScoreEntrySize);

            if (name != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(slot + GameAddress::kScoreEntryNameOffset), GameAddress::kScoreTextSize, name, GameAddress::kScoreTextSize - 1);
            }
            if (level != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(slot + GameAddress::kScoreEntryLevelOffset),
                    GameAddress::kScoreTextSize, level, GameAddress::kScoreTextSize - 1);
            }

            memcpy(slot + GameAddress::kScoreEntryTimeOffset, &time, sizeof(time));
            memcpy(slot + GameAddress::kScoreEntryScoreOffset, &score, sizeof(score));

            slot[GameAddress::kScoreEntryFilledOffset] = 1;

            // Said back in the numbering this interface counts in, by finding the slot again
            for (int at = 0; at < ApiCount(mod); ++at)
            {
                int which = -1;
                int inCastle = -1;

                if (EntryAt(at, which, inCastle) == slot)
                {
                    if (g_undoing)
                    {
                        return at;
                    }

                    ScoreWas held = {};
                    held.owner = Caller(mod);
                    held.kind = 0;
                    held.level = level != nullptr ? level : "";
                    held.name = name != nullptr ? name : "";
                    g_touched.push_back(held);
                    return at;
                }
            }

            return 0;
        }

        const char* __cdecl ApiGetName(AmodMod mod, int index)
        {
            int castle = -1;
            int inCastle = -1;

            const uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;

            if (entry == nullptr)
            {
                return "";
            }

            Keep(g_name, sizeof(g_name), entry, GameAddress::kScoreEntryNameOffset);
            return g_name;
        }

        int __cdecl ApiSetName(AmodMod mod, int index, const char* name)
        {
            int castle = -1;
            int inCastle = -1;

            uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;

            if (entry != nullptr)
            {
                RememberScore(mod, index, 1);
            }

            if (entry == nullptr || name == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no score %d to name", index);
            }

            char* const field = reinterpret_cast<char*>(entry + GameAddress::kScoreEntryNameOffset);
            memset(field, 0, GameAddress::kScoreTextSize);
            strncpy_s(field, GameAddress::kScoreTextSize, name, GameAddress::kScoreTextSize - 1);

            entry[GameAddress::kScoreEntryFilledOffset] = 1;
            return 1;
        }

        int __cdecl ApiGetPoint(AmodMod mod, int index)
        {
            int castle = -1;
            int inCastle = -1;

            const uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;
            return entry != nullptr ? Read<int>(entry, GameAddress::kScoreEntryScoreOffset) : 0;
        }

        int __cdecl ApiSetPoint(AmodMod mod, int index, int score)
        {
            int castle = -1;
            int inCastle = -1;

            uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;

            if (entry != nullptr)
            {
                RememberScore(mod, index, 1);
            }

            if (entry == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no score %d to write", index);
            }

            memcpy(entry + GameAddress::kScoreEntryScoreOffset, &score, sizeof(score));
            entry[GameAddress::kScoreEntryFilledOffset] = 1;
            return 1;
        }

        int __cdecl ApiRemove(AmodMod mod, int index)
        {
            int castle = -1;
            int inCastle = -1;

            uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;

            if (entry != nullptr)
            {
                RememberScore(mod, index, 2);
            }
            uint8_t* const record = CastleAt(Manager(), castle);
            const int held = EntriesIn(record);

            if (entry == nullptr || record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no score %d to drop", index);
            }

            uint8_t* const entries = reinterpret_cast<uint8_t*>(Read<uintptr_t>(record, GameAddress::kCastleEntriesOffset));

            // The ones after it close up, and the castle holds one fewer
            for (int at = inCastle; at + 1 < held; ++at)
            {
                memcpy(entries + static_cast<size_t>(at) * GameAddress::kScoreEntrySize, entries + static_cast<size_t>(at + 1) * GameAddress::kScoreEntrySize,
                    GameAddress::kScoreEntrySize);
            }

            const int left = held - 1;
            memcpy(record + GameAddress::kCastleEntryCountOffset, &left, sizeof(left));
            return 1;
        }

        int __cdecl ApiSave(AmodMod mod)
        {
            uint8_t* const manager = Caller(mod) != nullptr ? Manager() : nullptr;
            const SaveRoutine save = Routine<SaveRoutine>(GameAddress::kScoreSaverSave);

            if (manager == nullptr || save == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the score table is not up yet");
            }

            save(manager);
            return 1;
        }
    }

    bool DatApi::Install()
    {
        if (GameModule::Resolve(GameAddress::kScoreManager) == nullptr ||
            Routine<SaveRoutine>(GameAddress::kScoreSaverSave) == nullptr)
        {
            FORGE_LOG_ERROR("The score table is not where it was expected, scores are not offered");
            return false;
        }

        g_table.structSize = sizeof(AmodDatApi);
        g_table.Count = &ApiCount;
        g_table.Get = &ApiGet;
        g_table.Set = &ApiSet;
        g_table.Add = &ApiAdd;
        g_table.CastleCount = &ApiCastleCount;
        g_table.CastleEntryCount = &ApiCastleEntryCount;
        g_table.CastleOfLevel = &ApiCastleOfLevel;
        g_table.Save = &ApiSave;
        g_table.GetName = &ApiGetName;
        g_table.SetName = &ApiSetName;
        g_table.GetPoint = &ApiGetPoint;
        g_table.SetPoint = &ApiSetPoint;
        g_table.Remove = &ApiRemove;
        std::string error;
        std::string mismatch;
        void* const write = GameModule::Resolve(GameAddress::kScoreSaverSave);

        if (write != nullptr &&
            GameModule::VerifyBytes(write, kScoreSaverSavePrologue,
                sizeof(kScoreSaverSavePrologue), mismatch) &&
            g_saveHook.Install(write, reinterpret_cast<void*>(&ScoreSaveDetour),
                kScoreSaverSavePrologue, sizeof(kScoreSaverSavePrologue), error))
        {
            g_originalSave = reinterpret_cast<ScoreSaveRoutine>(g_saveHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("ScoreSaver_Save is not hooked, so a mod's scores are saved into "
                "scores.dat as the player's own");
        }

        FrameApi::AddInternalTick(&ScoresTick, true);

        g_table.Keep = &ApiKeep;

        FORGE_LOG_INFO("Scores interface installed");
        return true;
    }

    void DatApi::Uninstall()
    {
        g_table = AmodDatApi{};
    }

    // What the mod kept from its last run, laid over whatever the player has now.
    void DatApi::ReplayStore(ModContext* owner)
    {
        AmodMod const mod = static_cast<AmodMod>(owner);

        // The table is only there once the game has read scores.dat, which is after the mods.
        if (CastleCountOf(Manager()) <= 0)
        {
            for (const ModContext* const waiting : g_pendingScores)
            {
                if (waiting == owner)
                {
                    return;
                }
            }

            g_pendingScores.push_back(owner);
            return;
        }

        for (const StoreRecord& kept : ModStore::Records(owner, "scores"))
        {
            if (kept.values.size() < 3)
            {
                continue;
            }

            const std::string& level = kept.values[0];
            const std::string& name = kept.values[1];
            const float time = static_cast<float>(atof(kept.values[2].c_str()));
            const int score = kept.values.size() > 3 ? atoi(kept.values[3].c_str()) : 0;

            const int at = FindScore(mod, level.c_str(), name.c_str());

            if (at >= 0)
            {
                ApiSet(mod, at, name.c_str(), time, score);
            }
            else
            {
                ApiAdd(mod, level.c_str(), name.c_str(), time, score);
            }
        }
    }

    void DatApi::ForgetMod(ModContext* owner)
    {
        AmodMod const mod = static_cast<AmodMod>(owner);

        const std::vector<ScoreWas> mine = TakeOwned(g_touched,
            [owner](const ScoreWas& held) { return held.owner == owner; });

        for (auto it = g_pendingScores.begin(); it != g_pendingScores.end();)
        {
            it = *it == owner ? g_pendingScores.erase(it) : ++it;
        }

        const UndoGuard undoing(g_undoing);

        // Newest first: a mod may have written the same entry more than once.
        for (auto it = mine.rbegin(); it != mine.rend(); ++it)
        {
            const std::string key = KeyOf(it->level, it->name);

            if (IsKept(owner, key))
            {
                const int at = FindScore(mod, it->level.c_str(), it->name.c_str());
                AmodScoreEntry entry = {};
                entry.structSize = sizeof(entry);

                if (at >= 0 && ApiGet(mod, at, &entry) != 0)
                {
                    StoreRecord record;
                    record.kind = "scores";
                    record.key = key;
                    record.values.push_back(it->level);
                    record.values.push_back(entry.name != nullptr ? entry.name : it->name);
                    record.values.push_back(std::to_string(entry.time));
                    record.values.push_back(std::to_string(entry.score));
                    ModStore::Put(owner, record);
                }
            }

            ToOriginal(mod, *it);
        }
    }

    const AmodDatApi* DatApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
