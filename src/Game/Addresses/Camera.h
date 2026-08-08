#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        // The one camera, a static object like g_level. CCamera vftable 0x004D1084.
        constexpr uintptr_t kCamera = 0x006946E8;
        constexpr size_t kCameraSize = 0xDC;

        constexpr uintptr_t kCameraEyeOffset = 0x04; // three floats
        constexpr uintptr_t kCameraTargetOffset = 0x10;
        constexpr uintptr_t kCameraUpOffset = 0x1C;
        constexpr uintptr_t kCameraEyeVelocityOffset = 0x28;
        constexpr uintptr_t kCameraTargetVelocityOffset = 0x34;

        // Radians about the view axis. Nonzero rebuilds up from its sine and cosine.
        constexpr uintptr_t kCameraRollOffset = 0x40;

        constexpr uintptr_t kCameraWantedEyeOffset = 0x44;
        constexpr uintptr_t kCameraWantedTargetOffset = 0x50;
        constexpr uintptr_t kCameraFollowedPointOffset = 0x5C;

        // scripts/camera.txt, $camera $level_move
        constexpr uintptr_t kCameraRoomDistOffset = 0x68;
        constexpr uintptr_t kCameraLagDistOffset = 0x6C;
        constexpr uintptr_t kCameraFollowSpeedOffset = 0x70;
        constexpr uintptr_t kCameraCenterFactorOffset = 0x74;

        // $event_rock, the shake that runs alongside whatever else the camera is doing
        constexpr uintptr_t kCameraShakeAmpOffset = 0x78;
        constexpr uintptr_t kCameraShakeSpeedOffset = 0x7C;
        constexpr uintptr_t kCameraShakeRunningOffset = 0x80; // one byte
        constexpr uintptr_t kCameraShakePhaseOffset = 0x84;
        constexpr uintptr_t kCameraShakeDirectionOffset = 0x88; // three floats
        constexpr uintptr_t kCameraUnshakenEyeOffset = 0x94;

        // Both start at 1. Clearing one leaves that end of the camera where it stands.
        constexpr uintptr_t kCameraFollowEyeOffset = 0xA0; // one byte
        constexpr uintptr_t kCameraFollowTargetOffset = 0xA1;

        constexpr uintptr_t kCameraEffectOffset = 0xA4;
        constexpr int kCameraEffectNone = 0;
        constexpr int kCameraEffectZoom = 1;
        constexpr int kCameraEffectRocking = 2;
        constexpr int kCameraEffectRotate = 3;

        // $fx_zoom
        constexpr uintptr_t kCameraZoomTimeInOffset = 0xA8;
        constexpr uintptr_t kCameraZoomTimeOutOffset = 0xAC;
        constexpr uintptr_t kCameraZoomMinDistOffset = 0xB0;
        constexpr uintptr_t kCameraZoomStageOffset = 0xB4; // 0 going in, 1 coming out
        constexpr uintptr_t kCameraZoomTimerOffset = 0xB8;

        // $fx_rotate
        constexpr uintptr_t kCameraRotateTimeOffset = 0xBC;
        constexpr uintptr_t kCameraRotatePauseOffset = 0xC0;
        constexpr uintptr_t kCameraRotateStageOffset = 0xC4;
        constexpr uintptr_t kCameraRotateTimerOffset = 0xC8;

        // $fx_rocking. The angle is stored in radians, the script writes degrees.
        constexpr uintptr_t kCameraRockAngleOffset = 0xCC;
        constexpr uintptr_t kCameraRockTimeOffset = 0xD0;
        constexpr uintptr_t kCameraRockCountOffset = 0xD4;
        constexpr uintptr_t kCameraRockTimerOffset = 0xD8;

        constexpr uintptr_t kCameraConstruct = 0x0044C6E0; // __fastcall (camera)
        constexpr uintptr_t kCameraParseScript = 0x0044C960; // __thiscall (camera, path)
        constexpr uintptr_t kCameraInit = 0x0044D5A0; // __thiscall (camera)

        // __cdecl (eye, wanted, followSpeed, lagDist, dt, velocity) -> bool
        constexpr uintptr_t kCameraStepToward = 0x0044C800;

        // __thiscall (camera, out[3], offset[3]) - offset is right, up, forward
        constexpr uintptr_t kCameraLocalToWorld = 0x0044CDC0;

        constexpr uintptr_t kCameraStartShake = 0x0044CD30; // __fastcall (camera)
        constexpr uintptr_t kCameraStopShake = 0x0044CDB0; // __fastcall (camera)

        // Follow leaves the eye to catch up; Place puts it there this instant.
        constexpr uintptr_t kCameraFollow = 0x0044D0F0; // __thiscall (camera, point[3])
        constexpr uintptr_t kCameraPlace = 0x0044D140; // __thiscall (camera, point[3])

        constexpr uintptr_t kCameraStartEffect = 0x0044D1B0; // __thiscall (camera, effect)
        constexpr uintptr_t kCameraStopEffect = 0x0044D220; // __fastcall (camera)
        constexpr uintptr_t kCameraUpdate = 0x0044D620; // __thiscall (camera, dt)

        // Console variables, each a record whose value is the float at +0x0C.
        constexpr uintptr_t kCvarCamHorz = 0x006946E4;
        constexpr uintptr_t kCvarCamVert = 0x006946E0;
        constexpr uintptr_t kCvarCamDepth = 0x006946D8;
        constexpr uintptr_t kCvarCamFree = 0x006946DC;
        constexpr uintptr_t kCvarValueOffset = 0x0C;

        // cam_free decides the whole of Camera_Update; anything but 0 flies it by hand.
        constexpr float kCamFreeOff = 0.0f;

        // Game_UpdateFrame calls this once a frame with the camera's own eye, target and up.
        constexpr uintptr_t kSceneSetViewSlot = 0x006912EC;
        constexpr uintptr_t kSceneSetView = 0x0040C9B0; // __cdecl (eye[3], target[3], up[3])

        // Where Scene_SetView leaves them, read back by Scene_RenderWorld.
        constexpr uintptr_t kSceneEyeOffset = 0x1514;
        constexpr uintptr_t kSceneUpOffset = 0x1520;
        constexpr uintptr_t kSceneTargetOffset = 0x152C;

        // __thiscall (matrix, eye, target, up); forward is eye - target, so -z is ahead
        constexpr uintptr_t kMatrixLookAt = 0x00408170;

        // Scene_RenderWorld builds projection inline @0x0040BFE7 (device vtable+0x48); no camera slot.
        constexpr uintptr_t kSceneProjectionCall = 0x0040BFE7;
        constexpr uintptr_t kRenderSettings = 0x00503530; // pointer
        constexpr uintptr_t kRenderSettingsFovOffset = 0x0C; // degrees
        constexpr uintptr_t kProjectionAspect = 0x004C86B0; // 1.333
        constexpr uintptr_t kProjectionNear = 0x004C86B4; // 0.1
        constexpr uintptr_t kProjectionFar = 0x004C866C; // 10.0; world ~0.33 tall

        // __thiscall (player, out[3]) - the player's position raised half his box
        constexpr uintptr_t kPlayerGetCameraFocus = 0x004424D0;

        constexpr uintptr_t kGameUpdateFrame = 0x00438BE0; // __cdecl (dt)
    }
}
