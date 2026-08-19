#include <windows.h>

#include "src/Api/DemoApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <string>
#include <vector>

namespace Forge
{
    using ApiRead::Caller;
    using ApiRead::CopyOut;
    using ApiRead::Fail;
    using ApiRead::Read;

    namespace
    {
        typedef void*(__cdecl* ActiveRoutine)();
        typedef void(__fastcall* GetMoveRoutine)(void* player, void* unused, const float* position, int* out, float seconds);
        typedef void(__fastcall* BreakRoutine)(void* player, void* unused);
        typedef void(__fastcall* NextScriptRoutine)(void* player, void* unused);
        typedef char(__fastcall* LoadListRoutine)(void* player, void* unused, void* parser);
        typedef char(__fastcall* ParseScriptRoutine)(void* script, void* unused, const char* path);
        typedef void(__fastcall* FreeMoveCmdsRoutine)(void* script, void* unused);
        typedef void(__cdecl* TimeScaleRoutine)(float scale);

        typedef void*(__cdecl* AllocRoutine)(uint32_t size, const char* file, uint32_t line);
        typedef void*(__cdecl* ReallocRoutine)(void* block, uint32_t size, const char* file, uint32_t line);
        typedef void(__cdecl* FreeRoutine)(void* block);

        struct DemoSub
        {
            ModContext* owner;
            AmodDemoCallback callback;
            void* user;
            AmodToken token;
        };

        struct MoveSub
        {
            ModContext* owner;
            AmodDemoMoveCallback callback;
            void* user;
            AmodToken token;
        };

        struct InputSub
        {
            ModContext* owner;
            AmodDemoInputCallback callback;
            void* user;
            AmodToken token;
        };

        struct Undo
        {
            ModContext* owner;
            int script;
            int move;           // -1 when the field is the script's own
            int index;          // -1 when the field is the move's own
            uintptr_t list;     // which of the move's three lists the index counts in
            uint32_t offset;
            uint32_t length;
            uint8_t before[GameAddress::kDemoCamCmdSize];
        };

        struct Added
        {
            ModContext* owner;
            std::string path;
        };

        AmodDemoApi g_table = {};

        ActiveRoutine g_active = nullptr;
        GetMoveRoutine g_originalGetMove = nullptr;
        BreakRoutine g_originalBreak = nullptr;
        LoadListRoutine g_originalLoadList = nullptr;
        NextScriptRoutine g_nextScript = nullptr;
        ParseScriptRoutine g_parseScript = nullptr;
        FreeMoveCmdsRoutine g_freeMoveCmds = nullptr;

        AllocRoutine g_alloc = nullptr;
        ReallocRoutine g_realloc = nullptr;
        FreeRoutine g_free = nullptr;

        InlineHook g_activeHook;
        InlineHook g_getMoveHook;
        InlineHook g_breakHook;
        InlineHook g_loadListHook;

        std::vector<DemoSub> g_demoSubs;
        std::vector<MoveSub> g_moveSubs;
        std::vector<InputSub> g_inputSubs;
        std::vector<Undo> g_undo;
        std::vector<Added> g_added;
        std::vector<ModContext*> g_suppressors;
        AmodToken g_nextToken = 1;

        bool g_wasPlaying = false;
        int g_lastScript = -1;
        int g_lastMove = -1;
        bool g_broken = false;

        uint8_t* Resolve(uintptr_t address)
        {
            return static_cast<uint8_t*>(GameModule::Resolve(address));
        }

        template <typename T>
        void Write(uint8_t* base, uintptr_t offset, const T& value)
        {
            memcpy(base + offset, &value, sizeof(T));
        }

        // CDemoPlayer, whether or not a demo is playing: the level record carries it at +0x48.
        uint8_t* Player()
        {
            void** const slot = reinterpret_cast<void**>(Resolve(GameAddress::kLevelRecordSlot));
            if (slot == nullptr || *slot == nullptr)
            {
                return nullptr;
            }

            typedef void*(__cdecl* RecordRoutine)();
            void* const record = reinterpret_cast<RecordRoutine>(*slot)();
            if (record == nullptr ||
                *static_cast<uint32_t*>(record) != GameAddress::kLevelRecordMagic)
            {
                return nullptr;
            }

            return *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(record) + GameAddress::kLevelDemoPlayerOffset);
        }

        int ScriptCount(const uint8_t* player)
        {
            return player != nullptr ? Read<int>(player, GameAddress::kDemoPlayerCountOffset) : 0;
        }

        uint8_t* ScriptAt(uint8_t* player, int index)
        {
            if (player == nullptr || index < 0 || index >= ScriptCount(player))
            {
                return nullptr;
            }

            uint8_t* const scripts =
                Read<uint8_t*>(player, GameAddress::kDemoPlayerScriptsOffset);
            return scripts != nullptr ? scripts + index * GameAddress::kDemoScriptSize : nullptr;
        }

        uint8_t* RunningScript(uint8_t* player)
        {
            return player != nullptr
                ? Read<uint8_t*>(player, GameAddress::kDemoPlayerCurrentOffset)
                : nullptr;
        }

        int RunningIndex(uint8_t* player)
        {
            return RunningScript(player) != nullptr
                ? Read<int>(player, GameAddress::kDemoPlayerIndexOffset)
                : -1;
        }

        uint8_t* MoveAt(uint8_t* script, int move)
        {
            if (script == nullptr || move < 0 ||
                move >= Read<int>(script, GameAddress::kDemoScriptMoveCountOffset))
            {
                return nullptr;
            }

            uint8_t* const moves = Read<uint8_t*>(script, GameAddress::kDemoScriptMoveCmdsOffset);
            return moves != nullptr ? moves + move * GameAddress::kDemoMoveCmdSize : nullptr;
        }

        int ListCount(const uint8_t* move, uintptr_t listOffset)
        {
            if (move == nullptr)
            {
                return 0;
            }
            const uint8_t* const list = Read<uint8_t*>(move, listOffset);
            return list != nullptr ? Read<int>(list, GameAddress::kDemoListCountOffset) : 0;
        }

        uint8_t* ListItem(uint8_t* move, uintptr_t listOffset, int index, size_t stride)
        {
            if (move == nullptr || index < 0)
            {
                return nullptr;
            }

            uint8_t* const list = Read<uint8_t*>(move, listOffset);
            if (list == nullptr || index >= Read<int>(list, GameAddress::kDemoListCountOffset))
            {
                return nullptr;
            }

            uint8_t* const items = Read<uint8_t*>(list, GameAddress::kDemoListItemsOffset);
            return items != nullptr ? items + index * stride : nullptr;
        }

