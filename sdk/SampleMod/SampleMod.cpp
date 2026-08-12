#include <ForgeApi.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

using namespace Amod;

bool g_holding = false;
bool g_tinting = false;
bool g_marking = false;
float g_tint[4] = { 0.0f, 1.0f, 0.0f, 1.0f };


const uint32_t kWordMark = 0xF17E;

struct Show
{
    bool running;
    float time;
    float nextBurst;
    float nextDrop;
    int bursts;
    bool wordSent;
    float origin[3];
};

Show g_show = {};

bool Same(const char* a, const char* b)
{
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

bool Off(int argc, const char** argv, int at)
{
    return argc > at && Same(argv[at], "off");
}

const char* KindName(int kind)
{
    switch (kind)
    {
    case AMOD_PARTICLE_KIND_POOL_0:
        return "pool 0";
    case AMOD_PARTICLE_KIND_POOL_1:
        return "pool 1";
    case AMOD_PARTICLE_KIND_POOL_2:
        return "pool 2 (never drawn)";
    default:
        return "uni";
    }
}

int __cdecl Hold(AmodParticle particle, float delta, void*)
{
    AmodParticleDesc desc = {};
    if (!Particles::Get(particle, desc))
    {
        return 0;
    }

    desc.position[1] += delta * 0.05f;
    Particles::Set(particle, desc, AMOD_PARTICLE_FIELD_POSITION);
    return 1;
}

int __cdecl DrawNothing(void*)
{
    return 1;
}

int __cdecl RefuseTrails(AmodParticleDesc*, void*)
{
    return 0;
}

const uint32_t kMine = 0xA2F0;

int __cdecl MarkBorn(AmodParticle particle, AmodParticleKind, void*)
{
    Particles::SetTag(particle, kMine);
    return 1;
}

int __cdecl RefuseBorn(AmodParticle, AmodParticleKind, void*)
{
    return 0;
}

float Between(float from, float to)
{
    return from + (to - from) * (static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
}

// Hue as three cosines a third of a turn apart - enough of a rainbow, and no table.
void Rainbow(float phase, float* out)
{
    out[0] = 0.5f + 0.5f * cosf(phase);
    out[1] = 0.5f + 0.5f * cosf(phase - 2.0944f);
    out[2] = 0.5f + 0.5f * cosf(phase + 2.0944f);
    out[3] = 1.0f;
}

int __cdecl MarkWord(AmodParticle particle, AmodParticleKind, void*)
{
    Particles::SetTag(particle, kWordMark);
    return 1;
}

void GlyphLook(AmodParticleDesc& desc, char glyph, float size)
{
    float scale[2] = { 0.004f, 0.004f };
    Particles::TextScale(scale);

    desc.structSize = sizeof(desc);
    desc.kind = AMOD_PARTICLE_KIND_POOL_1;
    desc.shape = AMOD_PARTICLE_SHAPE_MODEL;
    desc.model = Particles::TextFont();
    desc.modelFrame = static_cast<unsigned char>(glyph);
    desc.glyphScale[0] = scale[0] * size;
    desc.glyphScale[1] = scale[1] * size;
}

void Burst(const float* at)
{
    AmodParticleDesc desc = {};
    GlyphLook(desc, '*', 0.8f);

    desc.life = 1.0f;
    desc.decay = 0.7f;
    desc.gravity[1] = -0.4f;
    desc.sizeGrow = 0.0f;
    desc.spin = Between(-6.0f, 6.0f);

    for (int at_ = 0; at_ < 28; ++at_)
    {
        const float angle = Between(0.0f, 6.2832f);
        const float lift = Between(-0.6f, 1.0f);

        desc.position[0] = at[0];
        desc.position[1] = at[1];
        desc.position[2] = at[2];

        desc.direction[0] = cosf(angle);
        desc.direction[1] = lift;
        desc.direction[2] = sinf(angle) * 0.35f;
        desc.speed = Between(0.10f, 0.26f);

        // A colour of its own for every spark, not one for the whole burst
        desc.colour[0] = Between(0.2f, 1.0f);
        desc.colour[1] = Between(0.2f, 1.0f);
        desc.colour[2] = Between(0.2f, 1.0f);
        desc.colour[3] = 1.0f;

        Particles::Spawn(desc);
    }
}

void Drop(const float* over)
{
    AmodParticleDesc desc = {};
    GlyphLook(desc, '.', 1.2f);

    desc.life = 1.0f;
    desc.decay = 0.45f;
    desc.gravity[1] = -0.5f;
    desc.direction[1] = -1.0f;
    desc.speed = Between(0.05f, 0.12f);
    desc.colour[0] = 0.6f;
    desc.colour[1] = 0.8f;
    desc.colour[2] = 1.0f;
    desc.colour[3] = 1.0f;

    for (int made = 0; made < 4; ++made)
    {
        desc.position[0] = over[0] + Between(-0.35f, 0.35f);
        desc.position[1] = over[1] + Between(0.20f, 0.34f);
        desc.position[2] = over[2];
        Particles::Spawn(desc);
    }
}

int PaintTagged(const float* colour)
{
    int painted = 0;

    AmodParticleDesc desc = {};
    desc.structSize = sizeof(desc);
    desc.colour[0] = colour[0];
    desc.colour[1] = colour[1];
    desc.colour[2] = colour[2];
    desc.colour[3] = 1.0f;

    for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind <= AMOD_PARTICLE_KIND_UNI; ++kind)
    {
        const AmodParticleKind one = static_cast<AmodParticleKind>(kind);

        for (int at = Particles::Count(one) - 1; at >= 0; --at)
        {
            const AmodParticle particle = Particles::At(one, at);
            if (particle == nullptr || Particles::GetTag(particle) != kMine)
            {
                continue;
            }

            if (Particles::Set(particle, desc, AMOD_PARTICLE_FIELD_COLOUR))
            {
                ++painted;
            }
        }
    }

    return painted;
}

void Report()
{
    for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind <= AMOD_PARTICLE_KIND_UNI; ++kind)
    {
        const AmodParticleKind one = static_cast<AmodParticleKind>(kind);
        Console::Printf("  %-20s %4d live of %d\n", KindName(kind), Particles::Count(one), Particles::Capacity(one));
    }
}

