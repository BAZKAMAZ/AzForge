#ifndef AZFORGE_PARTICLES_API_H
#define AZFORGE_PARTICLES_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Game object; valid until the particle finishes on its own. */
typedef void* AmodParticle;

/* Four stores: three pools of 2048 + uni (trails). */
typedef enum AmodParticleKind
{
    AMOD_PARTICLE_KIND_POOL_0 = 0,
    AMOD_PARTICLE_KIND_POOL_1 = 1,

    /* Allocates; game never draws it -- only useful with a draw handler. */
    AMOD_PARTICLE_KIND_POOL_2 = 2,

    AMOD_PARTICLE_KIND_UNI = 3
} AmodParticleKind;

/* Draw shape. Uni only picks sprite or quad. */
#define AMOD_PARTICLE_SHAPE_SPRITE                                  0
#define AMOD_PARTICLE_SHAPE_QUAD                                    3
#define AMOD_PARTICLE_SHAPE_MODEL                                   4

/* Particle fields. Kind-specific ones are ignored/zeroed*/
typedef struct AmodParticleDesc
{
    uint32_t structSize;

    AmodParticleKind kind;

    float position[3];
    float direction[3]; /* normalised; pool only */

    /* Uni only: waypoint index (not a point). */
    float waypoint;

    float speed;
    float size;

    /* Life spent at decay/sec; finishes at zero. */
    float life;
    float decay;

    float colour[4];

    /* Texture ids (Textures::Resolve); two layers, two passes. */
    int textureA;
    int textureB;

    int shape; /* AMOD_PARTICLE_SHAPE_* */

    int model;
    int modelFrame;

    /* Parent entity; pos added, scale multiplies. */
    int parent;

    /* Pool only. Per frame: speed+=grow*dt; dir+=gravity*dt; pos+=dir*speed*dt. */
    float gravity[3];
    float speedGrow;

    /* Pool only; size/sec. Negative size ends the particle. */
    float sizeGrow;

    /* Pool only: angle and spin rate. */
    float angle;
    float spin;

    /* Pool + SHAPE_MODEL only; zero draws nothing. */
    float glyphScale[2];

    /* Pool only: CreateAnimation id + frames/sec. Zero leaves texture alone. */
    int animation;
    float frameRate;

} AmodParticleDesc;

AMOD_LAST_FIELD(AmodParticleDesc, frameRate, 0x84);

    /* Set mask; Spawn takes all fields. */
#define AMOD_PARTICLE_FIELD_POSITION                                0x0001
#define AMOD_PARTICLE_FIELD_DIRECTION                               0x0002
#define AMOD_PARTICLE_FIELD_WAYPOINT                                0x0004
#define AMOD_PARTICLE_FIELD_SPEED                                   0x0008
#define AMOD_PARTICLE_FIELD_SIZE                                    0x0010
#define AMOD_PARTICLE_FIELD_LIFE                                    0x0020
#define AMOD_PARTICLE_FIELD_DECAY                                   0x0040
#define AMOD_PARTICLE_FIELD_COLOUR                                  0x0080
#define AMOD_PARTICLE_FIELD_TEXTURES                                0x0100
#define AMOD_PARTICLE_FIELD_SHAPE                                   0x0200
#define AMOD_PARTICLE_FIELD_MODEL                                   0x0400
#define AMOD_PARTICLE_FIELD_PARENT                                  0x0800
#define AMOD_PARTICLE_FIELD_GRAVITY                                 0x1000
#define AMOD_PARTICLE_FIELD_GROWTH                                  0x2000 /* speedGrow + sizeGrow */
#define AMOD_PARTICLE_FIELD_TURN                                    0x4000 /* angle + spin */
#define AMOD_PARTICLE_FIELD_GLYPH                                   0x8000
#define AMOD_PARTICLE_FIELD_ANIMATION                               0x10000
#define AMOD_PARTICLE_FIELD_ALL                                     0x1FFFF

/* Per-particle think instead of the game's. Return 0 to finish it. */
typedef int(__cdecl* AmodParticleThink)(AmodParticle particle, float delta, void* user);

/* On alloc, before fill. Return 0 to refuse. Use SetTag here to mark ownership. */
typedef int(__cdecl* AmodParticleBirthHandler)(AmodParticle particle, AmodParticleKind kind, void* user);

/* Replaces the whole particle draw; nonzero skips the game's. */
typedef int(__cdecl* AmodParticleDrawHandler)(void* user);

