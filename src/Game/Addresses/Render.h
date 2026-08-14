#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        constexpr uintptr_t kSystemFont = 0x00503598;
        constexpr uintptr_t kMenuFindFont = 0x00444380; // __cdecl (const char* name) -> font

        // Sound facade @ kSound; Load = vtable+0x18 (0x00402C20)
        // NOT +0x1C (0x00402C40): that RELEASES — passing a name destructs the caller's string.
        constexpr uintptr_t kSound = 0x0067A5F0;
        constexpr uintptr_t kSoundRelease = 0x00402C40;     // __cdecl (id)
        constexpr uintptr_t kSoundPlay = 0x00402C60;        // __cdecl (id, flag)
        constexpr uintptr_t kSoundPlayAt = 0x00402C80;      // __cdecl (id, position, loop, volume) -> voice
        constexpr uintptr_t kSoundStop = 0x00402CE0;        // __cdecl (voice)
        constexpr uintptr_t kSoundMove = 0x00402D00;        // __cdecl (voice, position)

        // Music object: two stream slots (second at +0x114). Empty stream (+0x110 null) gets the
        // new track; name at +0x0D. PlayFile is __thiscall (ECX = kMusic); StopAll is __cdecl ().
        constexpr uintptr_t kMusic = 0x005028A8;
        constexpr uintptr_t kMusicPlayFile = 0x004028C0;    // __thiscall (music, path, fade, loop)
        constexpr uintptr_t kMusicStopAll = 0x00402620;     // __cdecl ()
        constexpr uintptr_t kMusicSlotStride = 0x114;
        constexpr uintptr_t kMusicNameOffset = 0x0D;
        constexpr uintptr_t kMusicStreamOffset = 0x110;

        constexpr uintptr_t kLightColourOffset = 0x00; // three floats, r g b
        constexpr uintptr_t kLightAlphaOffset = 0x0C; // one
        constexpr uintptr_t kLightRangeOffset = 0x10;
        constexpr uintptr_t kLightPositionOffset = 0x14; // x, y, and a z that is always zero
        constexpr uintptr_t kSceneLightsOffset = 0xFA0;
        constexpr uintptr_t kSceneLightCountOffset = 0x13A4;
        constexpr size_t kSceneLightCapacity = 16;

        // Frame lights (not room-carried).
        constexpr uintptr_t kSceneAddLight = 0x00409440; // __thiscall (scene, position, colour, range)
        constexpr uintptr_t kSceneDynamicLightsOffset = 0x11A0;
        constexpr uintptr_t kSceneDynamicLightCountOffset = 0x13A0;
        constexpr size_t kSceneDynamicLightCapacity = 15;

        // Scene_AddLight also writes triangles into the scene; only from the sort path.
        constexpr uintptr_t kSceneLightVertexBufferOffset = 0x14F8;
        constexpr uintptr_t kSceneLightVerticesOffset = 0x1500;
        constexpr uintptr_t kSceneLightVertexCountOffset = 0x14FC;
        constexpr size_t kSceneLightVertexCapacity = 0x3FE;

        // The room the scene is showing
        constexpr uintptr_t kSceneRoomOffset = 0x14AC;

        constexpr uintptr_t kSceneLightFlickerOffset = 0x1510;
        constexpr uintptr_t kRoomLoadedOffset = 0x2674;
        constexpr uintptr_t kRoomSpawnListOffset = 0x2678;
        constexpr uintptr_t kRoomSpawnCountOffset = 0x267C;
        constexpr uintptr_t kRoomEntitiesOffset = 0x2680;

        // Plays one at a place, __cdecl (id, const float position[3], 0, volume, speed)
        constexpr uintptr_t kPlayEffectSlot = 0x00691354;

        // The resolvers the parsers go through, __cdecl (name) -> id, 0 when there is no such resource
        constexpr uintptr_t kResolveModelSlot = 0x006912B8;
        constexpr uintptr_t kResolveTextureSlot = 0x006912A4;
        constexpr uintptr_t kResolveSoundSlot = 0x00691344;

        constexpr uintptr_t kModelManager = 0x00503520;
        constexpr uintptr_t kModelListHead = 0x00503524;
        constexpr uintptr_t kMeshManager = 0x00503528;

        // The renderer, a pointer
        constexpr uintptr_t kRenderDevice = 0x00503590;
        constexpr uintptr_t kRenderDeviceSetTexture = 0x28;
        constexpr uintptr_t kRenderDeviceMultiplyWorld = 0x58; // world = given * world
        constexpr uintptr_t kRenderDeviceReleaseVertices = 0x74;
        constexpr uintptr_t kRenderDeviceDrawVertices = 0x78;

        constexpr uintptr_t kLoadOrFindModel = 0x00406B80; // __cdecl (path) -> model
        constexpr uintptr_t kModelRelease = 0x00406300; // __cdecl (model)
        constexpr uintptr_t kModelCreateFromMesh = 0x004068B0; // __cdecl (0, mesh) -> model
        constexpr uintptr_t kModelBuildFromMesh = 0x004065C0; // __stdcall (model) -> bool, RET 4
        constexpr uintptr_t kModelRender = 0x004061E0; // __cdecl (model)

        constexpr uintptr_t kModelDrawNode = 0x00408470; // __stdcall (node), RET 4
        constexpr uintptr_t kSceneDrawObjects = 0x0040AC20; // __thiscall (scene)
        constexpr uintptr_t kSceneObjectListsOffset = 0xE53C; // four heads, linked by node+0x64

        constexpr uintptr_t kDrawNodeFlagsOffset = 0x00; // 2 a second stage, 0x10 animated
        constexpr uintptr_t kDrawNodeModelOffset = 0x04;
        constexpr uintptr_t kDrawNodeTextureOffset = 0x08;
        constexpr uintptr_t kDrawNodeMatrixOffset = 0x0C; // 4x4 floats, model -> world
        constexpr uintptr_t kDrawNodeFrameAOffset = 0x4C;
        constexpr uintptr_t kDrawNodeFrameBOffset = 0x50;
        constexpr uintptr_t kDrawNodeBlendOffset = 0x54;
        constexpr uintptr_t kDrawNodeSecondTextureOffset = 0x58;
        constexpr uintptr_t kDrawNodeKindOffset = 0x60;
        constexpr uintptr_t kDrawNodeNextOffset = 0x64;
        constexpr uint8_t kDrawNodeIsAnimated = 0x10;
        constexpr uintptr_t kModelRenderAnimated = 0x00406760; // __cdecl (model, from, to, blend)
        constexpr uintptr_t kMeshCreate = 0x00406CF0; // __cdecl () -> mesh, 0x34 zeroed
        constexpr uintptr_t kMeshFree = 0x00406C60; // __cdecl (mesh)
        constexpr uintptr_t kMeshLoadFile = 0x004075D0; // __cdecl (path) -> mesh
        constexpr uintptr_t kMeshComputeCenter = 0x00406FB0; // __stdcall (mesh, frame), RET 8
        constexpr uintptr_t kMeshComputeExtents = 0x00407040; // __stdcall (mesh, frame), RET 8
        constexpr uintptr_t kMeshMakeWorkingCopy = 0x00406E00; // __stdcall (mesh) -> bool, RET 4

        constexpr size_t kModelSize = 0x13C;
        constexpr uintptr_t kModelMagicOffset = 0x000;
        constexpr uint32_t kModelMagic = 0x2222;
        constexpr uintptr_t kModelNameOffset = 0x004; // the path it was loaded by
        constexpr size_t kModelNameLength = 0x100;
        constexpr uintptr_t kModelKindOffset = 0x104;
        constexpr uintptr_t kModelMeshOffset = 0x108; // kept for kind 0 and kind 2
        constexpr uintptr_t kModelVertexBufferOffset = 0x10C;
        constexpr uintptr_t kModelVertexCountOffset = 0x110;
        constexpr uintptr_t kModelPrimitiveCountOffset = 0x114;
        constexpr uintptr_t kModelCentreOffset = 0x118; // three floats
        constexpr uintptr_t kModelReferencesOffset = 0x124; // extra owners, not owners
        constexpr uintptr_t kModelExtentsOffset = 0x128; // three floats
        constexpr uintptr_t kModelPreviousOffset = 0x134;
        constexpr uintptr_t kModelNextOffset = 0x138;

        constexpr int kModelKindMesh = 0;
        constexpr int kModelKindStatic = 1;
        constexpr int kModelKindAnimated = 2;

        // interpolated into before it is drawn.
        constexpr size_t kMeshSize = 0x34;
        constexpr uintptr_t kMeshFramesOffset = 0x14;
        constexpr uintptr_t kMeshFrameCountOffset = 0x18;
        constexpr uintptr_t kMeshCentreOffset = 0x1C;
        constexpr uintptr_t kMeshExtentsOffset = 0x28;

        constexpr size_t kMeshFrameStride = 0x14;
        constexpr uintptr_t kMeshFrameVerticesOffset = 0x00;
        constexpr uintptr_t kMeshFrameVertexCountOffset = 0x04;
        constexpr uintptr_t kMeshFrameIndicesOffset = 0x08;
        constexpr uintptr_t kMeshFrameIndexCountOffset = 0x0C;
        constexpr uintptr_t kMeshFramePrimitiveCountOffset = 0x10;

        // A .mmd vertex is x y z nx ny nz u v, all floats; an index is a uint16.
        constexpr size_t kMeshVertexSize = 0x20;

        constexpr uintptr_t kMatrixPerspective = 0x004080C0; // __thiscall (m, fov, aspect, n, f)
        constexpr uintptr_t kSceneSortObjects = 0x0040A640;
        constexpr uintptr_t kSceneNodePoolOffset = 0x1538;
        constexpr size_t kSceneNodeStride = 0x68;
        constexpr uintptr_t kSceneNodeCountOffset = 0xE538;
        constexpr size_t kSceneNodeCapacity = 0x200;

        constexpr uintptr_t kScene = 0x00505AA8;
        constexpr uintptr_t kSceneDrawListOffset = 0x00004; // items, stride 0x7C
        constexpr uintptr_t kSceneDrawCountOffset = 0x00F84;
        constexpr size_t kSceneDrawCapacity = 0x20;
        constexpr uintptr_t kSceneViewOffset = 0x111A8; // both are 4x4 floats
        constexpr uintptr_t kSceneProjectionOffset = 0x111E8;

        constexpr uintptr_t kSceneAddModel = 0x0040CD50; // __cdecl (item)
        constexpr uintptr_t kSceneDrawModels = 0x0040A0A0; // __thiscall (scene)

        constexpr size_t kDrawItemSize = 0x7C;
        constexpr uintptr_t kDrawItemFlagsOffset = 0x00;
        constexpr uintptr_t kDrawItemModelOffset = 0x04; // refused when null
        constexpr uintptr_t kDrawItemTextureOffset = 0x08;
        constexpr uintptr_t kDrawItemPositionOffset = 0x0C; // three floats
        constexpr uintptr_t kDrawItemAngleXOffset = 0x18; // radians, applied last
        constexpr uintptr_t kDrawItemAngleYOffset = 0x1C;
        constexpr uintptr_t kDrawItemAngleZOffset = 0x20; // applied first
        constexpr uintptr_t kDrawItemFrameOffset = 0x24; // 0 draws the model still
        constexpr uintptr_t kDrawItemMatrixOffset = 0x2C;
        constexpr uintptr_t kDrawItemAxisOffset = 0x6C;
        constexpr uintptr_t kDrawItemAxisAngleOffset = 0x78;

        constexpr uint8_t kDrawItemTurnsAboutAxis = 0x01;
        constexpr uint8_t kDrawItemHasMatrix = 0x02;

        // reference count. CAnimManager is the static object at 0x005176E8.
        constexpr uintptr_t kAnimManager = 0x005176E8;
        constexpr uintptr_t kAnimListHead = 0x005176EC;
        constexpr uintptr_t kLoadOrFindAnim = 0x004146B0; // __cdecl (path) -> animation

        constexpr uintptr_t kResolveAnimSlot = 0x0069136C; // = LoadOrFindAnim
        constexpr uintptr_t kAnimPlaySlot = 0x00691370; // = kAnimPlay
        constexpr uintptr_t kAnimIsPlayingSlot = 0x00691374; // = kAnimIsPlaying
        constexpr uintptr_t kAnimReleaseSlot = 0x00691378; // = kAnimRelease

        // __cdecl (animation, frameset, state, position, flags), five arguments
        constexpr uintptr_t kAnimPlay = 0x004148F0;
        // __cdecl (animation, frameset, state) -> bool
        constexpr uintptr_t kAnimIsPlaying = 0x00414A50;
        constexpr uintptr_t kAnimRelease = 0x00414710; // __cdecl (animation)
        constexpr uint32_t kAnimMagic = 0x5555; // at animation+0

        constexpr uint32_t kAnimFlagStopped = 0x4;
        constexpr uint32_t kAnimFlagFinished = 0x8;

        // Frame A/B + blend = drawn pose.
        constexpr uintptr_t kAnimStateAnimationOffset = 0x04;
        constexpr uintptr_t kAnimStateFramesetOffset = 0x08; // the frameset record, or null
        constexpr uintptr_t kAnimStatePositionOffset = 0x2C; // three floats, for the sounds
        constexpr uintptr_t kAnimStateFlagsOffset = 0x40;
        constexpr uintptr_t kAnimStateSpeedOffset = 0x44;
        constexpr uintptr_t kAnimStateDirectionOffset = 0x48; // below zero runs the range down
        constexpr uintptr_t kAnimStateFrameAOffset = 0x4C;
        constexpr uintptr_t kAnimStateFrameBOffset = 0x50;
        constexpr uintptr_t kAnimStateBlendOffset = 0x54; // 0..1 from A to B

        constexpr uintptr_t kAnimNameOffset = 0x004;
        constexpr size_t kAnimNameLength = 0x100;
        constexpr uintptr_t kAnimFramesetsOffset = 0x104;
        constexpr uintptr_t kAnimFramesetCountOffset = 0x108;
        constexpr uintptr_t kAnimBoxOffset = 0x10C; // $anim_bbox
        constexpr uintptr_t kAnimBoxEntityOffset = 0x110; // $anim_bbox_entity

        // Both point at an AnimBox, 0x24, built by AnimBox_Create 0x00414330
        constexpr uintptr_t kAnimBoxParse = 0x00414040;
        constexpr uintptr_t kAnimBoxSample = 0x00414250; // (frameA, frameB, blend, animBox, out)
        constexpr uintptr_t kEntityFrameBoxOffset = 0x13C; // where that result lands
        constexpr size_t kAnimBoxSize = 0x24;
        constexpr uintptr_t kAnimBoxDefaultOffset = 0x00; // $default_bbox
        constexpr uintptr_t kAnimBoxFramesOffset = 0x1C;
        constexpr uintptr_t kAnimBoxFrameCountOffset = 0x20;

        constexpr size_t kAnimBoxFrameStride = 0x20;
        constexpr uintptr_t kAnimBoxFrameUsedOffset = 0x00; // that frame has one of its own
        constexpr uintptr_t kAnimBoxFrameBoxOffset = 0x04;
        constexpr uintptr_t kAnimNextOffset = 0x118;
        constexpr uintptr_t kAnimReferencesOffset = 0x11C;

        // One $frameset block of the .anm, as CAnimBox::Parse reads it.
        constexpr size_t kFramesetStride = 0xB8;
        constexpr uintptr_t kFramesetNameOffset = 0x00; // 0x40
        constexpr uintptr_t kFramesetFpsOffset = 0x40;
        constexpr uintptr_t kFramesetDirectionOffset = 0x44; // 1.0 up the range, -1.0 down
        constexpr uintptr_t kFramesetStartFrameOffset = 0x48; // $startframe, clamped to the range
        constexpr uintptr_t kFramesetFromOffset = 0x4C; // $range
        constexpr uintptr_t kFramesetToOffset = 0x50;
        constexpr uintptr_t kFramesetLoopOffset = 0x54;
        constexpr uintptr_t kFramesetBreakOffset = 0x58; // forced to 1 when it loops
        constexpr uintptr_t kFramesetFollowOffset = 0x60; // 0x40, what plays after it

        // Texture list: names, refcount, magic.
        constexpr uintptr_t kTextureManager = 0x00695D38; // its first dword is the list head
        constexpr uintptr_t kTextureListHead = 0x00695D38;
        constexpr uintptr_t kLoadOrFindTexture = 0x0047C200; // __thiscall (mgr, name, mip, alpha)
        constexpr uintptr_t kFindTextureInList = 0x0047BDB0; // matches all three, not just name
        constexpr uintptr_t kTextureRelease = 0x0047C400; // __thiscall (mgr, texture)
        constexpr uintptr_t kTextureUnlink = 0x0047BFB0; // __thiscall (mgr, texture)
        constexpr uintptr_t kTextureConstruct = 0x0047BFE0; // __fastcall (texture)

        constexpr uintptr_t kTextureBuild = 0x0047C000; // __stdcall (texture) -> bool, RET 4
        constexpr uintptr_t kTextureLoadFromFile = 0x0047BC90; // when it has a name
        constexpr uintptr_t kTextureFromPixels = 0x0047B950; // when it has pixels instead

        constexpr size_t kTextureSize = 0x130;
        constexpr uintptr_t kTextureNextOffset = 0x004;
        constexpr uintptr_t kTexturePreviousOffset = 0x008;
        constexpr uintptr_t kTextureMagicOffset = 0x00C;
        constexpr uint32_t kTextureMagic = 0x1111;

        // Pixel buffer must come from the game allocator.
        constexpr uintptr_t kTextureFromPixelsOffset = 0x010;
        constexpr uintptr_t kTextureNameOffset = 0x011; // what Package_OpenFile is given
        constexpr uintptr_t kTexturePixelsOffset = 0x114; // width*height, 4 bytes each, RGBx
        constexpr uintptr_t kTextureAlphaOffset = 0x118; // picks one of two D3DFORMATs
        constexpr uintptr_t kTextureSurfaceOffset = 0x11C; // IDirect3DTexture8*
        constexpr uintptr_t kTextureMipOffset = 0x120; // zero: one level, else a chain
        constexpr uintptr_t kTextureWidthOffset = 0x124;
        constexpr uintptr_t kTextureHeightOffset = 0x128;
        constexpr uintptr_t kTextureReferencesOffset = 0x12C;

        constexpr uintptr_t kD3dDevice = 0x00695C74;
        constexpr uintptr_t kTextureFormatAlpha = 0x00695D3C;
        constexpr uintptr_t kTextureFormatPlain = 0x00695D40;
    }
}