        uint8_t* UndoTarget(const Undo& entry)
        {
            uint8_t* const script = ScriptAt(Player(), entry.script);
            if (script == nullptr)
            {
                return nullptr;
            }
            if (entry.move < 0)
            {
                return script + entry.offset;
            }

            uint8_t* const move = MoveAt(script, entry.move);
            if (move == nullptr)
            {
                return nullptr;
            }
            if (entry.index < 0)
            {
                return move + entry.offset;
            }

            const size_t stride = entry.list == GameAddress::kDemoMoveCmdTimeListOffset
                ? GameAddress::kDemoTimeCmdSize
                : GameAddress::kDemoCamCmdSize;

            uint8_t* const item = ListItem(move, entry.list, entry.index, stride);
            return item != nullptr ? item + entry.offset : nullptr;
        }

        void Snapshot(ModContext* owner, int script, int move, int index, uintptr_t list, uint32_t offset, uint32_t length)
        {
            if (length == 0 || length > sizeof(Undo::before))
            {
                return;
            }

            Undo entry = {};
            entry.owner = owner;
            entry.script = script;
            entry.move = move;
            entry.index = index;
            entry.list = list;
            entry.offset = offset;
            entry.length = length;

            for (const Undo& kept : g_undo)
            {
                if (kept.script == script && kept.move == move && kept.index == index && kept.list == list && kept.offset == offset && kept.length == length)
                {
                    return;
                }
            }

            const uint8_t* const address = UndoTarget(entry);
            if (address == nullptr)
            {
                return;
            }

            memcpy(entry.before, address, length);
            g_undo.push_back(entry);
        }

        void Restore(const Undo& entry)
        {
            uint8_t* const address = UndoTarget(entry);
            if (address != nullptr)
            {
                memcpy(address, entry.before, entry.length);
            }
        }

        void Notify(AmodDemoEvent event, int script)
        {
            const std::vector<DemoSub> snapshot = g_demoSubs;
            for (const DemoSub& subscriber : snapshot)
            {
                if (subscriber.callback != nullptr)
                {
                    subscriber.callback(event, script, subscriber.user);
                }
            }
        }

        void NotifyMove(int script, int move)
        {
            const std::vector<MoveSub> snapshot = g_moveSubs;
            for (const MoveSub& subscriber : snapshot)
            {
                if (subscriber.callback != nullptr)
                {
                    subscriber.callback(script, move, subscriber.user);
                }
            }
        }

        void RewindScript(uint8_t* script)
        {
            if (script == nullptr)
            {
                return;
            }

            Write<float>(script, GameAddress::kDemoScriptElapsedOffset, 0.0f);
            Write<int>(script, GameAddress::kDemoScriptMoveIndexOffset, 0);
            Write<uint8_t>(script, GameAddress::kDemoScriptEnteringOffset, 1);
            Write<float>(script, GameAddress::kDemoScriptPauseLeftOffset, 0.0f);
            Write<int>(script, GameAddress::kDemoScriptTimeCursorOffset, 0);
            Write<int>(script, GameAddress::kDemoScriptViewCursorOffset, 0);
            Write<int>(script, GameAddress::kDemoScriptCamCursorOffset, 0);
            Write<uint8_t>(script, GameAddress::kDemoScriptCameraPendingOffset, Read<uint8_t>(script, GameAddress::kDemoScriptHasCameraOffset));

            uint8_t* const moves = Read<uint8_t*>(script, GameAddress::kDemoScriptMoveCmdsOffset);
            const int count = Read<int>(script, GameAddress::kDemoScriptMoveCountOffset);

            for (int i = 0; moves != nullptr && i < count; ++i)
            {
                uint8_t* const move = moves + i * GameAddress::kDemoMoveCmdSize;
                const uintptr_t lists[] = { GameAddress::kDemoMoveCmdTimeListOffset,
                                            GameAddress::kDemoMoveCmdViewListOffset,
                                            GameAddress::kDemoMoveCmdCamListOffset };

                for (const uintptr_t offset : lists)
                {
                    uint8_t* const list = Read<uint8_t*>(move, offset);
                    if (list != nullptr)
                    {
                        Write<int>(list, GameAddress::kDemoListCursorOffset, 0);
                    }
                }
            }
        }

        void RewindPlayer(uint8_t* player)
        {
            Write<float>(player, GameAddress::kDemoPlayerPauseLeftOffset, 0.0f);
            Write<float>(player, GameAddress::kDemoPlayerFactorLeftOffset, 0.0f);
            Write<void*>(player, GameAddress::kDemoPlayerCamCmdOffset, nullptr);
            Write<void*>(player, GameAddress::kDemoPlayerViewCmdOffset, nullptr);
            Write<float>(player, GameAddress::kDemoPlayerCamElapsedOffset, 0.0f);
            Write<float>(player, GameAddress::kDemoPlayerViewElapsedOffset, 0.0f);
        }

        void StopDemo(uint8_t* player)
        {
            if (player == nullptr)
            {
                return;
            }

            Write<void*>(player, GameAddress::kDemoPlayerCurrentOffset, nullptr);
            RewindPlayer(player);

            uint8_t* const free = Resolve(GameAddress::kDemoCameraFree);
            uint8_t* const viewFree = Resolve(GameAddress::kDemoViewFree);
            if (free != nullptr)
            {
                *free = 1;
            }
            if (viewFree != nullptr)
            {
                *viewFree = 1;
            }

            void** const scale = reinterpret_cast<void**>(Resolve(GameAddress::kTimeScaleSlot));
            if (scale != nullptr && *scale != nullptr)
            {
                reinterpret_cast<TimeScaleRoutine>(*scale)(1.0f);
            }

            uint8_t* const mode = Resolve(GameAddress::kDemoMode);
            if (mode != nullptr)
            {
                *mode = 0;
            }
        }

        // The game's parser throws rather than returning on a file it cannot read.
        bool ParseScriptSafely(uint8_t* script, const char* path)
        {
            if (g_parseScript == nullptr)
            {
                return false;
            }

            bool parsed = false;
            __try
            {
                parsed = g_parseScript(script, nullptr, path) != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                parsed = false;
            }

            return parsed;
        }

