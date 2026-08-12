#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        // Four stores 0x005177B8..0x006797EC: three same-shape pools + uni.
        // Pool: 2048 x 0xCC; free/live lists share the same link.
        constexpr size_t kParticlePoolCount = 3;
        constexpr size_t kParticlePoolCapacity = 2048;
        constexpr size_t kParticleSize = 0xCC;
        constexpr uintptr_t kParticleLinkOffset = 0xC8;

        constexpr uintptr_t kParticleArenaOffset = 0x04;
        constexpr uintptr_t kParticlePoolFreeHeadOffset = 0x66004;
        constexpr uintptr_t kParticlePoolLiveHeadOffset = 0x66008;

        // Indexed by Particle_AllocKind kind.
        constexpr uintptr_t kParticlePools[kParticlePoolCount] = { 0x005177B8, 0x006137E0, 0x0057D7C8 };
        constexpr uintptr_t kParticleLiveHeads[kParticlePoolCount] = { 0x0057D7C0, 0x006797E8, 0x005E37D0 };

        // Draw walks kinds 0, 1 and uni. Kind 2 is never drawn.
        constexpr int kParticleKindDrawn[kParticlePoolCount] = { 1, 1, 0 };

        // Pooled particle layout; link at 0xC8.
        constexpr uintptr_t kParticleTextureAOffset = 0x00;
        constexpr uintptr_t kParticleTextureBOffset = 0x04;
        constexpr uintptr_t kParticlePositionOffset = 0x08; // three floats
        constexpr uintptr_t kParticleSizeOffset = 0x14;
        constexpr uintptr_t kParticleParentOffset = 0x2C; // entity; pos added, 0x25C..0x264 scales particle
        constexpr uintptr_t kParticleModelOffset = 0x34; // shape 4 draws this instead of a sprite
        constexpr uintptr_t kParticleModelFrameOffset = 0x38;
        constexpr uintptr_t kParticleDirectionOffset = 0x6C; // three floats, normalised
        constexpr uintptr_t kParticleSpeedOffset = 0x78;

        // Particle_Step: speed += grow*dt; dir += gravity*dt; pos += dir*speed*dt.
        constexpr uintptr_t kParticleSpeedGrowOffset = 0x7C;
        constexpr uintptr_t kParticleGravityOffset = 0x80; // three floats

        // Flipbook: when set, step walks frames into texture +0x00.
        constexpr uintptr_t kParticleAnimatedOffset = 0x44; // one byte
        constexpr uintptr_t kParticleFrameListOffset = 0x48; // texture ids
        constexpr uintptr_t kParticleFrameCountOffset = 0x4C;
        constexpr uintptr_t kParticleFrameRateOffset = 0x50;
        constexpr uintptr_t kParticleFrameTimeOffset = 0x54;

        // Only when shape draws a model or glyph.
        constexpr uintptr_t kParticleGlyphScaleXOffset = 0x3C;
        constexpr uintptr_t kParticleGlyphScaleYOffset = 0x40;

        // Session font; Score_Add hands it to Particle_SpawnText.
        constexpr uintptr_t kSessionTextFontOffset = 0xC44;

        // Text scale (font units ~0.004); mod sizes should be multiples of this.
        constexpr uintptr_t kSessionTextScaleOffset = 0xC48;
        // Angle + spin; step does angle += spin * delta.
        constexpr uintptr_t kParticleAngleOffset = 0x60;
        constexpr uintptr_t kParticleSpinOffset = 0x64;

        // Quad draw angle (vtable+0x88); only shape 3; not stepped.
        constexpr uintptr_t kParticleQuadAngleOffset = 0xC4;

        // Life + decay/sec; UpdatePool frees at life==0.
        constexpr uintptr_t kParticleLifeOffset = 0x58;
        constexpr uintptr_t kParticleDecayOffset = 0x5C;

        constexpr uintptr_t kParticleSizeGrowOffset = 0x18;

        constexpr uintptr_t kParticleColourDriverOffset = 0x30;
        constexpr uintptr_t kParticleShapeOffset = 0x68; // 3 draws a quad, 4 draws a model, else a sprite
        constexpr uintptr_t kParticleQuadOffset = 0xAC; // six floats, only read when shape is 3

        // RGBA (4 floats); draw hands to vtable+0x18. 0x60 is NOT colour.
        constexpr uintptr_t kParticleColourOffset = 0x1C;

        // Shape 2: colour lerped from/to each frame; write all three.
        constexpr uintptr_t kParticleColourFromOffset = 0x8C;
        constexpr uintptr_t kParticleColourToOffset = 0x9C;

        // Alpha = colour[3]; step overwrites with life each frame -- hold life instead.
        constexpr uintptr_t kParticleAlphaOffset = 0x28;

        // Uni particles: 1024 x 0xC0; array between the two list heads.
        constexpr size_t kUniParticleCapacity = 1024;
        constexpr size_t kUniParticleSize = 0xC0;
        constexpr uintptr_t kUniParticles = 0x005E37DC;
        constexpr uintptr_t kUniParticleLiveHead = 0x005E37D8;
        constexpr uintptr_t kUniParticleFreeHead = 0x006137DC;

        // Alloc zeroes through 0xB8; update + link follow.
        constexpr uintptr_t kUniParticleUpdateOffset = 0xB8;
        constexpr uintptr_t kUniParticleLinkOffset = 0xBC;

        // Uni draw layout; two textures drawn in two passes.
        constexpr uintptr_t kUniParticleShapeOffset = 0x00; // one byte, 0 draws a sprite
        constexpr uintptr_t kUniParticleTextureBOffset = 0x04;
        constexpr uintptr_t kUniParticleTextureAOffset = 0x08;
        constexpr uintptr_t kUniParticlePositionOffset = 0x0C; // three floats
        constexpr uintptr_t kUniParticleCornerOffset = 0x18; // three floats, the quad's far corner
        constexpr uintptr_t kUniParticleSizeOffset = 0x24;

        // Layer colours: 0x40 with tex A, 0x30 with tex B; SpawnTrail sets alpha = life.
        constexpr uintptr_t kUniParticleColourAOffset = 0x40;
        constexpr uintptr_t kUniParticleColourBOffset = 0x30;

        constexpr uintptr_t kUniParticleAngleAOffset = 0x2C;
        constexpr uintptr_t kUniParticleAngleBOffset = 0x28;
        constexpr uintptr_t kUniParticleQuadAngleOffset = 0x50;

        // Unused uni fields: 0x54, 0x58, 0x5C, 0x60, 0x68, 0x74..0xA4, 0xB0, 0xB4.
        constexpr uintptr_t kUniParticleNegSpeedOffset = 0x64;

        // TrailUpdate: life -= decay*dt; free at zero; alpha/fade mirror life.
        constexpr uintptr_t kUniParticleAlphaOffset = 0x3C;
        constexpr uintptr_t kUniParticleFadeOffset = 0x4C;
        constexpr uintptr_t kUniParticleLifeOffset = 0x6C;
        constexpr uintptr_t kUniParticleDecayOffset = 0x70;
        constexpr uintptr_t kUniParticleSpeedOffset = 0xA8;
        constexpr uintptr_t kUniParticleTargetOffset = 0xAC;

        // Callback table slots; read, do not call the functions directly.
        constexpr uintptr_t kParticleAllocKindSlot = 0x00691290; // __cdecl (int kind) -> particle
        constexpr uintptr_t kParticleAllocTrailSlot = 0x0069129C; // __cdecl (void) -> uni particle
        constexpr uintptr_t kParticleAttachUpdateSlot = 0x006912A0; // __cdecl (particle, update)

        // Per-frame live-list walk; pool frees at life==0, uni calls think at +0xB8.
        constexpr uintptr_t kParticleUpdateAll = 0x0041E410; // __cdecl (delta), all four stores
        constexpr uintptr_t kParticleUpdatePool = 0x0041E160; // __thiscall (pool, delta)
        constexpr uintptr_t kParticleUpdateUni = 0x0041E290; // __fastcall (live head)
        // Particle_Step: __stdcall RET 8 (not cdecl -- wrong CC leaks 8 bytes/frame).
        constexpr uintptr_t kParticleStep = 0x0041DE20; // __stdcall (particle, delta)
        constexpr uintptr_t kParticleResetAll = 0x0041E3E0;

        constexpr uintptr_t kParticleAllocKind = 0x0041E460;
        constexpr uintptr_t kParticleAllocFromPool = 0x0041E220; // __fastcall (pool base)
        constexpr uintptr_t kParticleAllocTrail = 0x0041E380;
        constexpr uintptr_t kParticleAttachUpdate = 0x0041E3D0;

        // Think: __cdecl (particle) -> 0 when done; swap +0xB8 to replace behaviour.
        constexpr uintptr_t kParticleTrailUpdate = 0x0044DB80;

        // SpawnText: __cdecl, call site cleans 0x20 (8 args). Prefer this over placing glyphs.
        constexpr uintptr_t kParticleSpawnText = 0x00448970;

        // SpawnTrail + Scene_DrawParticles (__fastcall scene); same seam as camera lens.
        constexpr uintptr_t kParticleSpawnTrail = 0x0044DC20;
        constexpr uintptr_t kSceneDrawParticles = 0x0040AF70; // __fastcall (scene)

        // Frame delta for TrailUpdate; NOT Core.h kFrameDelta (0x0067A5F8).
        constexpr uintptr_t kParticleFrameDelta = 0x005177A4;
    }
}