int PaintAll(const float* colour)
{
    int painted = 0;

    AmodParticleDesc desc = {};
    desc.structSize = sizeof(desc);
    desc.colour[0] = colour[0];
    desc.colour[1] = colour[1];
    desc.colour[2] = colour[2];
    desc.colour[3] = colour[3];

    for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind <= AMOD_PARTICLE_KIND_UNI; ++kind)
    {
        const AmodParticleKind one = static_cast<AmodParticleKind>(kind);

        for (int at = Particles::Count(one) - 1; at >= 0; --at)
        {
            const AmodParticle particle = Particles::At(one, at);
            if (particle != nullptr && Particles::Set(particle, desc, AMOD_PARTICLE_FIELD_COLOUR))
            {
                ++painted;
            }
        }
    }

    return painted;
}

int HoldAll(bool holding)
{
    int touched = 0;

    for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind <= AMOD_PARTICLE_KIND_UNI; ++kind)
    {
        const AmodParticleKind one = static_cast<AmodParticleKind>(kind);

        for (int at = Particles::Count(one) - 1; at >= 0; --at)
        {
            const AmodParticle particle = Particles::At(one, at);
            if (particle == nullptr)
            {
                continue;
            }

            if (holding ? Particles::SetThink(particle, &Hold) : Particles::ClearThink(particle))
            {
                ++touched;
            }
        }
    }

    return touched;
}

AmodTextureId TextureFor(const char* path)
{
    static AmodTextureId held = AMOD_INVALID_TEXTURE;
    static const char* heldPath = nullptr;

    if (held != AMOD_INVALID_TEXTURE && heldPath != nullptr && Same(heldPath, path))
    {
        return held;
    }

    held = Textures::Load(path, true, true);
    heldPath = held != AMOD_INVALID_TEXTURE ? path : nullptr;
    return held;
}

bool BorrowLook(AmodParticleDesc& desc)
{
    for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind <= AMOD_PARTICLE_KIND_UNI; ++kind)
    {
        const AmodParticleKind one = static_cast<AmodParticleKind>(kind);
        const AmodParticle live = Particles::At(one, 0);

        AmodParticleDesc from = {};
        if (live == nullptr || !Particles::Get(live, from) || from.textureA == 0)
        {
            continue;
        }

        desc.textureA = from.textureA;
        desc.textureB = from.textureB;
        desc.shape = from.shape;
        if (from.size > 0.0f)
        {
            desc.size = from.size;
        }
        return true;
    }

    return false;
}