        bool AppendScript(uint8_t* player, const uint8_t* script)
        {
            if (player == nullptr || g_alloc == nullptr || g_realloc == nullptr)
            {
                return false;
            }

            const int count = ScriptCount(player);
            uint8_t* scripts = Read<uint8_t*>(player, GameAddress::kDemoPlayerScriptsOffset);
            const uint32_t wanted = static_cast<uint32_t>((count + 1) * GameAddress::kDemoScriptSize);

            uint8_t* grown = scripts == nullptr
                ? static_cast<uint8_t*>(g_alloc(wanted, ".\\demPlayer.cpp", 0))
                : static_cast<uint8_t*>(g_realloc(scripts, wanted, ".\\demPlayer.cpp", 0));

            if (grown == nullptr)
            {
                return false;
            }

            memcpy(grown + count * GameAddress::kDemoScriptSize, script, GameAddress::kDemoScriptSize);

            Write<uint8_t*>(player, GameAddress::kDemoPlayerScriptsOffset, grown);
            Write<int>(player, GameAddress::kDemoPlayerCountOffset, count + 1);

            if (RunningScript(player) != nullptr)
            {
                const int index = Read<int>(player, GameAddress::kDemoPlayerIndexOffset);
                Write<uint8_t*>(player, GameAddress::kDemoPlayerCurrentOffset, grown + index * GameAddress::kDemoScriptSize);
            }

            return true;
        }

        void DropScripts(uint8_t* player)
        {
            if (player == nullptr)
            {
                return;
            }

            const int count = ScriptCount(player);
            for (int i = 0; i < count; ++i)
            {
                uint8_t* const script = ScriptAt(player, i);
                if (script != nullptr && g_freeMoveCmds != nullptr)
                {
                    g_freeMoveCmds(script, nullptr);
                }
            }

            uint8_t* const scripts = Read<uint8_t*>(player, GameAddress::kDemoPlayerScriptsOffset);
            if (scripts != nullptr && g_free != nullptr)
            {
                g_free(scripts);
            }

            Write<void*>(player, GameAddress::kDemoPlayerScriptsOffset, nullptr);
            Write<int>(player, GameAddress::kDemoPlayerCountOffset, 0);
            Write<int>(player, GameAddress::kDemoPlayerIndexOffset, 0);
            Write<void*>(player, GameAddress::kDemoPlayerCurrentOffset, nullptr);
        }

        bool DropScript(uint8_t* player, int index)
        {
            uint8_t* const script = ScriptAt(player, index);
            if (script == nullptr)
            {
                return false;
            }

            if (RunningScript(player) == script)
            {
                StopDemo(player);
            }

            if (g_freeMoveCmds != nullptr)
            {
                g_freeMoveCmds(script, nullptr);
            }

            const int count = ScriptCount(player);
            uint8_t* const scripts = Read<uint8_t*>(player, GameAddress::kDemoPlayerScriptsOffset);

            memmove(scripts + index * GameAddress::kDemoScriptSize,
                    scripts + (index + 1) * GameAddress::kDemoScriptSize,
                    (count - index - 1) * GameAddress::kDemoScriptSize);

            Write<int>(player, GameAddress::kDemoPlayerCountOffset, count - 1);

            const int running = Read<int>(player, GameAddress::kDemoPlayerIndexOffset);
            if (RunningScript(player) != nullptr && running > index)
            {
                Write<int>(player, GameAddress::kDemoPlayerIndexOffset, running - 1);
                Write<uint8_t*>(player, GameAddress::kDemoPlayerCurrentOffset, scripts + (running - 1) * GameAddress::kDemoScriptSize);
            }

            return true;
        }

        void ApplyModScripts(uint8_t* player)
        {
            if (!g_suppressors.empty())
            {
                DropScripts(player);
            }

            for (const Added& added : g_added)
            {
                uint8_t script[GameAddress::kDemoScriptSize] = {};
                if (ParseScriptSafely(script, added.path.c_str()))
                {
                    AppendScript(player, script);
                }
                else
                {
                    FORGE_LOG_WARN("[%s] the demo script '%s' could not be read", added.owner->id.c_str(), added.path.c_str());
                }
            }
        }

        int AppendListItem(uint8_t* player, uint8_t* move, uintptr_t listOffset, size_t stride, const void* filled)
        {
            uint8_t* list = Read<uint8_t*>(move, listOffset);
            if (list == nullptr)
            {
                list = static_cast<uint8_t*>(
                    g_alloc(GameAddress::kDemoListSize, ".\\demPlayer.cpp", 0));
                if (list == nullptr)
                {
                    return -1;
                }
                memset(list, 0, GameAddress::kDemoListSize);
                Write<uint8_t*>(move, listOffset, list);
            }

            const int count = Read<int>(list, GameAddress::kDemoListCountOffset);
            uint8_t* const items = Read<uint8_t*>(list, GameAddress::kDemoListItemsOffset);
            const uint32_t wanted = static_cast<uint32_t>((count + 1) * stride);
            const size_t held = static_cast<size_t>(count) * stride;

            uint8_t* const cam = Read<uint8_t*>(player, GameAddress::kDemoPlayerCamCmdOffset);
            uint8_t* const view = Read<uint8_t*>(player, GameAddress::kDemoPlayerViewCmdOffset);
            const bool camHere = items != nullptr && cam >= items && cam < items + held;
            const bool viewHere = items != nullptr && view >= items && view < items + held;
            const size_t camAt = camHere ? static_cast<size_t>(cam - items) : 0;
            const size_t viewAt = viewHere ? static_cast<size_t>(view - items) : 0;

            uint8_t* const grown = items == nullptr
                ? static_cast<uint8_t*>(g_alloc(wanted, ".\\demPlayer.cpp", 0))
                : static_cast<uint8_t*>(g_realloc(items, wanted, ".\\demPlayer.cpp", 0));

            if (grown == nullptr)
            {
                return -1;
            }

            memcpy(grown + held, filled, stride);
            Write<uint8_t*>(list, GameAddress::kDemoListItemsOffset, grown);
            Write<int>(list, GameAddress::kDemoListCountOffset, count + 1);

            if (camHere)
            {
                Write<uint8_t*>(player, GameAddress::kDemoPlayerCamCmdOffset, grown + camAt);
            }
            if (viewHere)
            {
                Write<uint8_t*>(player, GameAddress::kDemoPlayerViewCmdOffset, grown + viewAt);
            }

            return count;
        }

