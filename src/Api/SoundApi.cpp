#include <windows.h>

#include "src/Api/SoundApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"

#include <stddef.h>
#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        AmodSoundApi g_table = {};

        typedef AmodSoundId(__cdecl* LoadRoutine)(const char* name);
        typedef void(__cdecl* PlayRoutine)(AmodSoundId sound, int flag);
        typedef AmodVoice(__cdecl* PlayAtRoutine)(AmodSoundId sound, const float* position, int loop, float pitch, float volume);
        typedef void(__cdecl* StopRoutine)(AmodVoice voice);
        typedef void(__cdecl* MoveRoutine)(AmodVoice voice, const float* position);

        // RoomSounds_Append: __stdcall, RET 8 (Ghidra mislabels cdecl).
        typedef uint8_t* (__stdcall* AppendRoutine)(void* array, int* count);

        template <typename T>
        T Routine(uintptr_t address)
        {
            return reinterpret_cast<T>(GameModule::Resolve(address));
        }

        // Slot filled by Game_BuildCallbackTable at startup.
        LoadRoutine LoadThroughSlot()
        {
            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kResolveSoundSlot));
            return at != nullptr ? reinterpret_cast<LoadRoutine>(*at) : nullptr;
        }

        // Voices started by a mod; stopped on unload.
        struct Started
        {
            ModContext* owner;
            AmodVoice voice;
        };

        // Room loop snapshot: restore bytes + count on unload.
        struct RoomSnapshot
        {
            ModContext* owner;
            int roomIndex;
            int count;
            std::vector<uint8_t> bytes;
        };

        std::vector<Started> g_started;
        std::vector<RoomSnapshot> g_rooms;

        // Slot filled after Install; resolve per call, not at Install.
        bool Ready()
        {
            return GameModule::Resolve(GameAddress::kSound) != nullptr &&
                GameModule::Resolve(GameAddress::kResolveSoundSlot) != nullptr &&
                Routine<PlayAtRoutine>(GameAddress::kSoundPlayAt) != nullptr;
        }

        uint8_t* RoomFor(int roomIndex)
        {
            return roomIndex < 0 ? WorldApi::CurrentRoom() : WorldApi::RoomAt(roomIndex);
        }

        int RoomSoundCount(const uint8_t* room)
        {
            const int count = room != nullptr ? Read<int>(room, GameAddress::kRoomLoopSoundCountOffset) : 0;
            return (count > 0 && count < 0x1000) ? count : 0;
        }

        uint8_t* RoomSoundAt(const uint8_t* room, int index)
        {
            if (room == nullptr || index < 0 || index >= RoomSoundCount(room))
            {
                return nullptr;
            }

            uint8_t* const sounds = reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLoopSoundsOffset));
            const size_t wanted = static_cast<size_t>(index + 1) * GameAddress::kRoomLoopSoundStride;

            return (sounds != nullptr && Readable(sounds, wanted)) ? sounds + static_cast<size_t>(index) * GameAddress::kRoomLoopSoundStride : nullptr;
        }

        void KeepRoom(ModContext* owner, int roomIndex, const uint8_t* room)
        {
            for (const RoomSnapshot& kept : g_rooms)
            {
                if (kept.roomIndex == roomIndex && kept.owner == owner)
                {
                    return;
                }
            }

            const int count = RoomSoundCount(room);
            const uint8_t* const sounds = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLoopSoundsOffset));

            RoomSnapshot kept;
            kept.owner = owner;
            kept.roomIndex = roomIndex;
            kept.count = count;

            if (sounds != nullptr && count > 0 && Readable(sounds, static_cast<size_t>(count) * GameAddress::kRoomLoopSoundStride))
            {
                kept.bytes.assign(sounds, sounds + static_cast<size_t>(count) * GameAddress::kRoomLoopSoundStride);
            }
            g_rooms.push_back(kept);
        }

        AmodSoundId __cdecl ApiLoad(AmodMod mod, const char* name)
        {
            const LoadRoutine load = LoadThroughSlot();

            if (Caller(mod) == nullptr || load == nullptr)
            {
                return AMOD_INVALID_SOUND;
            }
            if (name == nullptr || name[0] == '\0')
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Load was given no sound to look for");
                return AMOD_INVALID_SOUND;
            }

            const AmodSoundId sound = load(name);
            if (sound == AMOD_INVALID_SOUND)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "the game has no sound '%s'", name);
            }
            return sound;
        }

        int __cdecl ApiPlay(AmodMod mod, AmodSoundId sound)
        {
            const PlayRoutine play = Routine<PlayRoutine>(GameAddress::kSoundPlay);

            if (Caller(mod) == nullptr || play == nullptr || sound == AMOD_INVALID_SOUND)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no such sound to play");
            }

            play(sound, 0);
            return 1;
        }

        AmodVoice __cdecl ApiPlayAt(AmodMod mod, AmodSoundId sound, const float* position, int loop, float pitch)
        {
            ModContext* const caller = Caller(mod);
            const PlayAtRoutine play = Routine<PlayAtRoutine>(GameAddress::kSoundPlayAt);

            if (caller == nullptr || play == nullptr)
            {
                return AMOD_INVALID_VOICE;
            }
            if (sound == AMOD_INVALID_SOUND || position == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "PlayAt needs a sound and a place to play it");
                return AMOD_INVALID_VOICE;
            }

            // pitch <= 0 never advances; game hangs waiting for it
            if (pitch <= 0.0f)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a pitch of %.3f never plays", pitch);
                return AMOD_INVALID_VOICE;
            }

            const AmodVoice voice = play(sound, position, loop != 0 ? 1 : 0, pitch, 1.0f);
            if (voice == AMOD_INVALID_VOICE)
            {
                Fail(mod, AMOD_FAIL_REFUSED, "the game had no voice left to play it with");
                return AMOD_INVALID_VOICE;
            }

            Started started;
            started.owner = caller;
            started.voice = voice;
            g_started.push_back(started);
            return voice;
        }

        void Forget(AmodVoice voice)
        {
            for (auto it = g_started.begin(); it != g_started.end(); ++it)
            {
                if (it->voice == voice)
                {
                    g_started.erase(it);
                    return;
                }
            }
        }

        int __cdecl ApiMove(AmodMod mod, AmodVoice voice, const float* position)
        {
            const MoveRoutine move = Routine<MoveRoutine>(GameAddress::kSoundMove);

            if (Caller(mod) == nullptr || move == nullptr || voice == AMOD_INVALID_VOICE || position == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Move needs a voice and a place to move it to");
            }

            move(voice, position);
            return 1;
        }

        int __cdecl ApiStop(AmodMod mod, AmodVoice voice)
        {
            const StopRoutine stop = Routine<StopRoutine>(GameAddress::kSoundStop);

            if (Caller(mod) == nullptr || stop == nullptr || voice == AMOD_INVALID_VOICE)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no such voice to stop");
            }

            stop(voice);
            Forget(voice);
            return 1;
        }

        int __cdecl ApiGetRoomSoundCount(AmodMod mod, int roomIndex)
        {
            return Caller(mod) != nullptr ? RoomSoundCount(RoomFor(roomIndex)) : 0;
        }

        int __cdecl ApiGetRoomSound(AmodMod mod, int roomIndex, int index, AmodRoomSound* out)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? RoomSoundAt(RoomFor(roomIndex), index) : nullptr;

            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no sound %d", roomIndex, index);
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetRoomSound was given nowhere to put it");
            }

            AmodRoomSound filled = {};
            filled.structSize = sizeof(filled);
            filled.sound = Read<AmodSoundId>(record, GameAddress::kRoomLoopSoundIdOffset);
            memcpy(filled.position, record + GameAddress::kRoomLoopSoundPositionOffset, sizeof(filled.position));
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        void WriteRoomSound(uint8_t* record, const AmodRoomSound& from)
        {
            memcpy(record + GameAddress::kRoomLoopSoundIdOffset, &from.sound, sizeof(from.sound));
            memcpy(record + GameAddress::kRoomLoopSoundPositionOffset, from.position, sizeof(from.position));
        }

        int __cdecl ApiSetRoomSound(AmodMod mod, int roomIndex, int index, const AmodRoomSound* sound)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const room = caller != nullptr ? RoomFor(roomIndex) : nullptr;
            uint8_t* const record = room != nullptr ? RoomSoundAt(room, index) : nullptr;

            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no sound %d", roomIndex, index);
            }
            if (sound == nullptr || sound->structSize < sizeof(AmodRoomSound))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetRoomSound was given nothing to write");
            }

            KeepRoom(caller, roomIndex, room);
            WriteRoomSound(record, *sound);
            return 1;
        }

        int __cdecl ApiAddRoomSound(AmodMod mod, int roomIndex, const AmodRoomSound* sound)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const room = caller != nullptr ? RoomFor(roomIndex) : nullptr;
            const AppendRoutine append = Routine<AppendRoutine>(GameAddress::kRoomSoundsAppend);

            if (room == nullptr || append == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to put a sound in", roomIndex);
            }
            if (sound == nullptr || sound->structSize < sizeof(AmodRoomSound) || sound->sound == AMOD_INVALID_SOUND)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a room sound is a sound standing somewhere, and none was named");
            }

            KeepRoom(caller, roomIndex, room);

            const int count = RoomSoundCount(room);
            uint8_t* const put = append(room + GameAddress::kRoomLoopSoundsOffset, reinterpret_cast<int*>(room + GameAddress::kRoomLoopSoundCountOffset));

            if (put == nullptr)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "the room would not take another sound");
            }

            WriteRoomSound(put, *sound);
            return count;
        }
    }

    bool SoundApi::Install()
    {
        if (!Ready())
        {
            FORGE_LOG_WARN("The sound manager was not located, so the sound interface is not offered");
            return false;
        }

        g_table.structSize = sizeof(AmodSoundApi);
        g_table.Load = &ApiLoad;
        g_table.Play = &ApiPlay;
        g_table.PlayAt = &ApiPlayAt;
        g_table.Move = &ApiMove;
        g_table.Stop = &ApiStop;
        g_table.GetRoomSoundCount = &ApiGetRoomSoundCount;
        g_table.GetRoomSound = &ApiGetRoomSound;
        g_table.SetRoomSound = &ApiSetRoomSound;
        g_table.AddRoomSound = &ApiAddRoomSound;

        FORGE_LOG_INFO("Sound interface installed");
        return true;
    }

    void SoundApi::Uninstall()
    {
        g_started.clear();
        g_rooms.clear();
        g_table = AmodSoundApi{};
    }

    const AmodSoundApi* SoundApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void SoundApi::ForgetMod(ModContext* owner)
    {
        const StopRoutine stop = Routine<StopRoutine>(GameAddress::kSoundStop);

        for (auto it = g_started.begin(); it != g_started.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            if (stop != nullptr)
            {
                stop(it->voice);
            }
            it = g_started.erase(it);
        }

        // Restore loop bytes + count (Room_Activate reads the count).
        for (auto it = g_rooms.begin(); it != g_rooms.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const room = RoomFor(it->roomIndex);
            uint8_t* const sounds = room != nullptr ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLoopSoundsOffset)) : nullptr;

            if (sounds != nullptr && !it->bytes.empty() && Readable(sounds, it->bytes.size()))
            {
                memcpy(sounds, it->bytes.data(), it->bytes.size());
            }
            if (room != nullptr)
            {
                memcpy(room + GameAddress::kRoomLoopSoundCountOffset, &it->count, sizeof(it->count));
            }
            it = g_rooms.erase(it);
        }
    }
}
