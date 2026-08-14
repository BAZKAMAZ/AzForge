#include <windows.h>

#include "src/Api/DatApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
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

        int __cdecl ApiSet(AmodMod mod, int index, const char* name, float time, int score)
        {
            int castle = -1;
            int inCastle = -1;

            uint8_t* const entry = Caller(mod) != nullptr ? EntryAt(index, castle, inCastle) : nullptr;

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

        // Level is a path ("levels/level_t.txt"). Normalize before compare: strip quotes,
        // backslash->slash, lower case -- so LEVELS\LEVEL_T.TXT still matches the table.
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

        FORGE_LOG_INFO("Scores interface installed");
        return true;
    }

    void DatApi::Uninstall()
    {
        g_table = AmodDatApi{};
    }

    void DatApi::ForgetMod(ModContext*)
    {
    }

    const AmodDatApi* DatApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