        int AppendMove(uint8_t* script, const AmodDemoMove* value)
        {
            const int count = Read<int>(script, GameAddress::kDemoScriptMoveCountOffset);
            uint8_t* const moves = Read<uint8_t*>(script, GameAddress::kDemoScriptMoveCmdsOffset);
            const uint32_t wanted =
                static_cast<uint32_t>((count + 1) * GameAddress::kDemoMoveCmdSize);

            uint8_t* const grown = moves == nullptr
                ? static_cast<uint8_t*>(g_alloc(wanted, ".\\demPlayer.cpp", 0))
                : static_cast<uint8_t*>(g_realloc(moves, wanted, ".\\demPlayer.cpp", 0));

            if (grown == nullptr)
            {
                return -1;
            }

            uint8_t* const record = grown + count * GameAddress::kDemoMoveCmdSize;
            memset(record, 0, GameAddress::kDemoMoveCmdSize);
            Write<float>(record, GameAddress::kDemoMoveCmdPauseOffset, value->pause);
            Write<int>(record, GameAddress::kDemoMoveCmdHorzOffset, value->horizontal);
            Write<int>(record, GameAddress::kDemoMoveCmdVertOffset, value->vertical);
            Write<int>(record, GameAddress::kDemoMoveCmdJumpOffset, value->jump);
            Write<float>(record, GameAddress::kDemoMoveCmdEndCoordOffset, value->endCoord);

            Write<uint8_t*>(script, GameAddress::kDemoScriptMoveCmdsOffset, grown);
            Write<int>(script, GameAddress::kDemoScriptMoveCountOffset, count + 1);
            return count;
        }

        void FillCamera(uint8_t* record, const AmodDemoCamera* value)
        {
            memset(record, 0, GameAddress::kDemoCamCmdSize);
            memcpy(record + GameAddress::kDemoCamCmdPosOffset, value->position, sizeof(value->position));
            memcpy(record + GameAddress::kDemoCamCmdSpeedOffset, value->speed, sizeof(value->speed));
            Write<float>(record, GameAddress::kDemoCamCmdTimeOffset, value->time);
            Write<int>(record, GameAddress::kDemoCamCmdAutoOffset, value->giveBack != 0 ? 1 : 0);
        }

        void* __cdecl ActiveDetour()
        {
            void* const player = g_active != nullptr ? g_active() : nullptr;

            const bool playing = player != nullptr;
            if (playing != g_wasPlaying)
            {
                if (playing)
                {
                    g_lastScript = RunningIndex(static_cast<uint8_t*>(player));
                    g_lastMove = -1;
                    g_broken = false;
                    Notify(AMOD_DEMO_STARTED, g_lastScript);
                }
                else
                {
                    Notify(g_broken ? AMOD_DEMO_BROKEN : AMOD_DEMO_ENDED, g_lastScript);
                    g_lastScript = -1;
                    g_lastMove = -1;
                    g_broken = false;
                }

                g_wasPlaying = playing;
            }

            return player;
        }

        void __fastcall GetMoveDetour(void* player, void* unused, const float* position, int* out, float seconds)
        {
            g_originalGetMove(player, unused, position, out, seconds);

            uint8_t* const script = RunningScript(static_cast<uint8_t*>(player));
            if (script != nullptr && !g_moveSubs.empty())
            {
                const int move = Read<int>(script, GameAddress::kDemoScriptMoveIndexOffset);
                if (move != g_lastMove)
                {
                    g_lastMove = move;
                    NotifyMove(RunningIndex(static_cast<uint8_t*>(player)), move);
                }
            }

            if (g_inputSubs.empty() || out == nullptr)
            {
                return;
            }

            int horizontal = out[0];
            int vertical = out[1];
            int jump = out[2];

            const std::vector<InputSub> snapshot = g_inputSubs;
            for (const InputSub& subscriber : snapshot)
            {
                if (subscriber.callback != nullptr && subscriber.callback(position, &horizontal, &vertical, &jump, seconds, subscriber.user) != 0)
                {
                    out[0] = horizontal;
                    out[1] = vertical;
                    out[2] = jump;
                    return;
                }
            }
        }

        void __fastcall BreakDetour(void* player, void* unused)
        {
            const uint8_t* const before = RunningScript(static_cast<uint8_t*>(player));
            g_originalBreak(player, unused);

            if (before != nullptr && RunningScript(static_cast<uint8_t*>(player)) == nullptr)
            {
                g_broken = true;
            }
        }

        char __fastcall LoadListDetour(void* player, void* unused, void* parser)
        {
            const char result = g_originalLoadList(player, unused, parser);

            g_undo.clear();

            // The new level's list has none of what Create made, and no file to make them from.
            for (auto it = g_added.begin(); it != g_added.end();)
            {
                it = it->path.empty() ? g_added.erase(it) : ++it;
            }

            if (g_suppressors.empty() && g_added.empty())
            {
                return result;
            }

            ApplyModScripts(static_cast<uint8_t*>(player));

            const int count = ScriptCount(static_cast<uint8_t*>(player));
            if (count != 0)
            {
                Write<uint8_t*>(static_cast<uint8_t*>(player), GameAddress::kDemoPlayerCurrentOffset, ScriptAt(static_cast<uint8_t*>(player), 0));
            }

            return count != 0 ? 1 : 0;
        }

        int __cdecl ApiIsPlaying(AmodMod mod)
        {
            return Caller(mod) != nullptr && g_active != nullptr && g_active() != nullptr ? 1 : 0;
        }

