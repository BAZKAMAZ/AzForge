#ifndef AZFORGE_SOUND_API_H
#define AZFORGE_SOUND_API_H

#include "ForgeTypes.h"

typedef uint32_t AmodSoundId;
typedef uint32_t AmodVoice;

#define AMOD_INVALID_SOUND                                          0
#define AMOD_INVALID_VOICE                                          0

#ifdef __cplusplus
extern "C" {
#endif

// Room loop sound
typedef struct AmodRoomSound
{
    uint32_t structSize;

    AmodSoundId sound;
    float position[3]; /* where it is heard from */

} AmodRoomSound;

AMOD_LAST_FIELD(AmodRoomSound, position, 0x08);

typedef struct AmodSoundApi
{
    uint32_t structSize;

    /* Load or find by name. */
    AmodSoundId(__cdecl* Load)(AmodMod mod, const char* name);

    /* Non-positional; menus and inventory use this. */
    int(__cdecl* Play)(AmodMod mod, AmodSoundId sound);

    /* Positional; returns voice for Move/Stop. pitch <= 0 hangs (refused). */
    AmodVoice(__cdecl* PlayAt)(AmodMod mod, AmodSoundId sound, const float* position, int loop, float pitch);

    int(__cdecl* Move)(AmodMod mod, AmodVoice voice, const float* position);
    int(__cdecl* Stop)(AmodMod mod, AmodVoice voice);

    /* Room loop sounds; start on enter, stop on leave. */
    int(__cdecl* GetRoomSoundCount)(AmodMod mod, int roomIndex);
    int(__cdecl* GetRoomSound)(AmodMod mod, int roomIndex, int index, AmodRoomSound* out);
    int(__cdecl* SetRoomSound)(AmodMod mod, int roomIndex, int index, const AmodRoomSound* sound);

    /* Add room loop; takes effect next visit. Index or -1. */
    int(__cdecl* AddRoomSound)(AmodMod mod, int roomIndex, const AmodRoomSound* sound);

    /* Streamed music (not a sound); same door as console "music play". fade = crossfade. */
    int(__cdecl* PlayMusic)(AmodMod mod, const char* path, int fade, int loop);
    int(__cdecl* StopMusic)(AmodMod mod);

    /* What is playing now, or "" - read off the stream slot the game filled. */
    const char* (__cdecl* CurrentMusic)(AmodMod mod);

} AmodSoundApi;

AMOD_LAST_FIELD(AmodSoundApi, CurrentMusic, 0x30);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Sound
    {
        inline const AmodSoundApi* g_sound = nullptr;
        inline AmodMod g_soundSelf = nullptr;

        inline void                                                 Attach(const AmodSoundApi* api, AmodMod self)
        {
            g_sound = api;
            g_soundSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_sound != nullptr;
        }

        inline AmodSoundId                                          Load(const char* name)
        {
            return g_sound != nullptr ? g_sound->Load(g_soundSelf, name) : AMOD_INVALID_SOUND;
        }

        inline bool                                                 Play(AmodSoundId sound)
        {
            return g_sound != nullptr && g_sound->Play(g_soundSelf, sound) != 0;
        }

        inline AmodVoice                                            PlayAt(AmodSoundId sound, float x, float y, float z, bool loop, float pitch)
        {
            const float position[3] = { x, y, z };
            return g_sound != nullptr ? g_sound->PlayAt(g_soundSelf, sound, position, loop ? 1 : 0, pitch) : AMOD_INVALID_VOICE;
        }

        inline bool                                                 Move(AmodVoice voice, float x, float y, float z)
        {
            const float position[3] = { x, y, z };
            return g_sound != nullptr && g_sound->Move(g_soundSelf, voice, position) != 0;
        }

        inline bool                                                 Stop(AmodVoice voice)
        {
            return g_sound != nullptr && g_sound->Stop(g_soundSelf, voice) != 0;
        }

        inline int                                                  RoomSoundCount(int roomIndex = -1)
        {
            return g_sound != nullptr ? g_sound->GetRoomSoundCount(g_soundSelf, roomIndex) : 0;
        }

        inline bool                                                 GetRoomSound(int index, AmodRoomSound& out, int roomIndex = -1)
        {
            out = AmodRoomSound{};
            out.structSize = sizeof(out);
            return g_sound != nullptr && g_sound->GetRoomSound(g_soundSelf, roomIndex, index, &out) != 0;
        }

        inline bool                                                 SetRoomSound(int index, const AmodRoomSound& sound, int roomIndex = -1)
        {
            return g_sound != nullptr && g_sound->SetRoomSound(g_soundSelf, roomIndex, index, &sound) != 0;
        }

        inline int                                                  AddRoomSound(const AmodRoomSound& sound, int roomIndex = -1)
        {
            return g_sound != nullptr ? g_sound->AddRoomSound(g_soundSelf, roomIndex, &sound) : -1;
        }

        inline bool                                                 PlayMusic(const char* path, bool fade = true, bool loop = true)
        {
            return g_sound != nullptr && g_sound->structSize >= sizeof(AmodSoundApi) && g_sound->PlayMusic != nullptr &&
                g_sound->PlayMusic(g_soundSelf, path, fade ? 1 : 0, loop ? 1 : 0) != 0;
        }

        inline bool                                                 StopMusic()
        {
            return g_sound != nullptr && g_sound->structSize >= sizeof(AmodSoundApi) && g_sound->StopMusic != nullptr &&
                g_sound->StopMusic(g_soundSelf) != 0;
        }

        inline const char*                                          CurrentMusic()
        {
            return (g_sound != nullptr && g_sound->structSize >= sizeof(AmodSoundApi) && g_sound->CurrentMusic != nullptr)
                ? g_sound->CurrentMusic(g_soundSelf)
                : "";
        }
    }
}

#endif

#endif
