#include <windows.h>

#include "src/Api/CameraApi.h"
#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Api/WorldApi.h"
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
        AmodCameraApi g_table = {};

        // Camera_Update and the rest are __thiscall; fastcall reads ECX without touching it.
        typedef void(__fastcall* CameraUpdateRoutine)(void* camera, void* unused, float delta);
        typedef void(__fastcall* CameraPointRoutine)(void* camera, void* unused, const float* point);
        typedef void(__fastcall* CameraEffectRoutine)(void* camera, void* unused, int effect);
        typedef void(__fastcall* CameraPlainRoutine)(void* camera, void* unused);
        typedef void(__fastcall* CameraParseRoutine)(void* camera, void* unused, const char* path);
        typedef void(__fastcall* PlayerFocusRoutine)(void* session, void* unused, float* out);

        typedef void(__cdecl* SceneSetViewRoutine)(const float* eye, const float* target, const float* up);
        typedef void(__fastcall* MatrixPerspectiveRoutine)(float* matrix, void* unused, float fov, float aspect, float nearPlane, float farPlane);

        constexpr size_t kFieldCountMax = 14;

        struct Driving
        {
            ModContext* owner;
            AmodCameraDriver driver;
            AmodCameraUpdateHandler handler;
            void* user;
        };

        struct Viewing
        {
            ModContext* owner;
            AmodCameraViewHandler handler;
            void* user;
        };

        struct Watching
        {
            ModContext* owner;
            AmodCameraEventHandler handler;
            void* user;
        };

        // What a mod wrote that the game would not put back on its own
        struct Snapshot
        {
            ModContext* owner;
            uint8_t follows[2];
            float roll;
            uint32_t settings[kFieldCountMax]; // bits, so an int setting survives the round trip
        };

        Driving g_driving = {};
        Viewing g_viewing = {};
        Watching g_watching = {};
        Snapshot g_snapshot = {};

        InlineHook g_updateHook;
        CameraUpdateRoutine g_gameUpdate = nullptr;
        bool g_updateHookTried = false;

        InlineHook g_placeHook;
        CameraPointRoutine g_gamePlace = nullptr;
        bool g_placeHookTried = false;

        InlineHook g_lensHook;
        MatrixPerspectiveRoutine g_gamePerspective = nullptr;
        bool g_lensHookTried = false;

        // Below zero is "the game's"; the world's call is the only one told apart by its matrix.
        AmodCameraLens g_lens = {};
        AmodCameraLens g_lastLens = {};

        SceneSetViewRoutine g_gameSetView = nullptr;
        AmodCameraView g_lastView = {};

        const ApiProperty::Field kFields[] =
        {
            { "room_dist",      GameAddress::kCameraRoomDistOffset,     ApiProperty::Kind::Float, 0, 0, 0, true },
            { "lag_dist",       GameAddress::kCameraLagDistOffset,      ApiProperty::Kind::Float, 0, 0, 0, true },
            { "follow_speed",   GameAddress::kCameraFollowSpeedOffset,  ApiProperty::Kind::Float, 0, 0, 0, true },
            { "center_factor",  GameAddress::kCameraCenterFactorOffset, ApiProperty::Kind::Float, 0, 0, 0, true },
            { "rock_amp",       GameAddress::kCameraShakeAmpOffset,     ApiProperty::Kind::Float, 0, 0, 0, true },
            { "rock_speed",     GameAddress::kCameraShakeSpeedOffset,   ApiProperty::Kind::Float, 0, 0, 0, true },
            { "zoom_time_in",   GameAddress::kCameraZoomTimeInOffset,   ApiProperty::Kind::Float, 0, 0, 0, true },
            { "zoom_time_out",  GameAddress::kCameraZoomTimeOutOffset,  ApiProperty::Kind::Float, 0, 0, 0, true },
            { "zoom_min_dist",  GameAddress::kCameraZoomMinDistOffset,  ApiProperty::Kind::Float, 0, 0, 0, true },
            { "rotate_time",    GameAddress::kCameraRotateTimeOffset,   ApiProperty::Kind::Float, 0, 0, 0, true },
            { "rotate_pause",   GameAddress::kCameraRotatePauseOffset,  ApiProperty::Kind::Float, 0, 0, 0, true },
            { "rock_angle",     GameAddress::kCameraRockAngleOffset,    ApiProperty::Kind::Float, 0, 0, 0, true },
            { "rock_time",      GameAddress::kCameraRockTimeOffset,     ApiProperty::Kind::Float, 0, 0, 0, true },
            { "rock_count",     GameAddress::kCameraRockCountOffset,    ApiProperty::Kind::Int,   0, 0, 0, true },
        };

        constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);
        static_assert(kFieldCount == kFieldCountMax, "the snapshot holds one number per setting");

        uint8_t* Camera()
        {
            return static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kCamera));
        }

        uint8_t* LiveCamera()
        {
            return WorldApi::IsLevelLoaded() ? Camera() : nullptr;
        }

        void ReadTriple(const uint8_t* camera, uintptr_t offset, float* out)
        {
            memcpy(out, camera + offset, sizeof(float) * 3);
        }

        void WriteTriple(uint8_t* camera, uintptr_t offset, const float* in)
        {
            memcpy(camera + offset, in, sizeof(float) * 3);
        }

        void FillFrame(const uint8_t* camera, float delta, AmodCameraFrame& frame)
        {
            frame.structSize = sizeof(AmodCameraFrame);
            frame.delta = delta;
            ReadTriple(camera, GameAddress::kCameraEyeOffset, frame.eye);
            ReadTriple(camera, GameAddress::kCameraTargetOffset, frame.target);
            ReadTriple(camera, GameAddress::kCameraUpOffset, frame.up);
            frame.roll = Read<float>(camera, GameAddress::kCameraRollOffset);
            ReadTriple(camera, GameAddress::kCameraFollowedPointOffset, frame.followed);
        }

        void __fastcall UpdateDetour(void* camera, void* unused, float delta)
        {
            const Driving claim = g_driving;
            const bool mine = claim.owner != nullptr;

            const bool runGame = !mine || claim.driver == AMOD_CAMERA_DRIVER_GAME || claim.driver == AMOD_CAMERA_DRIVER_BOTH;

            if (runGame && g_gameUpdate != nullptr)
            {
                g_gameUpdate(camera, unused, delta);
            }

            const bool runMod = mine && claim.handler != nullptr &&
                (claim.driver == AMOD_CAMERA_DRIVER_MOD || claim.driver == AMOD_CAMERA_DRIVER_BOTH);

            if (runMod)
            {
                AmodCameraFrame frame = {};
                FillFrame(static_cast<const uint8_t*>(camera), delta, frame);
                claim.handler(&frame, claim.user);
            }
        }

        void __cdecl SetViewThunk(const float* eye, const float* target, const float* up)
        {
            AmodCameraView view = {};
            view.structSize = sizeof(AmodCameraView);
            memcpy(view.eye, eye, sizeof(view.eye));
            memcpy(view.target, target, sizeof(view.target));
            memcpy(view.up, up, sizeof(view.up));

            const Viewing claim = g_viewing;
            if (claim.owner != nullptr && claim.handler != nullptr && claim.handler(&view, claim.user) == 0)
            {
                // It looked and did not take the frame; whatever it wrote is not drawn.
                memcpy(view.eye, eye, sizeof(view.eye));
                memcpy(view.target, target, sizeof(view.target));
                memcpy(view.up, up, sizeof(view.up));
            }

            g_lastView = view;

            if (g_gameSetView != nullptr)
            {
                g_gameSetView(view.eye, view.target, view.up);
            }
        }

        void HoldViewSlot()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kSceneSetViewSlot));
            if (slot == nullptr || *slot == reinterpret_cast<void*>(&SetViewThunk))
            {
                return;
            }

            if (*slot != nullptr)
            {
                g_gameSetView = reinterpret_cast<SceneSetViewRoutine>(*slot);
            }
            *slot = reinterpret_cast<void*>(&SetViewThunk);
        }

        bool HoldUpdateHook()
        {
            if (g_updateHookTried)
            {
                return g_updateHook.IsInstalled();
            }
            g_updateHookTried = true;

            void* const target = GameModule::Resolve(GameAddress::kCameraUpdate);
            std::string mismatch;

            if (target == nullptr || !GameModule::VerifyBytes(target, kCameraUpdatePrologue, sizeof(kCameraUpdatePrologue), mismatch))
            {
                FORGE_LOG_ERROR("Camera_Update does not open as expected (%s)", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_updateHook.Install(target, reinterpret_cast<void*>(&UpdateDetour), kCameraUpdatePrologue, sizeof(kCameraUpdatePrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Camera_Update: %s", error.c_str());
                return false;
            }

            g_gameUpdate = reinterpret_cast<CameraUpdateRoutine>(g_updateHook.Trampoline());
            return true;
        }

        void __fastcall PlaceDetour(void* camera, void* unused, const float* point)
        {
            const Watching claim = g_watching;

            if (claim.owner != nullptr && claim.handler != nullptr)
            {
                AmodCameraEventInfo info = {};
                info.structSize = sizeof(info);
                info.event = AMOD_CAMERA_EVENT_PLACING;
                memcpy(info.point, point, sizeof(info.point));

                if (claim.handler(&info, claim.user) != 0)
                {
                    return;
                }
            }

            if (g_gamePlace != nullptr)
            {
                g_gamePlace(camera, unused, point);
            }
        }

        bool HoldPlaceHook()
        {
            if (g_placeHookTried)
            {
                return g_placeHook.IsInstalled();
            }
            g_placeHookTried = true;

            void* const target = GameModule::Resolve(GameAddress::kCameraPlace);
            std::string mismatch;

            if (target == nullptr || !GameModule::VerifyBytes(target, kCameraPlacePrologue, sizeof(kCameraPlacePrologue), mismatch))
            {
                FORGE_LOG_ERROR("Camera_Place does not open as expected (%s)", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_placeHook.Install(target, reinterpret_cast<void*>(&PlaceDetour), kCameraPlacePrologue, sizeof(kCameraPlacePrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Camera_Place: %s", error.c_str());
                return false;
            }

            g_gamePlace = reinterpret_cast<CameraPointRoutine>(g_placeHook.Trampoline());
            return true;
        }

        void __fastcall PerspectiveDetour(float* matrix, void* unused, float fov, float aspect, float nearPlane, float farPlane)
        {
            uint8_t* const scene = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScene));
            const bool world = scene != nullptr &&
                matrix == reinterpret_cast<float*>(scene + GameAddress::kSceneProjectionOffset);

            if (world)
            {
                g_lastLens.structSize = sizeof(g_lastLens);
                g_lastLens.fov = g_lens.fov > 0.0f ? g_lens.fov : fov;
                g_lastLens.aspect = g_lens.aspect > 0.0f ? g_lens.aspect : aspect;
                g_lastLens.nearPlane = g_lens.nearPlane > 0.0f ? g_lens.nearPlane : nearPlane;
                g_lastLens.farPlane = g_lens.farPlane > 0.0f ? g_lens.farPlane : farPlane;

                fov = g_lastLens.fov;
                aspect = g_lastLens.aspect;
                nearPlane = g_lastLens.nearPlane;
                farPlane = g_lastLens.farPlane;
            }

            if (g_gamePerspective != nullptr)
            {
                g_gamePerspective(matrix, unused, fov, aspect, nearPlane, farPlane);
            }
        }

        bool HoldLensHook()
        {
            if (g_lensHookTried)
            {
                return g_lensHook.IsInstalled();
            }
            g_lensHookTried = true;

            void* const target = GameModule::Resolve(GameAddress::kMatrixPerspective);
            std::string mismatch;

            if (target == nullptr ||
                !GameModule::VerifyBytes(target, kMatrixPerspectivePrologue, sizeof(kMatrixPerspectivePrologue), mismatch))
            {
                FORGE_LOG_ERROR("Matrix_Perspective does not open as expected (%s)", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_lensHook.Install(target, reinterpret_cast<void*>(&PerspectiveDetour),
                                    kMatrixPerspectivePrologue, sizeof(kMatrixPerspectivePrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Matrix_Perspective: %s", error.c_str());
                return false;
            }

            g_gamePerspective = reinterpret_cast<MatrixPerspectiveRoutine>(g_lensHook.Trampoline());
            return true;
        }

        // Taken once, the first time a mod writes something the game will not put back itself.
        void KeepCamera(ModContext* owner)
        {
            const uint8_t* const camera = Camera();

            if (camera == nullptr || g_snapshot.owner != nullptr)
            {
                return;
            }

            g_snapshot.owner = owner;
            g_snapshot.follows[0] = Read<uint8_t>(camera, GameAddress::kCameraFollowEyeOffset);
            g_snapshot.follows[1] = Read<uint8_t>(camera, GameAddress::kCameraFollowTargetOffset);
            g_snapshot.roll = Read<float>(camera, GameAddress::kCameraRollOffset);

            for (size_t at = 0; at < kFieldCount; ++at)
            {
                g_snapshot.settings[at] = Read<uint32_t>(camera, kFields[at].offset);
            }
        }

        void RestoreCamera(ModContext* owner)
        {
            uint8_t* const camera = Camera();

            if (camera == nullptr || g_snapshot.owner == nullptr || g_snapshot.owner != owner)
            {
                return;
            }

            memcpy(camera + GameAddress::kCameraFollowEyeOffset, &g_snapshot.follows[0], 1);
            memcpy(camera + GameAddress::kCameraFollowTargetOffset, &g_snapshot.follows[1], 1);
            memcpy(camera + GameAddress::kCameraRollOffset, &g_snapshot.roll, sizeof(float));

            for (size_t at = 0; at < kFieldCount; ++at)
            {
                memcpy(camera + kFields[at].offset, &g_snapshot.settings[at], sizeof(uint32_t));
            }

            g_snapshot = Snapshot{};
        }

        // Camera_ParseScript throws when the file will not read; keep the throw off C++ frames.
        bool ParseGuarded(CameraParseRoutine parse, void* camera, const char* path)
        {
            __try
            {
                parse(camera, nullptr, path);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        int __cdecl ApiIsHere(AmodMod mod)
        {
            return (Caller(mod) != nullptr && LiveCamera() != nullptr) ? 1 : 0;
        }

        int __cdecl ApiGet(AmodMod mod, AmodCamera* out)
        {
            const uint8_t* const camera = Caller(mod) != nullptr ? LiveCamera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera right now");
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Get was given nowhere to put it");
            }

            AmodCamera filled = {};
            filled.structSize = sizeof(AmodCamera);
            ReadTriple(camera, GameAddress::kCameraEyeOffset, filled.eye);
            ReadTriple(camera, GameAddress::kCameraTargetOffset, filled.target);
            ReadTriple(camera, GameAddress::kCameraUpOffset, filled.up);
            filled.roll = Read<float>(camera, GameAddress::kCameraRollOffset);
            ReadTriple(camera, GameAddress::kCameraFollowedPointOffset, filled.followed);
            filled.effect = Read<int>(camera, GameAddress::kCameraEffectOffset);
            filled.isShaking = Read<uint8_t>(camera, GameAddress::kCameraShakeRunningOffset) != 0 ? 1 : 0;
            filled.followsEye = Read<uint8_t>(camera, GameAddress::kCameraFollowEyeOffset) != 0 ? 1 : 0;
            filled.followsTarget = Read<uint8_t>(camera, GameAddress::kCameraFollowTargetOffset) != 0 ? 1 : 0;

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int SetTripleField(AmodMod mod, uintptr_t offset, const float* value, const char* what)
        {
            uint8_t* const camera = Caller(mod) != nullptr ? LiveCamera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera right now");
            }
            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%s was given nothing to write", what);
            }

            WriteTriple(camera, offset, value);
            return 1;
        }

        int __cdecl ApiSetEye(AmodMod mod, const float* position)
        {
            return SetTripleField(mod, GameAddress::kCameraEyeOffset, position, "SetEye");
        }

        int __cdecl ApiSetTarget(AmodMod mod, const float* position)
        {
            return SetTripleField(mod, GameAddress::kCameraTargetOffset, position, "SetTarget");
        }

        int __cdecl ApiSetUp(AmodMod mod, const float* direction)
        {
            return SetTripleField(mod, GameAddress::kCameraUpOffset, direction, "SetUp");
        }

        int __cdecl ApiSetRoll(AmodMod mod, float radians)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const camera = caller != nullptr ? LiveCamera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera to roll");
            }

            KeepCamera(caller);

            memcpy(camera + GameAddress::kCameraRollOffset, &radians, sizeof(radians));
            return 1;
        }

        int CallPoint(AmodMod mod, uintptr_t routine, const float* point, const char* what)
        {
            uint8_t* const camera = Caller(mod) != nullptr ? LiveCamera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera right now");
            }
            if (point == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%s was given no point", what);
            }

            const CameraPointRoutine call = reinterpret_cast<CameraPointRoutine>(GameModule::Resolve(routine));
            if (call == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "%s is not reachable in this build", what);
            }

            call(camera, nullptr, point);
            return 1;
        }

        int __cdecl ApiFollow(AmodMod mod, const float* point)
        {
            return CallPoint(mod, GameAddress::kCameraFollow, point, "Follow");
        }

        int __cdecl ApiPlace(AmodMod mod, const float* point)
        {
            return CallPoint(mod, GameAddress::kCameraPlace, point, "Place");
        }

        int __cdecl ApiGetFocus(AmodMod mod, float* out)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetFocus was given nowhere to put it");
            }
            if (WorldApi::Player() == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to look at");
            }

            void* const session = GameModule::Resolve(GameAddress::kGameSession);
            const PlayerFocusRoutine focus = reinterpret_cast<PlayerFocusRoutine>(GameModule::Resolve(GameAddress::kPlayerGetCameraFocus));

            if (session == nullptr || focus == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the focus is not reachable in this build");
            }

            focus(session, nullptr, out);
            return 1;
        }

        int SetFollowFlag(AmodMod mod, uintptr_t offset, int follows)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const camera = caller != nullptr ? LiveCamera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera right now");
            }

            KeepCamera(caller);

            const uint8_t value = follows != 0 ? 1u : 0u;
            memcpy(camera + offset, &value, sizeof(value));
            return 1;
        }

        int __cdecl ApiSetFollowEye(AmodMod mod, int follows)
        {
            return SetFollowFlag(mod, GameAddress::kCameraFollowEyeOffset, follows);
        }

        int __cdecl ApiSetFollowTarget(AmodMod mod, int follows)
        {
            return SetFollowFlag(mod, GameAddress::kCameraFollowTargetOffset, follows);
        }

        int __cdecl ApiGetProperty(AmodMod mod, const char* key, AmodValue* value)
        {
            const uint8_t* const camera = Caller(mod) != nullptr ? Camera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera to read");
            }
            if (key == nullptr || value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetProperty was given no key or nowhere to put it");
            }

            const ApiProperty::Field* const field = ApiProperty::Find(kFields, kFieldCount, key);
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the camera has no '%s'", key);
            }

            return ApiProperty::Get(camera, *field, value);
        }

        int __cdecl ApiSetProperty(AmodMod mod, const char* key, const AmodValue* value)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const camera = caller != nullptr ? Camera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera to write");
            }

            KeepCamera(caller);
            if (key == nullptr || value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetProperty was given no key or no value");
            }

            const ApiProperty::Field* const field = ApiProperty::Find(kFields, kFieldCount, key);
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the camera has no '%s'", key);
            }

            if (!ApiProperty::Set(camera, *field, *value))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "'%s' does not take that value", key);
            }
            return 1;
        }

        int __cdecl ApiPropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? static_cast<int>(kFieldCount) : 0;
        }

        const char* __cdecl ApiPropertyNameAt(AmodMod mod, int at)
        {
            if (Caller(mod) == nullptr || at < 0 || at >= static_cast<int>(kFieldCount))
            {
                return "";
            }
            return kFields[at].key;
        }

        int __cdecl ApiReload(AmodMod mod)
        {
            uint8_t* const camera = Caller(mod) != nullptr ? Camera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera to reload");
            }

            const CameraParseRoutine parse =
                reinterpret_cast<CameraParseRoutine>(GameModule::Resolve(GameAddress::kCameraParseScript));

            if (parse == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the camera parser is not reachable in this build");
            }

            if (!ParseGuarded(parse, camera, "scripts/camera.txt"))
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "scripts/camera.txt would not read");
            }
            return 1;
        }

        int __cdecl ApiGetEffect(AmodMod mod)
        {
            const uint8_t* const camera = Caller(mod) != nullptr ? LiveCamera() : nullptr;
            return camera != nullptr ? Read<int>(camera, GameAddress::kCameraEffectOffset) : AMOD_CAMERA_EFFECT_NONE;
        }

        int __cdecl ApiStartEffect(AmodMod mod, int effect)
        {
            uint8_t* const camera = Caller(mod) != nullptr ? LiveCamera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera right now");
            }
            if (effect < AMOD_CAMERA_EFFECT_ZOOM || effect > AMOD_CAMERA_EFFECT_ROTATE)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no camera effect %d", effect);
            }

            const CameraEffectRoutine start = reinterpret_cast<CameraEffectRoutine>(GameModule::Resolve(GameAddress::kCameraStartEffect));

            if (start == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the camera effects are not reachable in this build");
            }

            start(camera, nullptr, effect);
            return 1;
        }

        int CallPlain(AmodMod mod, uintptr_t routine, const char* what)
        {
            uint8_t* const camera = Caller(mod) != nullptr ? LiveCamera() : nullptr;

            if (camera == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no camera right now");
            }

            const CameraPlainRoutine call = reinterpret_cast<CameraPlainRoutine>(GameModule::Resolve(routine));
            if (call == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "%s is not reachable in this build", what);
            }

            call(camera, nullptr);
            return 1;
        }

        int __cdecl ApiStopEffect(AmodMod mod)
        {
            return CallPlain(mod, GameAddress::kCameraStopEffect, "StopEffect");
        }

        int __cdecl ApiStartShake(AmodMod mod)
        {
            return CallPlain(mod, GameAddress::kCameraStartShake, "StartShake");
        }

        int __cdecl ApiStopShake(AmodMod mod)
        {
            return CallPlain(mod, GameAddress::kCameraStopShake, "StopShake");
        }

        int __cdecl ApiSetDriver(AmodMod mod, AmodCameraDriver driver)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (driver < AMOD_CAMERA_DRIVER_GAME || driver > AMOD_CAMERA_DRIVER_NOBODY)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no camera driver %d", static_cast<int>(driver));
            }
            if (g_driving.owner != nullptr && g_driving.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is already driving the camera");
            }

            if (driver == AMOD_CAMERA_DRIVER_GAME && g_driving.handler == nullptr)
            {
                g_driving = Driving{};
                return 1;
            }

            if (!HoldUpdateHook())
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the camera update cannot be taken in this build");
            }

            g_driving.owner = caller;
            g_driving.driver = driver;
            return 1;
        }

        AmodCameraDriver __cdecl ApiGetDriver(AmodMod mod)
        {
            const ModContext* const caller = Caller(mod);
            return (caller != nullptr && g_driving.owner == caller) ? g_driving.driver : AMOD_CAMERA_DRIVER_GAME;
        }

        int __cdecl ApiSetUpdateHandler(AmodMod mod, AmodCameraUpdateHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (g_driving.owner != nullptr && g_driving.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is already driving the camera");
            }

            if (handler == nullptr)
            {
                if (g_driving.owner == caller)
                {
                    g_driving = Driving{};
                }
                return 1;
            }

            if (!HoldUpdateHook())
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the camera update cannot be taken in this build");
            }

            g_driving.owner = caller;
            g_driving.handler = handler;
            g_driving.user = user;
            return 1;
        }

        int __cdecl ApiSetViewHandler(AmodMod mod, AmodCameraViewHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (g_viewing.owner != nullptr && g_viewing.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod already has the view");
            }

            if (handler == nullptr)
            {
                g_viewing = Viewing{};
                return 1;
            }

            g_viewing.owner = caller;
            g_viewing.handler = handler;
            g_viewing.user = user;
            return 1;
        }

        int __cdecl ApiGetView(AmodMod mod, AmodCameraView* out)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetView was given nowhere to put it");
            }
            if (g_lastView.structSize == 0)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the world has not been drawn yet");
            }

            return CopyOut(out, out->structSize, &g_lastView, sizeof(g_lastView));
        }

        int __cdecl ApiGetLens(AmodMod mod, AmodCameraLens* out)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetLens was given nowhere to put it");
            }
            if (!HoldLensHook())
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the lens cannot be taken in this build");
            }
            if (g_lastLens.structSize == 0)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the world has not been drawn yet");
            }

            return CopyOut(out, out->structSize, &g_lastLens, sizeof(g_lastLens));
        }

        int __cdecl ApiSetLens(AmodMod mod, const AmodCameraLens* lens)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (lens == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetLens was given no lens");
            }
            if (!HoldLensHook())
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the lens cannot be taken in this build");
            }

            g_lens.structSize = sizeof(g_lens);
            g_lens.fov = lens->fov > 0.0f ? lens->fov : g_lens.fov;
            g_lens.aspect = lens->aspect > 0.0f ? lens->aspect : g_lens.aspect;
            g_lens.nearPlane = lens->nearPlane > 0.0f ? lens->nearPlane : g_lens.nearPlane;
            g_lens.farPlane = lens->farPlane > 0.0f ? lens->farPlane : g_lens.farPlane;
            return 1;
        }

        int __cdecl ApiClearLens(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            g_lens = AmodCameraLens{};
            return 1;
        }

        int __cdecl ApiSetEventHandler(AmodMod mod, AmodCameraEventHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (g_watching.owner != nullptr && g_watching.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is already told about the camera");
            }

            if (handler == nullptr)
            {
                g_watching = Watching{};
                return 1;
            }

            if (!HoldPlaceHook())
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "the camera snap cannot be taken in this build");
            }

            g_watching.owner = caller;
            g_watching.handler = handler;
            g_watching.user = user;
            return 1;
        }

        int __cdecl ApiGetFreeMode(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            const uint8_t* const at = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kCvarCamFree));

            if (at == nullptr)
            {
                return 0;
            }

            const uint8_t* const record = Read<const uint8_t*>(at, 0);
            if (record == nullptr || !Readable(record, GameAddress::kCvarValueOffset + sizeof(float)))
            {
                return 0;
            }

            return static_cast<int>(Read<float>(record, GameAddress::kCvarValueOffset));
        }
    }

    bool CameraApi::Install()
    {
        if (GameModule::Resolve(GameAddress::kCamera) == nullptr)
        {
            FORGE_LOG_ERROR("The camera is not where it was reversed from");
            return false;
        }

        HoldViewSlot();
        FrameApi::AddInternalTick(&HoldViewSlot);

        g_table.structSize = sizeof(AmodCameraApi);
        g_table.IsHere = &ApiIsHere;
        g_table.Get = &ApiGet;
        g_table.SetEye = &ApiSetEye;
        g_table.SetTarget = &ApiSetTarget;
        g_table.SetUp = &ApiSetUp;
        g_table.SetRoll = &ApiSetRoll;
        g_table.Follow = &ApiFollow;
        g_table.Place = &ApiPlace;
        g_table.GetFocus = &ApiGetFocus;
        g_table.SetFollowEye = &ApiSetFollowEye;
        g_table.SetFollowTarget = &ApiSetFollowTarget;
        g_table.GetProperty = &ApiGetProperty;
        g_table.SetProperty = &ApiSetProperty;
        g_table.PropertyCount = &ApiPropertyCount;
        g_table.PropertyNameAt = &ApiPropertyNameAt;
        g_table.Reload = &ApiReload;
        g_table.GetEffect = &ApiGetEffect;
        g_table.StartEffect = &ApiStartEffect;
        g_table.StopEffect = &ApiStopEffect;
        g_table.StartShake = &ApiStartShake;
        g_table.StopShake = &ApiStopShake;
        g_table.SetDriver = &ApiSetDriver;
        g_table.GetDriver = &ApiGetDriver;
        g_table.SetUpdateHandler = &ApiSetUpdateHandler;
        g_table.SetViewHandler = &ApiSetViewHandler;
        g_table.GetView = &ApiGetView;
        g_table.GetLens = &ApiGetLens;
        g_table.SetLens = &ApiSetLens;
        g_table.ClearLens = &ApiClearLens;
        g_table.SetEventHandler = &ApiSetEventHandler;
        g_table.GetFreeMode = &ApiGetFreeMode;

        for (size_t at = 0; at < sizeof(g_table) / sizeof(void*); ++at)
        {
            if (reinterpret_cast<void* const*>(&g_table)[at] == nullptr)
            {
                FORGE_LOG_WARN("The camera table has an empty slot at %u", static_cast<unsigned>(at));
            }
        }

        FORGE_LOG_INFO("Camera interface installed");
        return true;
    }

    void CameraApi::Uninstall()
    {
        g_driving = Driving{};
        g_viewing = Viewing{};

        void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kSceneSetViewSlot));
        if (slot != nullptr && *slot == reinterpret_cast<void*>(&SetViewThunk) && g_gameSetView != nullptr)
        {
            *slot = reinterpret_cast<void*>(g_gameSetView);
        }
        g_gameSetView = nullptr;

        g_updateHook.Uninstall();
        g_gameUpdate = nullptr;
        g_updateHookTried = false;

        g_placeHook.Uninstall();
        g_gamePlace = nullptr;
        g_placeHookTried = false;

        g_lensHook.Uninstall();
        g_gamePerspective = nullptr;
        g_lensHookTried = false;

        g_lens = AmodCameraLens{};
        g_lastLens = AmodCameraLens{};
        g_snapshot = Snapshot{};
        g_watching = Watching{};

        g_lastView = AmodCameraView{};
        g_table = AmodCameraApi{};
    }

    void CameraApi::ForgetMod(ModContext* owner)
    {
        if (g_driving.owner == owner)
        {
            g_driving = Driving{};
        }
        if (g_viewing.owner == owner)
        {
            g_viewing = Viewing{};
        }
        if (g_watching.owner == owner)
        {
            g_watching = Watching{};
        }

        g_lens = AmodCameraLens{};

        RestoreCamera(owner);
    }

    const AmodCameraApi* CameraApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