/* Before a game trail spawn; edit desc or return 0 to refuse. */
typedef int(__cdecl* AmodParticleSpawnHandler)(AmodParticleDesc* desc, void* user);

typedef struct AmodParticlesApi
{
    uint32_t structSize;

    /* false in menu / between levels / loading */
    int(__cdecl* IsHere)(AmodMod mod);

    /* Live count and capacity for a kind. */
    int(__cdecl* Count)(AmodMod mod, AmodParticleKind kind);
    int(__cdecl* Capacity)(AmodMod mod, AmodParticleKind kind);

    /* Live list, newest first; order not stable across frames. */
    AmodParticle(__cdecl* At)(AmodMod mod, AmodParticleKind kind, int at);

    /* Kind of a handle, or -1 if not live. */
    int(__cdecl* KindOf)(AmodMod mod, AmodParticle particle);

    /* Alloc + fill; null if that store is full. */
    AmodParticle(__cdecl* Spawn)(AmodMod mod, const AmodParticleDesc* desc);

    /* set out->structSize first */
    int(__cdecl* Get)(AmodMod mod, AmodParticle particle, AmodParticleDesc* out);
    int(__cdecl* Set)(AmodMod mod, AmodParticle particle, const AmodParticleDesc* desc, uint32_t fields);

    /* Spend life; walker frees next update. */
    int(__cdecl* Kill)(AmodMod mod, AmodParticle particle);

    /* End every live particle of a kind. */
    int(__cdecl* Clear)(AmodMod mod, AmodParticleKind kind);

    /* Any kind; ClearThink restores the game step. */
    int(__cdecl* SetThink)(AmodMod mod, AmodParticle particle, AmodParticleThink think, void* user);
    int(__cdecl* ClearThink)(AmodMod mod, AmodParticle particle);

    int(__cdecl* SetDrawHandler)(AmodMod mod, AmodParticleDrawHandler handler, void* user);

    /* Bonus trail only. For every particle use SetBirthHandler. */
    int(__cdecl* SetSpawnHandler)(AmodMod mod, AmodParticleSpawnHandler handler, void* user);

    int(__cdecl* SetBirthHandler)(AmodMod mod, AmodParticleBirthHandler handler, void* user);

    /* Mod mark; cleared on rebirth. Zero = untagged. */
    int(__cdecl* SetTag)(AmodMod mod, AmodParticle particle, uint32_t tag);
    uint32_t(__cdecl* GetTag)(AmodMod mod, AmodParticle particle);

    /* Score font; model+frame+SHAPE_MODEL = a letter. 0 if no game. */
    int(__cdecl* TextFont)(AmodMod mod);

    /* Score-style floating text. scale is a multiple of the game score size (1 = same). */
    int(__cdecl* SpawnText)(AmodMod mod, const char* text, const float* position, const float* scale,
        float speed, float decay, const float* direction, float gravity);

    /* Copy frames into the loader; returns id for desc.animation. Freed on unload. */
    int(__cdecl* CreateAnimation)(AmodMod mod, const int* textures, int count);

    /* Game score text size (font units); glyphScale should be a multiple of this. */
    int(__cdecl* TextScale)(AmodMod mod, float* out);

} AmodParticlesApi;

