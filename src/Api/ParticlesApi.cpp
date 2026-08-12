#include <windows.h>

#include "src/Api/ParticlesApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Hook/InlineHook.h"

#include <stddef.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        AmodParticlesApi g_table = {};

        typedef void* (__cdecl* AllocKindRoutine)(int kind);
        typedef void* (__cdecl* AllocTrailRoutine)(void);
        typedef void(__cdecl* AttachUpdateRoutine)(void* particle, void* update);
        typedef int(__cdecl* ParticleThinkRoutine)(void* particle);

        // __thiscall via __fastcall (ECX = this).
        // Particle_Step: __stdcall RET 8 (not cdecl -- wrong CC leaks 8 bytes/frame).
        typedef void(__stdcall* ParticleStepRoutine)(void* particle, float delta);

        typedef void(__fastcall* SceneDrawRoutine)(void* scene, void* unused);
        typedef void(__fastcall* SpawnTrailRoutine)(void* source, void* unused, const float* origin, int index);

        typedef void(__cdecl* SpawnTextRoutine)(int font, const float* position, const char* text, const float* scale, float speed, float decay, 
            const float* direction, float gravity);

        constexpr uintptr_t kNone = ~static_cast<uintptr_t>(0);

        void WriteFloat(uint8_t* base, uintptr_t offset, float value);
        void WriteInt(uint8_t* base, uintptr_t offset, int value);

        // Frame lists must outlive particles; copies kept here.
        struct Animation
        {
            ModContext* owner;
            int count;
            int frames[32];
        };

        constexpr size_t kAnimationMax = 32;
        Animation g_animations[kAnimationMax] = {};

        struct Layout
        {
            uintptr_t position;
            uintptr_t direction;
            uintptr_t waypoint;
            uintptr_t speed;
            uintptr_t size;
            uintptr_t life;
            uintptr_t decay;
            uintptr_t colour;
            uintptr_t colourB; // uni: one colour per layer
            uintptr_t textureA;
            uintptr_t textureB;
            uintptr_t shape;
            uintptr_t model;
            uintptr_t modelFrame;
            uintptr_t parent;
            uintptr_t link;
            uintptr_t colourFrom; // shape-2 colour lerp source
            uintptr_t colourTo;
            uintptr_t colourDriver;
            uintptr_t gravity;
            uintptr_t speedGrow;
            uintptr_t sizeGrow;
            uintptr_t angle;
            uintptr_t spin;
            uintptr_t glyphScaleX;
            uintptr_t glyphScaleY;
            uintptr_t animated;
            uintptr_t frameList;
            uintptr_t frameCount;
            uintptr_t frameRate;
            uintptr_t frameTime;
            bool shapeIsByte;
        };

        constexpr Layout kPoolLayout =
        {
            GameAddress::kParticlePositionOffset,
            GameAddress::kParticleDirectionOffset,
            kNone,
            GameAddress::kParticleSpeedOffset,
            GameAddress::kParticleSizeOffset,
            GameAddress::kParticleLifeOffset,
            GameAddress::kParticleDecayOffset,
            GameAddress::kParticleColourOffset,
            kNone,
            GameAddress::kParticleTextureAOffset,
            GameAddress::kParticleTextureBOffset,
            GameAddress::kParticleShapeOffset,
            GameAddress::kParticleModelOffset,
            GameAddress::kParticleModelFrameOffset,
            GameAddress::kParticleParentOffset,
            GameAddress::kParticleLinkOffset,
            GameAddress::kParticleColourFromOffset,
            GameAddress::kParticleColourToOffset,
            GameAddress::kParticleColourDriverOffset,
            GameAddress::kParticleGravityOffset,
            GameAddress::kParticleSpeedGrowOffset,
            GameAddress::kParticleSizeGrowOffset,
            GameAddress::kParticleAngleOffset,
            GameAddress::kParticleSpinOffset,
            GameAddress::kParticleGlyphScaleXOffset,
            GameAddress::kParticleGlyphScaleYOffset,
            GameAddress::kParticleAnimatedOffset,
            GameAddress::kParticleFrameListOffset,
            GameAddress::kParticleFrameCountOffset,
            GameAddress::kParticleFrameRateOffset,
            GameAddress::kParticleFrameTimeOffset,
            false
        };

        constexpr Layout kUniLayout =
        {
            GameAddress::kUniParticlePositionOffset,
            kNone,
            GameAddress::kUniParticleTargetOffset,
            GameAddress::kUniParticleSpeedOffset,
            GameAddress::kUniParticleSizeOffset,
            GameAddress::kUniParticleLifeOffset,
            GameAddress::kUniParticleDecayOffset,
            GameAddress::kUniParticleColourAOffset,
            GameAddress::kUniParticleColourBOffset,
            GameAddress::kUniParticleTextureAOffset,
            GameAddress::kUniParticleTextureBOffset,
            GameAddress::kUniParticleShapeOffset,
            kNone,
            kNone,
            kNone,
            GameAddress::kUniParticleLinkOffset,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            kNone,
            true
        };

        bool IsUni(int kind)
        {
            return kind == AMOD_PARTICLE_KIND_UNI;
        }

        bool KnownKind(int kind)
        {
            return kind >= AMOD_PARTICLE_KIND_POOL_0 && kind <= AMOD_PARTICLE_KIND_UNI;
        }

        const Layout& LayoutOf(int kind)
        {
            return IsUni(kind) ? kUniLayout : kPoolLayout;
        }

        void* LiveHead(int kind)
        {
            const uintptr_t address = IsUni(kind)
                ? GameAddress::kUniParticleLiveHead
                : GameAddress::kParticleLiveHeads[kind];

            void** const head = static_cast<void**>(GameModule::Resolve(address));
            return head != nullptr ? *head : nullptr;
        }

        void* Next(void* particle, int kind)
        {
            const uint8_t* const at = static_cast<const uint8_t*>(particle);
            return Read<void*>(at, LayoutOf(kind).link);
        }

        float FrameDelta()
        {
            const float* const delta = static_cast<const float*>(GameModule::Resolve(GameAddress::kParticleFrameDelta));
            return delta != nullptr ? *delta : 0.0f;
        }

        // Kind by live-list walk; particle itself does not record it.
        int FindKind(void* particle)
        {
            if (particle == nullptr)
            {
                return -1;
            }

            for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind <= AMOD_PARTICLE_KIND_UNI; ++kind)
            {
                for (void* walk = LiveHead(kind); walk != nullptr; walk = Next(walk, kind))
                {
                    if (walk == particle)
                    {
                        return kind;
                    }
                }
            }

            return -1;
        }

        struct Thinking
        {
            ModContext* owner;
            void* particle;
            void* game;
            AmodParticleThink think;
            void* user;
        };

        constexpr size_t kThinkingMax = GameAddress::kParticlePoolCount * GameAddress::kParticlePoolCapacity + GameAddress::kUniParticleCapacity;

        Thinking g_thinking[kThinkingMax] = {};

        // 0 when no thinks are installed.
        int g_thinkingCount = 0;

        int SlotOf(const void* particle)
        {
            const uint8_t* const at = static_cast<const uint8_t*>(particle);
            if (at == nullptr)
            {
                return -1;
            }

            for (size_t pool = 0; pool < GameAddress::kParticlePoolCount; ++pool)
            {
                const uint8_t* const manager = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kParticlePools[pool]));
                if (manager == nullptr)
                {
                    continue;
                }

                // Past vftable; particles start at +0x04.
                const uint8_t* const base = manager + GameAddress::kParticleArenaOffset;
                if (at < base)
                {
                    continue;
                }

                const size_t offset = static_cast<size_t>(at - base);
                if (offset >= GameAddress::kParticlePoolCapacity * GameAddress::kParticleSize)
                {
                    continue;
                }

                if (offset % GameAddress::kParticleSize != 0)
                {
                    return -1;
                }

                return static_cast<int>(pool * GameAddress::kParticlePoolCapacity + offset / GameAddress::kParticleSize);
            }

            const uint8_t* const uni = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kUniParticles));
            if (uni != nullptr && at >= uni)
            {
                const size_t offset = static_cast<size_t>(at - uni);
                if (offset < GameAddress::kUniParticleCapacity * GameAddress::kUniParticleSize && offset % GameAddress::kUniParticleSize == 0)
                {
                    return static_cast<int>(GameAddress::kParticlePoolCount * GameAddress::kParticlePoolCapacity + offset / GameAddress::kUniParticleSize);
                }
            }

            return -1;
        }

        Thinking* FindThinking(void* particle)
        {
            if (g_thinkingCount == 0)
            {
                return nullptr;
            }

            const int slot = SlotOf(particle);
            if (slot < 0 || g_thinking[slot].think == nullptr)
            {
                return nullptr;
            }

            return &g_thinking[slot];
        }

        void ReleaseThinking(Thinking* slot)
        {
            if (slot != nullptr && slot->think != nullptr)
            {
                *slot = Thinking{};
                --g_thinkingCount;
            }
        }

        // Uni think thunk; game passes the particle.
        int __cdecl ThinkThunk(void* particle)
        {
            Thinking* const slot = FindThinking(particle);
            if (slot == nullptr)
            {
                // No owner; clear thunk so next tenant does not get it.
                WriteInt(static_cast<uint8_t*>(particle), GameAddress::kUniParticleUpdateOffset, 0);
                return 0;
            }

            const AmodParticleThink think = slot->think;
            void* const user = slot->user;
            void* const game = slot->game;

            const int alive = think(particle, FrameDelta(), user);
            if (alive != 0)
            {
                return 1;
            }

            AttachUpdateRoutine attach = nullptr;
            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAttachUpdateSlot));
            if (at != nullptr)
            {
                attach = reinterpret_cast<AttachUpdateRoutine>(*at);
            }
            if (attach != nullptr && game != nullptr)
            {
                attach(particle, game);
            }

            ReleaseThinking(slot);
            return 0;
        }

        // Mod tags; slot arithmetic like thinks, cleared on birth.
        uint32_t g_tagged[kThinkingMax] = {};
        int g_taggedCount = 0;

        uint32_t TagOf(void* particle)
        {
            if (g_taggedCount == 0)
            {
                return 0;
            }

            const int slot = SlotOf(particle);
            return slot >= 0 ? g_tagged[slot] : 0;
        }

        void DropTag(void* particle)
        {
            if (g_taggedCount == 0)
            {
                return;
            }

            const int slot = SlotOf(particle);
            if (slot >= 0 && g_tagged[slot] != 0)
            {
                g_tagged[slot] = 0;
                --g_taggedCount;
            }
        }

        struct Drawing
        {
            ModContext* owner;
            AmodParticleDrawHandler handler;
            void* user;
        };

        struct Birthing
        {
            ModContext* owner;
            AmodParticleBirthHandler handler;
            void* user;
        };

        struct Spawning
        {
            ModContext* owner;
            AmodParticleSpawnHandler handler;
            void* user;
        };

        Drawing g_drawing = {};
        Spawning g_spawning = {};
        Birthing g_birthing = {};

        InlineHook g_drawHook;
        InlineHook g_spawnHook;
        InlineHook g_stepHook;
        SceneDrawRoutine g_gameDraw = nullptr;
        SpawnTrailRoutine g_gameSpawnTrail = nullptr;
        ParticleStepRoutine g_gameStep = nullptr;

        AllocKindRoutine g_gameAllocKind = nullptr;
        AllocTrailRoutine g_gameAllocTrail = nullptr;

        void __stdcall StepDetour(void* particle, float delta)
        {
            Thinking* const slot = FindThinking(particle);
            if (slot != nullptr)
            {
                if (slot->think(particle, delta, slot->user) == 0)
                {
                    // Life 0; walker frees on this pass.
                    WriteFloat(static_cast<uint8_t*>(particle), GameAddress::kParticleLifeOffset, 0.0f);
                    ReleaseThinking(slot);
                }
                return;
            }

            if (g_gameStep != nullptr)
            {
                g_gameStep(particle, delta);
            }
        }

        bool InstallStepHook()
        {
            if (g_stepHook.IsInstalled())
            {
                return true;
            }

            void* const target = GameModule::Resolve(GameAddress::kParticleStep);
            if (target == nullptr)
            {
                return false;
            }

            std::string error;
            if (!g_stepHook.Install(target, reinterpret_cast<void*>(&StepDetour), kParticleStepPrologue, sizeof(kParticleStepPrologue), error))
            {
                FORGE_LOG_ERROR("The particle step is not where it was reversed from: %s", error.c_str());
                return false;
            }

            g_gameStep = reinterpret_cast<ParticleStepRoutine>(g_stepHook.Trampoline());
            return true;
        }

        void* Born(void* particle, AmodParticleKind kind)
        {
            if (particle == nullptr)
            {
                return nullptr;
            }

            DropTag(particle);

            if (kind == AMOD_PARTICLE_KIND_UNI)
            {
                WriteInt(static_cast<uint8_t*>(particle), GameAddress::kUniParticleUpdateOffset, 0);
            }

            if (g_birthing.handler == nullptr)
            {
                return particle;
            }

            if (g_birthing.handler(particle, kind, g_birthing.user) != 0)
            {
                return particle;
            }

            if (kind == AMOD_PARTICLE_KIND_UNI)
            {
                // No think => UpdateUni frees next pass.
                WriteInt(static_cast<uint8_t*>(particle), GameAddress::kUniParticleUpdateOffset, 0);
            }
            else
            {
                WriteFloat(static_cast<uint8_t*>(particle), GameAddress::kParticleLifeOffset, 0.0f);
            }

            return nullptr;
        }

        void* __cdecl AllocKindThunk(int kind)
        {
            void* const particle = g_gameAllocKind != nullptr ? g_gameAllocKind(kind) : nullptr;
            return Born(particle, static_cast<AmodParticleKind>(kind));
        }

        void* __cdecl AllocTrailThunk(void)
        {
            void* const particle = g_gameAllocTrail != nullptr ? g_gameAllocTrail() : nullptr;
            return Born(particle, AMOD_PARTICLE_KIND_UNI);
        }

        // Re-hold every frame; game rewrites the slot table.
        void HoldBirthSlots()
        {
            void** const kindSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAllocKindSlot));
            if (kindSlot != nullptr && *kindSlot != reinterpret_cast<void*>(&AllocKindThunk))
            {
                g_gameAllocKind = reinterpret_cast<AllocKindRoutine>(*kindSlot);
                *kindSlot = reinterpret_cast<void*>(&AllocKindThunk);
            }

            void** const trailSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAllocTrailSlot));
            if (trailSlot != nullptr && *trailSlot != reinterpret_cast<void*>(&AllocTrailThunk))
            {
                g_gameAllocTrail = reinterpret_cast<AllocTrailRoutine>(*trailSlot);
                *trailSlot = reinterpret_cast<void*>(&AllocTrailThunk);
            }
        }

        void ReleaseBirthSlots()
        {
            void** const kindSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAllocKindSlot));
            if (kindSlot != nullptr && *kindSlot == reinterpret_cast<void*>(&AllocKindThunk) && g_gameAllocKind != nullptr)
            {
                *kindSlot = reinterpret_cast<void*>(g_gameAllocKind);
            }

            void** const trailSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAllocTrailSlot));
            if (trailSlot != nullptr && *trailSlot == reinterpret_cast<void*>(&AllocTrailThunk) && g_gameAllocTrail != nullptr)
            {
                *trailSlot = reinterpret_cast<void*>(g_gameAllocTrail);
            }
        }

        void __fastcall DrawDetour(void* scene, void* unused)
        {
            if (g_drawing.handler != nullptr && g_drawing.handler(g_drawing.user) != 0)
            {
                return;
            }

            if (g_gameDraw != nullptr)
            {
                g_gameDraw(scene, unused);
            }
        }

        bool InstallDrawHook()
        {
            void* const target = GameModule::Resolve(GameAddress::kSceneDrawParticles);
            if (target == nullptr)
            {
                return false;
            }

            std::string error;
            if (!g_drawHook.Install(target, reinterpret_cast<void*>(&DrawDetour),
                kSceneDrawParticlesPrologue, sizeof(kSceneDrawParticlesPrologue), error))
            {
                FORGE_LOG_ERROR("The particle draw is not where it was reversed from: %s", error.c_str());
                return false;
            }

            g_gameDraw = reinterpret_cast<SceneDrawRoutine>(g_drawHook.Trampoline());
            return true;
        }

        void __fastcall SpawnTrailDetour(void* source, void* unused, const float* origin, int index)
        {
            if (g_spawning.handler == nullptr)
            {
                if (g_gameSpawnTrail != nullptr)
                {
                    g_gameSpawnTrail(source, unused, origin, index);
                }
                return;
            }

            AmodParticleDesc desc = {};
            desc.structSize = sizeof(desc);
            desc.kind = AMOD_PARTICLE_KIND_UNI;
            desc.waypoint = static_cast<float>(index);

            if (origin != nullptr)
            {
                desc.position[0] = origin[0];
                desc.position[1] = origin[1];
                desc.position[2] = origin[2];
            }

            if (g_spawning.handler(&desc, g_spawning.user) == 0)
            {
                return;
            }

            // Handler may have moved position; use local copy.
            const float moved[3] = { desc.position[0], desc.position[1], desc.position[2] };

            if (g_gameSpawnTrail != nullptr)
            {
                g_gameSpawnTrail(source, unused, origin != nullptr ? moved : origin, index);
            }
        }

        bool InstallSpawnHook()
        {
            void* const target = GameModule::Resolve(GameAddress::kParticleSpawnTrail);
            if (target == nullptr)
            {
                return false;
            }

            std::string error;
            if (!g_spawnHook.Install(target, reinterpret_cast<void*>(&SpawnTrailDetour), kParticleSpawnTrailPrologue, sizeof(kParticleSpawnTrailPrologue), error))
            {
                FORGE_LOG_ERROR("The trail spawner is not where it was reversed from: %s", error.c_str());
                return false;
            }

            g_gameSpawnTrail = reinterpret_cast<SpawnTrailRoutine>(g_spawnHook.Trampoline());
            return true;
        }

        void WriteFloat(uint8_t* base, uintptr_t offset, float value)
        {
            if (offset != kNone)
            {
                memcpy(base + offset, &value, sizeof(value));
            }
        }

        void WriteInt(uint8_t* base, uintptr_t offset, int value)
        {
            if (offset != kNone)
            {
                memcpy(base + offset, &value, sizeof(value));
            }
        }

        float ReadFloat(const uint8_t* base, uintptr_t offset)
        {
            return offset != kNone ? Read<float>(base, offset) : 0.0f;
        }

        int ReadInt(const uint8_t* base, uintptr_t offset)
        {
            return offset != kNone ? Read<int>(base, offset) : 0;
        }

        void WriteVector(uint8_t* base, uintptr_t offset, const float* value)
        {
            if (offset != kNone)
            {
                memcpy(base + offset, value, sizeof(float) * 3);
            }
        }

        void ReadVector(const uint8_t* base, uintptr_t offset, float* out)
        {
            if (offset == kNone)
            {
                out[0] = 0.0f;
                out[1] = 0.0f;
                out[2] = 0.0f;
                return;
            }

            memcpy(out, base + offset, sizeof(float) * 3);
        }

        void ApplyFields(uint8_t* particle, const Layout& layout, const AmodParticleDesc* desc, uint32_t fields)
        {
            if ((fields & AMOD_PARTICLE_FIELD_POSITION) != 0)
            {
                WriteVector(particle, layout.position, desc->position);
            }
            if ((fields & AMOD_PARTICLE_FIELD_DIRECTION) != 0)
            {
                WriteVector(particle, layout.direction, desc->direction);
            }
            if ((fields & AMOD_PARTICLE_FIELD_WAYPOINT) != 0)
            {
                WriteFloat(particle, layout.waypoint, desc->waypoint);
            }
            if ((fields & AMOD_PARTICLE_FIELD_SPEED) != 0)
            {
                WriteFloat(particle, layout.speed, desc->speed);
            }
            if ((fields & AMOD_PARTICLE_FIELD_SIZE) != 0)
            {
                WriteFloat(particle, layout.size, desc->size);
            }
            if ((fields & AMOD_PARTICLE_FIELD_LIFE) != 0)
            {
                WriteFloat(particle, layout.life, desc->life);
            }
            if ((fields & AMOD_PARTICLE_FIELD_DECAY) != 0)
            {
                WriteFloat(particle, layout.decay, desc->decay);
            }
            if ((fields & AMOD_PARTICLE_FIELD_COLOUR) != 0)
            {
                if (layout.colour != kNone)
                {
                    memcpy(particle + layout.colour, desc->colour, sizeof(float) * 4);
                }

                // Uni drawn twice (per texture); each pass has its own colour.
                if (layout.colourB != kNone)
                {
                    memcpy(particle + layout.colourB, desc->colour, sizeof(float) * 4);
                }

                if (layout.colourFrom != kNone)
                {
                    memcpy(particle + layout.colourFrom, desc->colour, sizeof(float) * 3);
                }
                if (layout.colourTo != kNone)
                {
                    memcpy(particle + layout.colourTo, desc->colour, sizeof(float) * 3);
                }

                // colourDriver overrides written colour.
                if (layout.colourDriver != kNone)
                {
                    WriteInt(particle, layout.colourDriver, 0);
                }
            }
            if ((fields & AMOD_PARTICLE_FIELD_TEXTURES) != 0)
            {
                WriteInt(particle, layout.textureA, desc->textureA);
                WriteInt(particle, layout.textureB, desc->textureB);
            }
            if ((fields & AMOD_PARTICLE_FIELD_SHAPE) != 0 && layout.shape != kNone)
            {
                if (layout.shapeIsByte)
                {
                    const uint8_t shape = desc->shape != AMOD_PARTICLE_SHAPE_SPRITE ? 1 : 0;
                    memcpy(particle + layout.shape, &shape, sizeof(shape));
                }
                else
                {
                    WriteInt(particle, layout.shape, desc->shape);
                }
            }
            if ((fields & AMOD_PARTICLE_FIELD_MODEL) != 0)
            {
                WriteInt(particle, layout.model, desc->model);
                WriteInt(particle, layout.modelFrame, desc->modelFrame);
            }
            if ((fields & AMOD_PARTICLE_FIELD_PARENT) != 0)
            {
                WriteInt(particle, layout.parent, desc->parent);
            }
            if ((fields & AMOD_PARTICLE_FIELD_GRAVITY) != 0)
            {
                WriteVector(particle, layout.gravity, desc->gravity);
            }
            if ((fields & AMOD_PARTICLE_FIELD_GROWTH) != 0)
            {
                WriteFloat(particle, layout.speedGrow, desc->speedGrow);
                WriteFloat(particle, layout.sizeGrow, desc->sizeGrow);
            }
            if ((fields & AMOD_PARTICLE_FIELD_TURN) != 0)
            {
                WriteFloat(particle, layout.angle, desc->angle);
                WriteFloat(particle, layout.spin, desc->spin);
            }
            if ((fields & AMOD_PARTICLE_FIELD_GLYPH) != 0)
            {
                WriteFloat(particle, layout.glyphScaleX, desc->glyphScale[0]);
                WriteFloat(particle, layout.glyphScaleY, desc->glyphScale[1]);
            }
            if ((fields & AMOD_PARTICLE_FIELD_ANIMATION) != 0 && layout.animated != kNone)
            {
                const int at = desc->animation - 1;
                const bool known = at >= 0 && at < static_cast<int>(kAnimationMax) && g_animations[at].count != 0;

                WriteInt(particle, layout.animated, known ? 1 : 0);
                WriteInt(particle, layout.frameList, known ? static_cast<int>(reinterpret_cast<uintptr_t>(g_animations[at].frames)) : 0);
                WriteInt(particle, layout.frameCount, known ? g_animations[at].count : 0);
                WriteFloat(particle, layout.frameRate, desc->frameRate);
                WriteFloat(particle, layout.frameTime, 0.0f);
            }
        }

        int __cdecl ApiIsHere(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            return GameModule::Resolve(GameAddress::kUniParticleLiveHead) != nullptr ? 1 : 0;
        }

        int __cdecl ApiCount(AmodMod mod, AmodParticleKind kind)
        {
            if (Caller(mod) == nullptr || !KnownKind(kind))
            {
                return 0;
            }

            int count = 0;
            for (void* walk = LiveHead(kind); walk != nullptr; walk = Next(walk, kind))
            {
                ++count;
            }

            return count;
        }

        int __cdecl ApiCapacity(AmodMod mod, AmodParticleKind kind)
        {
            if (Caller(mod) == nullptr || !KnownKind(kind))
            {
                return 0;
            }

            return IsUni(kind)
                ? static_cast<int>(GameAddress::kUniParticleCapacity)
                : static_cast<int>(GameAddress::kParticlePoolCapacity);
        }

        AmodParticle __cdecl ApiAt(AmodMod mod, AmodParticleKind kind, int at)
        {
            if (Caller(mod) == nullptr || !KnownKind(kind) || at < 0)
            {
                return nullptr;
            }

            int index = 0;
            for (void* walk = LiveHead(kind); walk != nullptr; walk = Next(walk, kind))
            {
                if (index == at)
                {
                    return walk;
                }
                ++index;
            }

            return nullptr;
        }

        int __cdecl ApiKindOf(AmodMod mod, AmodParticle particle)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }

            return FindKind(particle);
        }

        AmodParticle __cdecl ApiSpawn(AmodMod mod, const AmodParticleDesc* desc)
        {
            ModContext* const caller = Caller(mod);
            if (caller == nullptr || desc == nullptr)
            {
                return nullptr;
            }

            if (!KnownKind(desc->kind))
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no particle kind %d", static_cast<int>(desc->kind));
                return nullptr;
            }

            void* particle = nullptr;

            if (IsUni(desc->kind))
            {
                void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAllocTrailSlot));
                const AllocTrailRoutine alloc = at != nullptr ? reinterpret_cast<AllocTrailRoutine>(*at) : nullptr;
                if (alloc == nullptr)
                {
                    return nullptr;
                }

                particle = alloc();
                if (particle == nullptr)
                {
                    Fail(mod, AMOD_FAIL_REFUSED, "every uni particle is in use");
                    return nullptr;
                }

                void** const attachAt = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAttachUpdateSlot));
                const AttachUpdateRoutine attach = attachAt != nullptr ? reinterpret_cast<AttachUpdateRoutine>(*attachAt) : nullptr;
                if (attach != nullptr)
                {
                    attach(particle, GameModule::Resolve(GameAddress::kParticleTrailUpdate));
                }
            }
            else
            {
                void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAllocKindSlot));
                const AllocKindRoutine alloc = at != nullptr ? reinterpret_cast<AllocKindRoutine>(*at) : nullptr;
                if (alloc == nullptr)
                {
                    return nullptr;
                }

                particle = alloc(static_cast<int>(desc->kind));
                if (particle == nullptr)
                {
                    Fail(mod, AMOD_FAIL_REFUSED, "every particle of kind %d is in use", static_cast<int>(desc->kind));
                    return nullptr;
                }
            }

            ApplyFields(static_cast<uint8_t*>(particle), LayoutOf(desc->kind), desc, AMOD_PARTICLE_FIELD_ALL);
            return particle;
        }

        int __cdecl ApiGet(AmodMod mod, AmodParticle particle, AmodParticleDesc* out)
        {
            if (Caller(mod) == nullptr || particle == nullptr || out == nullptr)
            {
                return 0;
            }

            const int kind = FindKind(particle);
            if (kind < 0)
            {
                return 0;
            }

            const Layout& layout = LayoutOf(kind);
            const uint8_t* const at = static_cast<const uint8_t*>(particle);

            AmodParticleDesc filled = {};
            filled.structSize = sizeof(AmodParticleDesc);
            filled.kind = static_cast<AmodParticleKind>(kind);

            ReadVector(at, layout.position, filled.position);
            ReadVector(at, layout.direction, filled.direction);
            filled.waypoint = ReadFloat(at, layout.waypoint);
            filled.speed = ReadFloat(at, layout.speed);
            filled.size = ReadFloat(at, layout.size);
            filled.life = ReadFloat(at, layout.life);
            filled.decay = ReadFloat(at, layout.decay);

            if (layout.colour != kNone)
            {
                memcpy(filled.colour, at + layout.colour, sizeof(float) * 4);
            }

            filled.textureA = ReadInt(at, layout.textureA);
            filled.textureB = ReadInt(at, layout.textureB);

            if (layout.shape != kNone)
            {
                filled.shape = layout.shapeIsByte
                    ? (Read<uint8_t>(at, layout.shape) != 0 ? AMOD_PARTICLE_SHAPE_QUAD : AMOD_PARTICLE_SHAPE_SPRITE)
                    : ReadInt(at, layout.shape);
            }

            filled.model = ReadInt(at, layout.model);
            filled.modelFrame = ReadInt(at, layout.modelFrame);
            filled.parent = ReadInt(at, layout.parent);

            ReadVector(at, layout.gravity, filled.gravity);
            filled.speedGrow = ReadFloat(at, layout.speedGrow);
            filled.sizeGrow = ReadFloat(at, layout.sizeGrow);
            filled.angle = ReadFloat(at, layout.angle);
            filled.spin = ReadFloat(at, layout.spin);
            filled.glyphScale[0] = ReadFloat(at, layout.glyphScaleX);
            filled.glyphScale[1] = ReadFloat(at, layout.glyphScaleY);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSet(AmodMod mod, AmodParticle particle, const AmodParticleDesc* desc, uint32_t fields)
        {
            if (Caller(mod) == nullptr || particle == nullptr || desc == nullptr)
            {
                return 0;
            }

            const int kind = FindKind(particle);
            if (kind < 0)
            {
                return 0;
            }

            ApplyFields(static_cast<uint8_t*>(particle), LayoutOf(kind), desc, fields);
            return 1;
        }

        int __cdecl ApiKill(AmodMod mod, AmodParticle particle)
        {
            if (Caller(mod) == nullptr || particle == nullptr)
            {
                return 0;
            }

            const int kind = FindKind(particle);
            if (kind < 0)
            {
                return 0;
            }

            WriteFloat(static_cast<uint8_t*>(particle), LayoutOf(kind).life, 0.0f);
            return 1;
        }

        int __cdecl ApiClear(AmodMod mod, AmodParticleKind kind)
        {
            if (Caller(mod) == nullptr || !KnownKind(kind))
            {
                return 0;
            }

            const Layout& layout = LayoutOf(kind);

            for (void* walk = LiveHead(kind); walk != nullptr; walk = Next(walk, kind))
            {
                WriteFloat(static_cast<uint8_t*>(walk), layout.life, 0.0f);
            }

            return 1;
        }

        int __cdecl ApiSetThink(AmodMod mod, AmodParticle particle, AmodParticleThink think, void* user)
        {
            ModContext* const caller = Caller(mod);
            if (caller == nullptr || particle == nullptr || think == nullptr)
            {
                return 0;
            }

            const int kind = FindKind(particle);
            if (kind < 0)
            {
                return 0;
            }

            if (!IsUni(kind) && !InstallStepHook())
            {
                return 0;
            }

            const int index = SlotOf(particle);
            if (index < 0)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "that is not a particle");
                return 0;
            }

            Thinking* const slot = &g_thinking[index];

            if (slot->think == nullptr)
            {
                ++g_thinkingCount;
            }

            slot->owner = caller;
            slot->particle = particle;
            slot->think = think;
            slot->user = user;

            if (!IsUni(kind))
            {
                // Pool: step detour finds by particle address.
                return 1;
            }

            uint8_t* const at = static_cast<uint8_t*>(particle);
            void* const current = Read<void*>(at, GameAddress::kUniParticleUpdateOffset);
            if (current != reinterpret_cast<void*>(&ThinkThunk))
            {
                slot->game = current;
            }

            void** const attachAt = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAttachUpdateSlot));
            const AttachUpdateRoutine attach = attachAt != nullptr ? reinterpret_cast<AttachUpdateRoutine>(*attachAt) : nullptr;
            if (attach == nullptr)
            {
                ReleaseThinking(slot);
                return 0;
            }

            attach(particle, reinterpret_cast<void*>(&ThinkThunk));
            return 1;
        }

        void RestoreThink(Thinking* slot)
        {
            if (slot == nullptr || slot->particle == nullptr)
            {
                return;
            }

            void** const attachAt = static_cast<void**>(GameModule::Resolve(GameAddress::kParticleAttachUpdateSlot));
            const AttachUpdateRoutine attach = attachAt != nullptr ? reinterpret_cast<AttachUpdateRoutine>(*attachAt) : nullptr;

            if (attach != nullptr && slot->game != nullptr)
            {
                attach(slot->particle, slot->game);
            }

            ReleaseThinking(slot);
        }

        int __cdecl ApiClearThink(AmodMod mod, AmodParticle particle)
        {
            if (Caller(mod) == nullptr || particle == nullptr)
            {
                return 0;
            }

            Thinking* const slot = FindThinking(particle);
            if (slot == nullptr)
            {
                return 0;
            }

            RestoreThink(slot);
            return 1;
        }

        int __cdecl ApiSetDrawHandler(AmodMod mod, AmodParticleDrawHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);
            if (caller == nullptr)
            {
                return 0;
            }

            if (handler == nullptr)
            {
                if (g_drawing.owner == caller)
                {
                    g_drawing = Drawing{};
                }
                return 1;
            }

            if (!g_drawHook.IsInstalled() && !InstallDrawHook())
            {
                return 0;
            }

            g_drawing.owner = caller;
            g_drawing.handler = handler;
            g_drawing.user = user;
            return 1;
        }

        int __cdecl ApiSetSpawnHandler(AmodMod mod, AmodParticleSpawnHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);
            if (caller == nullptr)
            {
                return 0;
            }

            if (handler == nullptr)
            {
                if (g_spawning.owner == caller)
                {
                    g_spawning = Spawning{};
                }
                return 1;
            }

            if (!g_spawnHook.IsInstalled() && !InstallSpawnHook())
            {
                return 0;
            }

            g_spawning.owner = caller;
            g_spawning.handler = handler;
            g_spawning.user = user;
            return 1;
        }

        int __cdecl ApiSetBirthHandler(AmodMod mod, AmodParticleBirthHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);
            if (caller == nullptr)
            {
                return 0;
            }

            if (handler == nullptr)
            {
                if (g_birthing.owner == caller)
                {
                    g_birthing = Birthing{};
                }
                return 1;
            }

            HoldBirthSlots();
            if (g_gameAllocKind == nullptr)
            {
                return 0;
            }

            g_birthing.owner = caller;
            g_birthing.handler = handler;
            g_birthing.user = user;
            return 1;
        }

        int __cdecl ApiSetTag(AmodMod mod, AmodParticle particle, uint32_t tag)
        {
            if (Caller(mod) == nullptr || particle == nullptr)
            {
                return 0;
            }

            if (tag == 0)
            {
                DropTag(particle);
                return 1;
            }

            const int slot = SlotOf(particle);
            if (slot < 0)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "that is not a particle");
                return 0;
            }

            if (g_tagged[slot] == 0)
            {
                ++g_taggedCount;
            }

            g_tagged[slot] = tag;
            return 1;
        }

        int __cdecl ApiCreateAnimation(AmodMod mod, const int* textures, int count)
        {
            ModContext* const caller = Caller(mod);
            if (caller == nullptr || textures == nullptr || count <= 0)
            {
                return 0;
            }

            if (count > static_cast<int>(sizeof(g_animations[0].frames) / sizeof(int)))
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "an animation holds at most %d frames",
                    static_cast<int>(sizeof(g_animations[0].frames) / sizeof(int)));
                return 0;
            }

            for (size_t at = 0; at < kAnimationMax; ++at)
            {
                if (g_animations[at].count != 0)
                {
                    continue;
                }

                g_animations[at].owner = caller;
                g_animations[at].count = count;
                memcpy(g_animations[at].frames, textures, sizeof(int) * static_cast<size_t>(count));
                return static_cast<int>(at) + 1;
            }

            Fail(mod, AMOD_FAIL_REFUSED, "no room left for another animation");
            return 0;
        }

        int __cdecl ApiSpawnText(AmodMod mod, const char* text, const float* position, const float* scale, float speed, float decay, const float* direction, 
            float gravity)
        {
            if (Caller(mod) == nullptr || text == nullptr || text[0] == '\0' || position == nullptr)
            {
                return 0;
            }

            const uint8_t* const session = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kGameSession));
            if (session == nullptr)
            {
                return 0;
            }

            const int font = Read<int>(session, GameAddress::kSessionTextFontOffset);
            if (font == 0)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "there is no text font until a game is running");
                return 0;
            }

            const SpawnTextRoutine spawn = reinterpret_cast<SpawnTextRoutine>(GameModule::Resolve(GameAddress::kParticleSpawnText));
            if (spawn == nullptr)
            {
                return 0;
            }

            // Scale is relative to session text size (font units).
            float own[2] = { 1.0f, 1.0f };
            memcpy(own, session + GameAddress::kSessionTextScaleOffset, sizeof(own));

            const float wanted[2] =
            {
                own[0] * (scale != nullptr ? scale[0] : 1.0f),
                own[1] * (scale != nullptr ? scale[1] : 1.0f)
            };

            const float up[3] = { 0.0f, 1.0f, 0.0f };

            spawn(font, position, text, wanted, speed, decay, direction != nullptr ? direction : up, gravity);
            return 1;
        }

        int __cdecl ApiTextScale(AmodMod mod, float* out)
        {
            if (Caller(mod) == nullptr || out == nullptr)
            {
                return 0;
            }

            const uint8_t* const session = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kGameSession));
            if (session == nullptr)
            {
                return 0;
            }

            memcpy(out, session + GameAddress::kSessionTextScaleOffset, sizeof(float) * 2);
            return 1;
        }

        int __cdecl ApiTextFont(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            // Session is a static object, not a pointer.
            const uint8_t* const session = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kGameSession));
            if (session == nullptr)
            {
                return 0;
            }

            return Read<int>(session, GameAddress::kSessionTextFontOffset);
        }

        uint32_t __cdecl ApiGetTag(AmodMod mod, AmodParticle particle)
        {
            if (Caller(mod) == nullptr || particle == nullptr)
            {
                return 0;
            }

            return TagOf(particle);
        }
    }

    bool ParticlesApi::Install()
    {
        if (GameModule::Resolve(GameAddress::kUniParticleLiveHead) == nullptr)
        {
            FORGE_LOG_ERROR("The particles are not where they were reversed from");
            return false;
        }

        g_table.structSize = sizeof(AmodParticlesApi);
        g_table.IsHere = &ApiIsHere;
        g_table.Count = &ApiCount;
        g_table.Capacity = &ApiCapacity;
        g_table.At = &ApiAt;
        g_table.KindOf = &ApiKindOf;
        g_table.Spawn = &ApiSpawn;
        g_table.Get = &ApiGet;
        g_table.Set = &ApiSet;
        g_table.Kill = &ApiKill;
        g_table.Clear = &ApiClear;
        g_table.SetThink = &ApiSetThink;
        g_table.ClearThink = &ApiClearThink;
        g_table.SetDrawHandler = &ApiSetDrawHandler;
        g_table.SetSpawnHandler = &ApiSetSpawnHandler;
        g_table.SetBirthHandler = &ApiSetBirthHandler;
        g_table.SetTag = &ApiSetTag;
        g_table.GetTag = &ApiGetTag;
        g_table.TextFont = &ApiTextFont;
        g_table.SpawnText = &ApiSpawnText;
        g_table.CreateAnimation = &ApiCreateAnimation;
        g_table.TextScale = &ApiTextScale;

        // Slot table rebuilt by game; re-hold thunks every frame.
        FrameApi::AddInternalTick(&HoldBirthSlots);

        return true;
    }

    void ParticlesApi::Uninstall()
    {
        for (size_t at = 0; at < kThinkingMax; ++at)
        {
            RestoreThink(&g_thinking[at]);
        }

        g_drawing = Drawing{};
        g_spawning = Spawning{};
        g_birthing = Birthing{};

        ReleaseBirthSlots();
        g_gameAllocKind = nullptr;
        g_gameAllocTrail = nullptr;

        for (size_t at = 0; at < kThinkingMax; ++at)
        {
            g_tagged[at] = 0;
        }
        g_taggedCount = 0;

        g_drawHook.Uninstall();
        g_spawnHook.Uninstall();
        g_stepHook.Uninstall();
        g_gameDraw = nullptr;
        g_gameSpawnTrail = nullptr;
        g_gameStep = nullptr;

        g_table = AmodParticlesApi{};
    }

    void ParticlesApi::ForgetMod(ModContext* owner)
    {
        for (size_t at = 0; at < kThinkingMax; ++at)
        {
            if (g_thinking[at].owner == owner)
            {
                RestoreThink(&g_thinking[at]);
            }
        }

        if (g_drawing.owner == owner)
        {
            g_drawing = Drawing{};
        }

        if (g_spawning.owner == owner)
        {
            g_spawning = Spawning{};
        }

        if (g_birthing.owner == owner)
        {
            g_birthing = Birthing{};
        }

        // Detach animations this mod owned so particles do not read freed frames.
        for (size_t at = 0; at < kAnimationMax; ++at)
        {
            if (g_animations[at].owner == owner)
            {
                for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind < AMOD_PARTICLE_KIND_UNI; ++kind)
                {
                    for (void* walk = LiveHead(kind); walk != nullptr; walk = Next(walk, kind))
                    {
                        uint8_t* const one = static_cast<uint8_t*>(walk);
                        if (Read<int>(one, GameAddress::kParticleFrameListOffset) == static_cast<int>(reinterpret_cast<uintptr_t>(g_animations[at].frames)))
                        {
                            WriteInt(one, GameAddress::kParticleAnimatedOffset, 0);
                            WriteInt(one, GameAddress::kParticleFrameListOffset, 0);
                        }
                    }
                }

                g_animations[at] = Animation{};
            }
        }
    }

    const AmodParticlesApi* ParticlesApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