        int __cdecl ApiCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? ScriptCount(Player()) : 0;
        }

        int __cdecl ApiCurrent(AmodMod mod)
        {
            return Caller(mod) != nullptr ? RunningIndex(Player()) : -1;
        }

        float __cdecl ApiElapsed(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0.0f;
            }

            const uint8_t* const script = RunningScript(Player());
            return script != nullptr ? Read<float>(script, GameAddress::kDemoScriptElapsedOffset)
                                     : 0.0f;
        }

        float __cdecl ApiGetBreakTime(AmodMod mod, int script)
        {
            if (Caller(mod) == nullptr)
            {
                return 0.0f;
            }

            const uint8_t* const record = ScriptAt(Player(), script);
            return record != nullptr
                ? Read<float>(record, GameAddress::kDemoScriptBreakTimeOffset)
                : 0.0f;
        }

        int __cdecl ApiSetBreakTime(AmodMod mod, int script, float seconds)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return 0;
            }

            uint8_t* const record = ScriptAt(Player(), script);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no demo script %d", script);
            }

            Snapshot(context, script, -1, -1, 0, GameAddress::kDemoScriptBreakTimeOffset, sizeof(float));
            Write<float>(record, GameAddress::kDemoScriptBreakTimeOffset, seconds);
            return 1;
        }

        int __cdecl ApiMoveCount(AmodMod mod, int script)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            const uint8_t* const record = ScriptAt(Player(), script);
            return record != nullptr
                ? Read<int>(record, GameAddress::kDemoScriptMoveCountOffset)
                : 0;
        }

        int __cdecl ApiMoveIndex(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }

            const uint8_t* const script = RunningScript(Player());
            return script != nullptr ? Read<int>(script, GameAddress::kDemoScriptMoveIndexOffset) : -1;
        }

        int __cdecl ApiGetMove(AmodMod mod, int script, int move, AmodDemoMove* out)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetMove was given nowhere to put it");
            }

            uint8_t* const record = MoveAt(ScriptAt(Player(), script), move);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "demo script %d has no move %d", script, move);
            }

            AmodDemoMove filled = {};
            filled.structSize = sizeof(filled);
            filled.pause = Read<float>(record, GameAddress::kDemoMoveCmdPauseOffset);
            filled.horizontal = Read<int>(record, GameAddress::kDemoMoveCmdHorzOffset);
            filled.vertical = Read<int>(record, GameAddress::kDemoMoveCmdVertOffset);
            filled.jump = Read<int>(record, GameAddress::kDemoMoveCmdJumpOffset);
            filled.endCoord = Read<float>(record, GameAddress::kDemoMoveCmdEndCoordOffset);
            filled.timeCount = ListCount(record, GameAddress::kDemoMoveCmdTimeListOffset);
            filled.viewCount = ListCount(record, GameAddress::kDemoMoveCmdViewListOffset);
            filled.camCount = ListCount(record, GameAddress::kDemoMoveCmdCamListOffset);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetMove(AmodMod mod, int script, int move, const AmodDemoMove* value)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return 0;
            }
            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetMove was given nothing to write");
            }

            uint8_t* const record = MoveAt(ScriptAt(Player(), script), move);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "demo script %d has no move %d", script, move);
            }

            // Only the five the parser fills; the three lists are the game's own memory.
            Snapshot(context, script, move, -1, 0, 0, GameAddress::kDemoMoveCmdTimeListOffset);
            Write<float>(record, GameAddress::kDemoMoveCmdPauseOffset, value->pause);
            Write<int>(record, GameAddress::kDemoMoveCmdHorzOffset, value->horizontal);
            Write<int>(record, GameAddress::kDemoMoveCmdVertOffset, value->vertical);
            Write<int>(record, GameAddress::kDemoMoveCmdJumpOffset, value->jump);
            Write<float>(record, GameAddress::kDemoMoveCmdEndCoordOffset, value->endCoord);
            return 1;
        }

        int ReadCamera(AmodMod mod, int script, int move, int index, AmodDemoCamera* out, uintptr_t listOffset, const char* what)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%s was given nowhere to put it", what);
            }

            uint8_t* const record = ListItem(MoveAt(ScriptAt(Player(), script), move), listOffset,
                                             index, GameAddress::kDemoCamCmdSize);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "%s: move %d of demo script %d has no %d", what, move, script, index);
            }

            AmodDemoCamera filled = {};
            filled.structSize = sizeof(filled);
            memcpy(filled.position, record + GameAddress::kDemoCamCmdPosOffset, sizeof(filled.position));
            memcpy(filled.speed, record + GameAddress::kDemoCamCmdSpeedOffset, sizeof(filled.speed));
            filled.time = Read<float>(record, GameAddress::kDemoCamCmdTimeOffset);
            filled.giveBack = Read<int>(record, GameAddress::kDemoCamCmdAutoOffset) != 0 ? 1 : 0;

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int WriteCamera(AmodMod mod, int script, int move, int index, const AmodDemoCamera* value, uintptr_t listOffset, const char* what)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return 0;
            }
            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%s was given nothing to write", what);
            }

            uint8_t* const record = ListItem(MoveAt(ScriptAt(Player(), script), move), listOffset, index, GameAddress::kDemoCamCmdSize);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "%s: move %d of demo script %d has no %d", what, move, script, index);
            }

            Snapshot(context, script, move, index, listOffset, 0, GameAddress::kDemoCamCmdSize);
            memcpy(record + GameAddress::kDemoCamCmdPosOffset, value->position, sizeof(value->position));
            memcpy(record + GameAddress::kDemoCamCmdSpeedOffset, value->speed, sizeof(value->speed));
            Write<float>(record, GameAddress::kDemoCamCmdTimeOffset, value->time);
            Write<int>(record, GameAddress::kDemoCamCmdAutoOffset, value->giveBack != 0 ? 1 : 0);
            return 1;
        }

        int __cdecl ApiGetCamera(AmodMod mod, int script, int move, int index, AmodDemoCamera* out)
        {
            return ReadCamera(mod, script, move, index, out, GameAddress::kDemoMoveCmdCamListOffset, "GetCamera");
        }

        int __cdecl ApiSetCamera(AmodMod mod, int script, int move, int index,
                                 const AmodDemoCamera* value)
        {
            return WriteCamera(mod, script, move, index, value, GameAddress::kDemoMoveCmdCamListOffset, "SetCamera");
        }

        int __cdecl ApiGetView(AmodMod mod, int script, int move, int index, AmodDemoCamera* out)
        {
            return ReadCamera(mod, script, move, index, out, GameAddress::kDemoMoveCmdViewListOffset, "GetView");
        }

        int __cdecl ApiSetView(AmodMod mod, int script, int move, int index, const AmodDemoCamera* value)
        {
            return WriteCamera(mod, script, move, index, value, GameAddress::kDemoMoveCmdViewListOffset, "SetView");
        }

        int __cdecl ApiGetTime(AmodMod mod, int script, int move, int index, AmodDemoTime* out)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetTime was given nowhere to put it");
            }

            uint8_t* const record = ListItem(MoveAt(ScriptAt(Player(), script), move),
                                             GameAddress::kDemoMoveCmdTimeListOffset, index,
                                             GameAddress::kDemoTimeCmdSize);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "move %d of demo script %d has no time %d", move, script, index);
            }

            AmodDemoTime filled = {};
            filled.structSize = sizeof(filled);
            filled.pause = Read<float>(record, GameAddress::kDemoTimeCmdPauseOffset);
            filled.factor = Read<float>(record, GameAddress::kDemoTimeCmdFactorOffset);
            filled.time = Read<float>(record, GameAddress::kDemoTimeCmdTimeOffset);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetTime(AmodMod mod, int script, int move, int index, const AmodDemoTime* value)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return 0;
            }
            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetTime was given nothing to write");
            }

            uint8_t* const record = ListItem(MoveAt(ScriptAt(Player(), script), move),
                                             GameAddress::kDemoMoveCmdTimeListOffset, index,
                                             GameAddress::kDemoTimeCmdSize);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "move %d of demo script %d has no time %d", move, script, index);
            }

            Snapshot(context, script, move, index, GameAddress::kDemoMoveCmdTimeListOffset, 0, GameAddress::kDemoTimeCmdSize);
            Write<float>(record, GameAddress::kDemoTimeCmdPauseOffset, value->pause);
            Write<float>(record, GameAddress::kDemoTimeCmdFactorOffset, value->factor);
            Write<float>(record, GameAddress::kDemoTimeCmdTimeOffset, value->time);
            return 1;
        }

        int __cdecl ApiGetCameraStart(AmodMod mod, int script, float position[3], float view[3])
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            const uint8_t* const record = ScriptAt(Player(), script);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no demo script %d", script);
            }
            if (Read<uint8_t>(record, GameAddress::kDemoScriptHasCameraOffset) == 0)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "demo script %d has no $camera_start", script);
            }

            if (position != nullptr)
            {
                memcpy(position, record + GameAddress::kDemoScriptCameraPosOffset,
                       3 * sizeof(float));
            }
            if (view != nullptr)
            {
                memcpy(view, record + GameAddress::kDemoScriptCameraViewOffset, 3 * sizeof(float));
            }
            return 1;
        }

        int __cdecl ApiSetCameraStart(AmodMod mod, int script, const float position[3], const float view[3])
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return 0;
            }

            uint8_t* const record = ScriptAt(Player(), script);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no demo script %d", script);
            }

            Snapshot(context, script, -1, -1, 0, GameAddress::kDemoScriptCameraPendingOffset, 0x1C);

            if (position != nullptr)
            {
                memcpy(record + GameAddress::kDemoScriptCameraPosOffset, position, 3 * sizeof(float));
            }
            if (view != nullptr)
            {
                memcpy(record + GameAddress::kDemoScriptCameraViewOffset, view, 3 * sizeof(float));
            }

            Write<uint8_t>(record, GameAddress::kDemoScriptHasCameraOffset, 1);
            return 1;
        }

        int __cdecl ApiGetPlace(AmodMod mod, int script, AmodDemoPlace* out)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetPlace was given nowhere to put it");
            }

            const uint8_t* const record = ScriptAt(Player(), script);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no demo script %d", script);
            }
            if (Read<uint8_t>(record, GameAddress::kDemoScriptHasPlaceOffset) == 0)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "demo script %d has no $player_place", script);
            }

            AmodDemoPlace filled = {};
            filled.structSize = sizeof(filled);
            filled.playerX = Read<int>(record, GameAddress::kDemoScriptPlayerPosOffset);
            filled.playerY = Read<int>(record, GameAddress::kDemoScriptPlayerPosOffset + 4);
            filled.roomX = Read<int>(record, GameAddress::kDemoScriptRoomPosOffset);
            filled.roomY = Read<int>(record, GameAddress::kDemoScriptRoomPosOffset + 4);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiPlay(AmodMod mod, int script)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            uint8_t* const player = Player();
            uint8_t* const record = ScriptAt(player, script);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no demo script %d", script);
            }

            RewindScript(record);
            RewindPlayer(player);
            Write<int>(player, GameAddress::kDemoPlayerIndexOffset, script);
            Write<uint8_t*>(player, GameAddress::kDemoPlayerCurrentOffset, record);

            uint8_t* const mode = Resolve(GameAddress::kDemoMode);
            if (mode != nullptr)
            {
                *mode = 1;
            }
            return 1;
        }

        int __cdecl ApiStop(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            uint8_t* const player = Player();
            if (player == nullptr || RunningScript(player) == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "no demo is playing");
            }

            g_broken = true;
            StopDemo(player);
            return 1;
        }

        int __cdecl ApiNext(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            uint8_t* const player = Player();
            if (player == nullptr || g_nextScript == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "this level has no demos");
            }

            g_nextScript(player, nullptr);

            uint8_t* const record = RunningScript(player);
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "that was the level's last demo");
            }

            RewindScript(record);
            RewindPlayer(player);
            return 1;
        }

        int __cdecl ApiAdd(AmodMod mod, const char* path)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return -1;
            }
            if (path == nullptr || *path == '\0')
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Add was given no path");
                return -1;
            }

            uint8_t* const player = Player();
            if (player == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "no level is loaded");
                return -1;
            }

            uint8_t script[GameAddress::kDemoScriptSize] = {};
            if (!ParseScriptSafely(script, path))
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "the demo script '%s' could not be read", path);
                return -1;
            }

            if (!AppendScript(player, script))
            {
                Fail(mod, AMOD_FAIL_REFUSED, "the demo list could not be grown");
                return -1;
            }

            g_added.push_back(Added{ context, path });
            return ScriptCount(player) - 1;
        }

        int __cdecl ApiRemove(AmodMod mod, int script)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return 0;
            }

            uint8_t* const player = Player();
            const int count = ScriptCount(player);
            const int first = count - static_cast<int>(g_added.size());

            if (script < first || script >= count)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "demo script %d is the level's own; SuppressLevelDemos drops those", script);
            }

            const size_t which = static_cast<size_t>(script - first);
            if (g_added[which].owner != context)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "demo script %d belongs to '%s'", script, g_added[which].owner->id.c_str());
            }

            if (!DropScript(player, script))
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no demo script %d", script);
            }

            g_added.erase(g_added.begin() + which);
            return 1;
        }

        int __cdecl ApiSuppressLevelDemos(AmodMod mod, int suppress)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return 0;
            }

            for (auto it = g_suppressors.begin(); it != g_suppressors.end(); ++it)
            {
                if (*it == context)
                {
                    if (suppress == 0)
                    {
                        g_suppressors.erase(it);
                    }
                    return 1;
                }
            }

            if (suppress != 0)
            {
                g_suppressors.push_back(context);
            }
            return 1;
        }

        AmodToken __cdecl ApiOnDemo(AmodMod mod, AmodDemoCallback callback, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || callback == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_demoSubs.push_back(DemoSub{ context, callback, user, token });
            return token;
        }

        AmodToken __cdecl ApiOnMove(AmodMod mod, AmodDemoMoveCallback callback, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || callback == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_moveSubs.push_back(MoveSub{ context, callback, user, token });
            return token;
        }

        AmodToken __cdecl ApiOnInput(AmodMod mod, AmodDemoInputCallback callback, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || callback == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_inputSubs.push_back(InputSub{ context, callback, user, token });
            return token;
        }

        void __cdecl ApiCancel(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || token == AMOD_INVALID_TOKEN)
            {
                return;
            }

            for (auto it = g_demoSubs.begin(); it != g_demoSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_demoSubs.erase(it);
                    return;
                }
            }
            for (auto it = g_moveSubs.begin(); it != g_moveSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_moveSubs.erase(it);
                    return;
                }
            }
            for (auto it = g_inputSubs.begin(); it != g_inputSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_inputSubs.erase(it);
                    return;
                }
            }
        }

        int __cdecl ApiCreate(AmodMod mod)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr)
            {
                return -1;
            }

            uint8_t* const player = Player();
            if (player == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "no level is loaded");
                return -1;
            }

            uint8_t script[GameAddress::kDemoScriptSize] = {};
            script[GameAddress::kDemoScriptEnteringOffset] = 1;

            if (!AppendScript(player, script))
            {
                Fail(mod, AMOD_FAIL_REFUSED, "the demo list could not be grown");
                return -1;
            }

            g_added.push_back(Added{ context, std::string() });
            return ScriptCount(player) - 1;
        }

        int __cdecl ApiAddMove(AmodMod mod, int script, const AmodDemoMove* value)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }
            if (value == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddMove was given nothing to write");
                return -1;
            }

            uint8_t* const record = ScriptAt(Player(), script);
            if (record == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "there is no demo script %d", script);
                return -1;
            }

            const int added = AppendMove(record, value);
            if (added < 0)
            {
                Fail(mod, AMOD_FAIL_REFUSED, "demo script %d could not be grown", script);
            }

            return added;
        }

        int AddCameraLike(AmodMod mod, int script, int move, const AmodDemoCamera* value, uintptr_t listOffset, const char* what)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }
            if (value == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%s was given nothing to write", what);
                return -1;
            }
            if (value->time <= 0.0f)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%s needs a time above zero, as the parser does",
                     what);
                return -1;
            }

            uint8_t* const player = Player();
            uint8_t* const record = MoveAt(ScriptAt(player, script), move);
            if (record == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "demo script %d has no move %d", script, move);
                return -1;
            }

            uint8_t filled[GameAddress::kDemoCamCmdSize] = {};
            FillCamera(filled, value);

            const int added = AppendListItem(player, record, listOffset, GameAddress::kDemoCamCmdSize, filled);
            if (added < 0)
            {
                Fail(mod, AMOD_FAIL_REFUSED, "%s: the list could not be grown", what);
            }

            return added;
        }

        int __cdecl ApiAddCamera(AmodMod mod, int script, int move, const AmodDemoCamera* value)
        {
            return AddCameraLike(mod, script, move, value, GameAddress::kDemoMoveCmdCamListOffset, "AddCamera");
        }

        int __cdecl ApiAddView(AmodMod mod, int script, int move, const AmodDemoCamera* value)
        {
            return AddCameraLike(mod, script, move, value, GameAddress::kDemoMoveCmdViewListOffset, "AddView");
        }

        int __cdecl ApiAddTime(AmodMod mod, int script, int move, const AmodDemoTime* value)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }
            if (value == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddTime was given nothing to write");
                return -1;
            }

            uint8_t* const player = Player();
            uint8_t* const record = MoveAt(ScriptAt(player, script), move);
            if (record == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "demo script %d has no move %d", script, move);
                return -1;
            }

            uint8_t filled[GameAddress::kDemoTimeCmdSize] = {};
            Write<float>(filled, GameAddress::kDemoTimeCmdPauseOffset, value->pause);
            Write<float>(filled, GameAddress::kDemoTimeCmdFactorOffset, value->factor);
            Write<float>(filled, GameAddress::kDemoTimeCmdTimeOffset, value->time);

            const int added = AppendListItem(player, record, GameAddress::kDemoMoveCmdTimeListOffset, GameAddress::kDemoTimeCmdSize, filled);
            if (added < 0)
            {
                Fail(mod, AMOD_FAIL_REFUSED, "AddTime: the list could not be grown");
            }

            return added;
        }
    }

    bool DemoApi::Install()
    {
        g_alloc = reinterpret_cast<AllocRoutine>(
            GameSymbols::Address(GameSymbol::MemAllocTracked));
        g_realloc = reinterpret_cast<ReallocRoutine>(
            GameSymbols::Address(GameSymbol::MemReallocTracked));
        g_free = reinterpret_cast<FreeRoutine>(
            GameSymbols::Address(GameSymbol::MemFreeTracked));

        g_nextScript = reinterpret_cast<NextScriptRoutine>(
            GameModule::Resolve(GameAddress::kDemoNextScript));
        g_parseScript = reinterpret_cast<ParseScriptRoutine>(
            GameModule::Resolve(GameAddress::kDemoParse));
        g_freeMoveCmds = reinterpret_cast<FreeMoveCmdsRoutine>(
            GameModule::Resolve(GameAddress::kDemoFreeMoveCmds));

        if (g_alloc == nullptr || g_realloc == nullptr || g_free == nullptr ||
            g_nextScript == nullptr || g_parseScript == nullptr || g_freeMoveCmds == nullptr)
        {
            FORGE_LOG_WARN("The demo player was not found, demos are the game's alone");
            return false;
        }

        std::string error;
        std::string mismatch;

        void* const activeTarget = GameModule::Resolve(GameAddress::kDemoActive);

        if (activeTarget == nullptr ||
            !GameModule::VerifyBytes(activeTarget, kDemoActivePrologue, sizeof(kDemoActivePrologue), mismatch) ||
            !g_activeHook.Install(activeTarget, reinterpret_cast<void*>(&ActiveDetour), kDemoActivePrologue, sizeof(kDemoActivePrologue), error))
        {
            FORGE_LOG_ERROR("Demo_Active was not hooked: %s", error.empty() ? mismatch.c_str()
                                                                           : error.c_str());
            return false;
        }
        g_active = reinterpret_cast<ActiveRoutine>(g_activeHook.Trampoline());

        void* const moveTarget = GameModule::Resolve(GameAddress::kDemoGetMove);
        if (moveTarget != nullptr &&
            GameModule::VerifyBytes(moveTarget, kDemoGetMovePrologue, sizeof(kDemoGetMovePrologue), mismatch) &&
            g_getMoveHook.Install(moveTarget, reinterpret_cast<void*>(&GetMoveDetour), kDemoGetMovePrologue, sizeof(kDemoGetMovePrologue), error))
        {
            g_originalGetMove = reinterpret_cast<GetMoveRoutine>(g_getMoveHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Demo_GetMove was not hooked, a mod cannot drive a demo itself");
        }

        void* const breakTarget = GameModule::Resolve(GameAddress::kDemoBreak);
        if (breakTarget != nullptr &&
            GameModule::VerifyBytes(breakTarget, kDemoBreakPrologue, sizeof(kDemoBreakPrologue), mismatch) &&
            g_breakHook.Install(breakTarget, reinterpret_cast<void*>(&BreakDetour), kDemoBreakPrologue, sizeof(kDemoBreakPrologue), error))
        {
            g_originalBreak = reinterpret_cast<BreakRoutine>(g_breakHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Demo_Break was not hooked, a broken demo reads as one that ended");
        }

        void* const listTarget = GameModule::Resolve(GameAddress::kDemoLoadList);
        if (listTarget != nullptr &&
            GameModule::VerifyBytes(listTarget, kDemoLoadListPrologue, sizeof(kDemoLoadListPrologue), mismatch) &&
            g_loadListHook.Install(listTarget, reinterpret_cast<void*>(&LoadListDetour), kDemoLoadListPrologue, sizeof(kDemoLoadListPrologue), error))
        {
            g_originalLoadList = reinterpret_cast<LoadListRoutine>(g_loadListHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Demo_LoadList was not hooked, a mod's own scripts stop at a level");
        }

        g_table.structSize = sizeof(AmodDemoApi);
        g_table.IsPlaying = &ApiIsPlaying;
        g_table.Count = &ApiCount;
        g_table.Current = &ApiCurrent;
        g_table.Elapsed = &ApiElapsed;
        g_table.GetBreakTime = &ApiGetBreakTime;
        g_table.SetBreakTime = &ApiSetBreakTime;
        g_table.MoveCount = &ApiMoveCount;
        g_table.MoveIndex = &ApiMoveIndex;
        g_table.GetMove = &ApiGetMove;
        g_table.SetMove = &ApiSetMove;
        g_table.GetCamera = &ApiGetCamera;
        g_table.SetCamera = &ApiSetCamera;
        g_table.GetView = &ApiGetView;
        g_table.SetView = &ApiSetView;
        g_table.GetTime = &ApiGetTime;
        g_table.SetTime = &ApiSetTime;
        g_table.GetCameraStart = &ApiGetCameraStart;
        g_table.SetCameraStart = &ApiSetCameraStart;
        g_table.GetPlace = &ApiGetPlace;
        g_table.Play = &ApiPlay;
        g_table.Stop = &ApiStop;
        g_table.Next = &ApiNext;
        g_table.Add = &ApiAdd;
        g_table.Remove = &ApiRemove;
        g_table.SuppressLevelDemos = &ApiSuppressLevelDemos;
        g_table.OnDemo = &ApiOnDemo;
        g_table.OnMove = &ApiOnMove;
        g_table.OnInput = &ApiOnInput;
        g_table.Cancel = &ApiCancel;
        g_table.Create = &ApiCreate;
        g_table.AddMove = &ApiAddMove;
        g_table.AddCamera = &ApiAddCamera;
        g_table.AddView = &ApiAddView;
        g_table.AddTime = &ApiAddTime;

        FORGE_LOG_INFO("Demo interface installed");
        return true;
    }

    void DemoApi::Uninstall()
    {
        for (auto it = g_undo.rbegin(); it != g_undo.rend(); ++it)
        {
            Restore(*it);
        }
        g_undo.clear();

        g_activeHook.Uninstall();
        g_getMoveHook.Uninstall();
        g_breakHook.Uninstall();
        g_loadListHook.Uninstall();

        g_active = nullptr;
        g_originalGetMove = nullptr;
        g_originalBreak = nullptr;
        g_originalLoadList = nullptr;
        g_nextScript = nullptr;
        g_parseScript = nullptr;
        g_freeMoveCmds = nullptr;
        g_alloc = nullptr;
        g_realloc = nullptr;
        g_free = nullptr;

        g_demoSubs.clear();
        g_moveSubs.clear();
        g_inputSubs.clear();
        g_added.clear();
        g_suppressors.clear();
        g_table = {};
        g_wasPlaying = false;
        g_lastScript = -1;
        g_lastMove = -1;
        g_broken = false;
    }

    const AmodDemoApi* DemoApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void DemoApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_demoSubs.begin(); it != g_demoSubs.end();)
        {
            it = it->owner == owner ? g_demoSubs.erase(it) : ++it;
        }
        for (auto it = g_moveSubs.begin(); it != g_moveSubs.end();)
        {
            it = it->owner == owner ? g_moveSubs.erase(it) : ++it;
        }
        for (auto it = g_inputSubs.begin(); it != g_inputSubs.end();)
        {
            it = it->owner == owner ? g_inputSubs.erase(it) : ++it;
        }
        for (auto it = g_suppressors.begin(); it != g_suppressors.end();)
        {
            it = *it == owner ? g_suppressors.erase(it) : ++it;
        }

        // The scripts it added, from the back so the ones before keep their places.
        uint8_t* const player = Player();
        const int count = ScriptCount(player);
        const int first = count - static_cast<int>(g_added.size());

        for (int i = static_cast<int>(g_added.size()) - 1; i >= 0; --i)
        {
            if (g_added[static_cast<size_t>(i)].owner == owner)
            {
                DropScript(player, first + i);
                g_added.erase(g_added.begin() + i);
            }
        }

        for (auto it = g_undo.rbegin(); it != g_undo.rend(); ++it)
        {
            if (it->owner == owner)
            {
                Restore(*it);
            }
        }
        for (auto it = g_undo.begin(); it != g_undo.end();)
        {
            it = it->owner == owner ? g_undo.erase(it) : ++it;
        }
    }
}