AMOD_LAST_FIELD(AmodParticlesApi, TextScale, 0x54);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Particles
    {
        inline const AmodParticlesApi* g_particles = nullptr;
        inline AmodMod g_particlesSelf = nullptr;

        inline void                                                 Attach(const AmodParticlesApi* api, AmodMod self)
        {
            g_particles = api;
            g_particlesSelf = self;
        }

        inline bool                                                 IsHere()
        {
            return g_particles != nullptr && g_particles->IsHere(g_particlesSelf) != 0;
        }

        inline int                                                  Count(AmodParticleKind kind)
        {
            return g_particles != nullptr ? g_particles->Count(g_particlesSelf, kind) : 0;
        }

        inline int                                                  Capacity(AmodParticleKind kind)
        {
            return g_particles != nullptr ? g_particles->Capacity(g_particlesSelf, kind) : 0;
        }

        inline AmodParticle                                         At(AmodParticleKind kind, int at)
        {
            return g_particles != nullptr ? g_particles->At(g_particlesSelf, kind, at) : nullptr;
        }

        inline int                                                  KindOf(AmodParticle particle)
        {
            return g_particles != nullptr ? g_particles->KindOf(g_particlesSelf, particle) : -1;
        }

        inline AmodParticle                                         Spawn(const AmodParticleDesc& desc)
        {
            return g_particles != nullptr ? g_particles->Spawn(g_particlesSelf, &desc) : nullptr;
        }

        inline bool                                                 Get(AmodParticle particle, AmodParticleDesc& out)
        {
            out.structSize = sizeof(AmodParticleDesc);
            return g_particles != nullptr && g_particles->Get(g_particlesSelf, particle, &out) != 0;
        }

        inline bool                                                 Set(AmodParticle particle, const AmodParticleDesc& desc, uint32_t fields = AMOD_PARTICLE_FIELD_ALL)
        {
            return g_particles != nullptr && g_particles->Set(g_particlesSelf, particle, &desc, fields) != 0;
        }

        inline bool                                                 SetPosition(AmodParticle particle, float x, float y, float z)
        {
            AmodParticleDesc desc = {};
            desc.structSize = sizeof(desc);
            desc.position[0] = x;
            desc.position[1] = y;
            desc.position[2] = z;
            return Set(particle, desc, AMOD_PARTICLE_FIELD_POSITION);
        }

        inline bool                                                 SetColour(AmodParticle particle, float r, float g, float b, float a)
        {
            AmodParticleDesc desc = {};
            desc.structSize = sizeof(desc);
            desc.colour[0] = r;
            desc.colour[1] = g;
            desc.colour[2] = b;
            desc.colour[3] = a;
            return Set(particle, desc, AMOD_PARTICLE_FIELD_COLOUR);
        }

        inline bool                                                 SetSize(AmodParticle particle, float size)
        {
            AmodParticleDesc desc = {};
            desc.structSize = sizeof(desc);
            desc.size = size;
            return Set(particle, desc, AMOD_PARTICLE_FIELD_SIZE);
        }

        inline bool                                                 Kill(AmodParticle particle)
        {
            return g_particles != nullptr && g_particles->Kill(g_particlesSelf, particle) != 0;
        }

        inline bool                                                 Clear(AmodParticleKind kind)
        {
            return g_particles != nullptr && g_particles->Clear(g_particlesSelf, kind) != 0;
        }

        inline bool                                                 SetThink(AmodParticle particle, AmodParticleThink think, void* user = nullptr)
        {
            return g_particles != nullptr && g_particles->SetThink(g_particlesSelf, particle, think, user) != 0;
        }

        inline bool                                                 ClearThink(AmodParticle particle)
        {
            return g_particles != nullptr && g_particles->ClearThink(g_particlesSelf, particle) != 0;
        }

        inline bool                                                 SetDrawHandler(AmodParticleDrawHandler handler, void* user = nullptr)
        {
            return g_particles != nullptr && g_particles->SetDrawHandler(g_particlesSelf, handler, user) != 0;
        }

        inline bool                                                 SetSpawnHandler(AmodParticleSpawnHandler handler, void* user = nullptr)
        {
            return g_particles != nullptr && g_particles->SetSpawnHandler(g_particlesSelf, handler, user) != 0;
        }

        inline bool                                                 SetBirthHandler(AmodParticleBirthHandler handler, void* user = nullptr)
        {
            return g_particles != nullptr && g_particles->SetBirthHandler(g_particlesSelf, handler, user) != 0;
        }

        inline bool                                                 SetTag(AmodParticle particle, uint32_t tag)
        {
            return g_particles != nullptr && g_particles->SetTag(g_particlesSelf, particle, tag) != 0;
        }

        inline uint32_t                                             GetTag(AmodParticle particle)
        {
            return g_particles != nullptr ? g_particles->GetTag(g_particlesSelf, particle) : 0;
        }

        inline int                                                  TextFont()
        {
            return g_particles != nullptr ? g_particles->TextFont(g_particlesSelf) : 0;
        }

        inline bool                                                 TextScale(float* out)
        {
            return g_particles != nullptr && g_particles->TextScale(g_particlesSelf, out) != 0;
        }

        inline int                                                  CreateAnimation(const int* textures, int count)
        {
            return g_particles != nullptr ? g_particles->CreateAnimation(g_particlesSelf, textures, count) : 0;
        }

        inline bool                                                 SpawnText(const char* text, const float* position, float scale = 1.0f, float speed = 0.08f, 
            float decay = 0.5f, const float* direction = nullptr, float gravity = 0.0f)
        {
            const float pair[2] = { scale, scale };
            return g_particles != nullptr && g_particles->SpawnText(g_particlesSelf, text, position, pair, speed, decay, direction, gravity) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_PARTICLES_API_H */