void RunShow(float delta)
{
    if (!g_show.running)
    {
        return;
    }

    g_show.time += delta;

    // Six bursts, moving corner to corner around him
    if (g_show.bursts < 6 && g_show.time >= g_show.nextBurst)
    {
        static const float corners[4][2] = { { -0.30f, 0.26f }, { 0.30f, 0.26f }, { -0.30f, 0.06f }, { 0.30f, 0.06f } };
        const float* const corner = corners[g_show.bursts % 4];

        const float at[3] = { g_show.origin[0] + corner[0], g_show.origin[1] + corner[1], g_show.origin[2] };
        Burst(at);

        ++g_show.bursts;
        g_show.nextBurst = g_show.time + 0.35f;
    }

    // Then the word, marked as it is born so the rainbow finds it and nothing else
    if (!g_show.wordSent && g_show.bursts >= 6)
    {
        const float where[3] = { g_show.origin[0], g_show.origin[1] + 0.12f, g_show.origin[2] };

        Particles::SetBirthHandler(&MarkWord);
        Particles::SpawnText("FIREWORK", where, 1.6f, 0.02f, 0.28f);
        Particles::SetBirthHandler(g_marking ? &MarkBorn : nullptr);

        g_show.wordSent = true;
    }

    // and the rain, while the word is still up
    if (g_show.wordSent && g_show.time >= g_show.nextDrop && g_show.time < 5.5f)
    {
        Drop(g_show.origin);
        g_show.nextDrop = g_show.time + 0.05f;
    }

    // The letters are repainted every frame
    if (g_show.wordSent)
    {
        float colour[4] = {};
        Rainbow(g_show.time * 4.0f, colour);

        AmodParticleDesc desc = {};
        desc.structSize = sizeof(desc);
        desc.colour[0] = colour[0];
        desc.colour[1] = colour[1];
        desc.colour[2] = colour[2];
        desc.colour[3] = 1.0f;

        for (int at = Particles::Count(AMOD_PARTICLE_KIND_POOL_1) - 1; at >= 0; --at)
        {
            const AmodParticle particle = Particles::At(AMOD_PARTICLE_KIND_POOL_1, at);
            if (particle != nullptr && Particles::GetTag(particle) == kWordMark)
            {
                Particles::Set(particle, desc, AMOD_PARTICLE_FIELD_COLOUR);
            }
        }
    }

    if (g_show.time > 6.5f)
    {
        g_show.running = false;
    }
}

void __cdecl ParticlesCommand(int argc, const char** argv, void*)
{
    if (!Particles::IsHere())
    {
        Console::Print("particles: not in a level\n");
        return;
    }

    const char* const what = argc > 1 ? argv[1] : "count";

    if (Same(what, "count"))
    {
        Report();
        Console::Printf("  hold %s, tint %s, mark %s\n", g_holding ? "on" : "off", g_tinting ? "on" : "off", g_marking ? "on" : "off");
        return;
    }

    if (Same(what, "paint") || Same(what, "tint"))
    {
        const bool lasting = Same(what, "tint");

        if (lasting && Off(argc, argv, 2))
        {
            g_tinting = false;
            Console::Print("particles: tint off\n");
            return;
        }

        float colour[4] = { 1.0f, 0.2f, 0.1f, 1.0f };
        if (argc > 4)
        {
            colour[0] = static_cast<float>(atof(argv[2]));
            colour[1] = static_cast<float>(atof(argv[3]));
            colour[2] = static_cast<float>(atof(argv[4]));
        }

        if (lasting)
        {
            g_tint[0] = colour[0];
            g_tint[1] = colour[1];
            g_tint[2] = colour[2];
            g_tinting = true;
            Console::Printf("particles: tint on, %.2f %.2f %.2f - every particle, every frame\n", colour[0], colour[1], colour[2]);
            return;
        }

        const int painted = PaintAll(colour);
        if (painted == 0)
        {
            Console::Print("particles: nothing is in the air - set off an effect or take a bonus\n");
            return;
        }

        Console::Printf("particles: %d painted %.2f %.2f %.2f\n", painted, colour[0], colour[1], colour[2]);
        return;
    }

    if (Same(what, "spawn"))
    {
        const int asked = argc > 2 ? atoi(argv[2]) : 0;
        const int wanted = asked > 0 ? asked : 16;
        const int kind = argc > 3 ? atoi(argv[3]) : AMOD_PARTICLE_KIND_POOL_1;

        float focus[3] = {};
        if (!Camera::GetFocus(focus))
        {
            Console::Print("particles: there is nobody to spawn them on\n");
            return;
        }

        AmodParticleDesc desc = {};
        desc.structSize = sizeof(desc);
        desc.kind = static_cast<AmodParticleKind>(kind);
        desc.size = 0.03f;
        desc.sizeGrow = -0.005f;
        desc.life = 1.0f;
        desc.decay = 0.4f;
        desc.speed = 0.25f;
        desc.gravity[1] = -0.6f;
        desc.spin = 3.0f;
        desc.colour[0] = 1.0f;
        desc.colour[1] = 0.4f;
        desc.colour[2] = 0.1f;
        desc.colour[3] = 1.0f;

        const char* const path = argc > 4 ? argv[4] : "textures/rooms/5_01_wall.jpg";
        desc.textureA = static_cast<int>(TextureFor(path));

        if (desc.textureA == AMOD_INVALID_TEXTURE)
        {
            if (!BorrowLook(desc))
            {
                Console::Printf("particles: '%s' would not load, and nothing is in the air to borrow from\n", path);
                return;
            }
            Console::Printf("particles: '%s' would not load, borrowed a look instead\n", path);
        }

        int made = 0;
        for (int at = 0; at < wanted; ++at)
        {
            const float spread = static_cast<float>(rand()) / RAND_MAX - 0.5f;
            const float across = static_cast<float>(rand()) / RAND_MAX - 0.5f;

            desc.position[0] = focus[0];
            desc.position[1] = focus[1];
            desc.position[2] = focus[2];

            desc.direction[0] = spread;
            desc.direction[1] = 1.0f;
            desc.direction[2] = across;

            if (Particles::Spawn(desc) != nullptr)
            {
                ++made;
            }
        }

        Console::Printf("particles: %d of %d spawned into %s\n", made, wanted, KindName(kind));
        return;
    }

    if (Same(what, "hold"))
    {
        g_holding = !Off(argc, argv, 2);
        if (!g_holding)
        {
            Console::Printf("particles: hold off, gave back %d\n", HoldAll(false));
            return;
        }

        Console::Printf("particles: hold on, standing in for %d so far - take a bonus\n", HoldAll(true));
        return;
    }

    if (Same(what, "clear"))
    {
        int ended = 0;
        for (int kind = AMOD_PARTICLE_KIND_POOL_0; kind <= AMOD_PARTICLE_KIND_UNI; ++kind)
        {
            const AmodParticleKind one = static_cast<AmodParticleKind>(kind);
            ended += Particles::Count(one);
            Particles::Clear(one);
        }

        Console::Printf("particles: %d ended\n", ended);
        return;
    }

    if (Same(what, "firework"))
    {
        if (!Camera::GetFocus(g_show.origin))
        {
            Console::Print("particles: there is nobody to set it off over\n");
            return;
        }

        g_show = Show{ true, 0.0f, 0.0f, 0.0f, 0, false,
            { g_show.origin[0], g_show.origin[1], g_show.origin[2] } };

        Console::Print("particles: six bursts, then the word and the rain\n");
        return;
    }

    if (Same(what, "hide"))
    {
        Console::Print(Particles::SetDrawHandler(&DrawNothing) ? "particles: the draw is ours\n" : "particles: could not take the draw\n");
        return;
    }

    if (Same(what, "show"))
    {
        Particles::SetDrawHandler(nullptr);
        Console::Print("particles: the draw is the game's again\n");
        return;
    }

    if (Same(what, "text"))
    {
        float focus[3] = {};
        Camera::GetFocus(focus);

        // The letters are spaced from the font's own metrics, which only the game knows
        const char* const words = argc > 2 ? argv[2] : "AZFORGE";
        const float scale = argc > 3 ? static_cast<float>(atof(argv[3])) : 1.0f;

        focus[1] += 0.05f;

        Console::Printf(Particles::SpawnText(words, focus, scale)
            ? "particles: '%s' floated at scale %.3f\n"
            : "particles: '%s' refused at scale %.3f - is a game running?\n", words, scale);
        return;
    }

    if (Same(what, "mark"))
    {
        g_marking = !Off(argc, argv, 2);
        if (!g_marking)
        {
            Particles::SetBirthHandler(nullptr);
            Console::Print("particles: no longer marking\n");
            return;
        }

        Console::Print(Particles::SetBirthHandler(&MarkBorn)
            ? "particles: marking every new particle; only those get painted now\n"
            : "particles: could not take the births\n");
        return;
    }

    if (Same(what, "none"))
    {
        const bool off = Off(argc, argv, 2);
        Particles::SetBirthHandler(off ? nullptr : &RefuseBorn);
        Console::Print(off ? "particles: births allowed again\n" : "particles: every birth refused\n");
        return;
    }

    if (Same(what, "quiet"))
    {
        Console::Print(Particles::SetSpawnHandler(&RefuseTrails) ? "particles: bonus trails refused\n" : "particles: could not take the spawn\n");
        return;
    }

    if (Same(what, "loud"))
    {
        Particles::SetSpawnHandler(nullptr);
        Console::Print("particles: bonus trails allowed again\n");
        return;
    }

    Console::Print("particles: count | paint r g b | tint r g b | tint off | spawn [n] [kind] [texture]\n");
    Console::Print("           text [word] [scale] | hold [off] | mark [off] | none [off] | clear\n");
    Console::Print("           firework | hide | show | quiet | loud\n");
    Console::Print("  uni particles exist only while a bonus is being taken\n");
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("particles", &ParticlesCommand);

    Log(AMOD_LOG_INFO, "%s is up", ModId());
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame)
{
    RunShow(frame != nullptr ? frame->delta : 0.0f);

    if (g_tinting)
    {
        if (g_marking)
        {
            PaintTagged(g_tint);
        }
        else
        {
            PaintAll(g_tint);
        }
    }

    if (g_holding)
    {
        HoldAll(true);
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
