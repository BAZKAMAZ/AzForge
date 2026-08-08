#ifndef AZFORGE_CAMERA_API_H
#define AZFORGE_CAMERA_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One effect at a time; shake runs alongside. */
#define AMOD_CAMERA_EFFECT_NONE                                     0
#define AMOD_CAMERA_EFFECT_ZOOM                                     1 /* $fx_zoom */
#define AMOD_CAMERA_EFFECT_ROCKING                                  2 /* $fx_rocking */
#define AMOD_CAMERA_EFFECT_ROTATE                                   3 /* $fx_rotate */

// Snapshot for this frame, not a handle
typedef struct AmodCamera
{
    uint32_t structSize;

    float eye[3];
    float target[3];
    float up[3];

    float roll; /* radians; effects write this */

    float followed[3]; /* last Follow point */

    int effect; /* AMOD_CAMERA_EFFECT_* */
    int isShaking; /* $event_rock running */

    /* Both start set; clear one to freeze that end. */
    int followsEye;
    int followsTarget;

} AmodCamera;

AMOD_LAST_FIELD(AmodCamera, followsTarget, 0x44);

/* who moves the camera each frame */
typedef enum AmodCameraDriver
{
    AMOD_CAMERA_DRIVER_GAME = 0,   /* game Camera_Update */
    AMOD_CAMERA_DRIVER_MOD = 1,    /* handler only */
    AMOD_CAMERA_DRIVER_BOTH = 2,   /* game then handler */
    AMOD_CAMERA_DRIVER_NOBODY = 3  /* neither; stands still */
} AmodCameraDriver;

/* frame at the camera update */
typedef struct AmodCameraFrame
{
    uint32_t structSize;

    float delta; /* delta given to game update */

    float eye[3];
    float target[3];
    float up[3];
    float roll;

    float followed[3];

} AmodCameraFrame;

AMOD_LAST_FIELD(AmodCameraFrame, followed, 0x30);

/* valid only for the call */
typedef void(__cdecl* AmodCameraUpdateHandler)(const AmodCameraFrame* frame, void* user);

// View handed to the renderer
typedef struct AmodCameraView
{
    uint32_t structSize;

    float eye[3];
    float target[3];
    float up[3];

} AmodCameraView;

AMOD_LAST_FIELD(AmodCameraView, up, 0x1C);

/* Edit in place; nonzero = use handler result */
typedef int(__cdecl* AmodCameraViewHandler)(AmodCameraView* view, void* user);

// World projection lens
typedef struct AmodCameraLens
{
    uint32_t structSize;

    float fov; /* radians (settings store degrees) */
    float aspect;
    float nearPlane;
    float farPlane; /* game default 10; player ~0.33 tall */

} AmodCameraLens;

AMOD_LAST_FIELD(AmodCameraLens, farPlane, 0x10);

typedef enum AmodCameraEvent
{
    /* Level/room start; game about to snap camera. */
    AMOD_CAMERA_EVENT_PLACING = 0
} AmodCameraEvent;

typedef struct AmodCameraEventInfo
{
    uint32_t structSize;
    AmodCameraEvent event;

    float point[3]; /* placement point */

} AmodCameraEventInfo;

AMOD_LAST_FIELD(AmodCameraEventInfo, point, 0x08);

/* nonzero refuses; camera stays put */
typedef int(__cdecl* AmodCameraEventHandler)(const AmodCameraEventInfo* info, void* user);

typedef struct AmodCameraApi
{
    uint32_t structSize;

    /* false in menu / between levels / loading */
    int(__cdecl* IsHere)(AmodMod mod);

    /* set out->structSize first */
    int(__cdecl* Get)(AmodMod mod, AmodCamera* out);

    int(__cdecl* SetEye)(AmodMod mod, const float* position);
    int(__cdecl* SetTarget)(AmodMod mod, const float* position);
    int(__cdecl* SetUp)(AmodMod mod, const float* direction);
    int(__cdecl* SetRoll)(AmodMod mod, float radians);

    /* Follow eases at $follow_speed; Place snaps. */
    int(__cdecl* Follow)(AmodMod mod, const float* point);
    int(__cdecl* Place)(AmodMod mod, const float* point);

    /* Player pos + half box height. */
    int(__cdecl* GetFocus)(AmodMod mod, float* out);

    int(__cdecl* SetFollowEye)(AmodMod mod, int follows);
    int(__cdecl* SetFollowTarget)(AmodMod mod, int follows);

    /* scripts/camera.txt keys (room_dist, follow_speed, zoom_*, rock_*, ...) */
    int(__cdecl* GetProperty)(AmodMod mod, const char* key, AmodValue* value);
    int(__cdecl* SetProperty)(AmodMod mod, const char* key, const AmodValue* value);
    int(__cdecl* PropertyCount)(AmodMod mod);
    const char* (__cdecl* PropertyNameAt)(AmodMod mod, int at);

    /* Reload scripts/camera.txt (cam_reload). */
    int(__cdecl* Reload)(AmodMod mod);

    int(__cdecl* GetEffect)(AmodMod mod);
    int(__cdecl* StartEffect)(AmodMod mod, int effect);
    int(__cdecl* StopEffect)(AmodMod mod);

    /* $event_rock; independent of effects. */
    int(__cdecl* StartShake)(AmodMod mod);
    int(__cdecl* StopShake)(AmodMod mod);

    int(__cdecl* SetDriver)(AmodMod mod, AmodCameraDriver driver);
    AmodCameraDriver(__cdecl* GetDriver)(AmodMod mod);
    int(__cdecl* SetUpdateHandler)(AmodMod mod, AmodCameraUpdateHandler handler, void* user);

    /* After camera+driver; last say before draw. */
    int(__cdecl* SetViewHandler)(AmodMod mod, AmodCameraViewHandler handler, void* user);

    /* Last frame view sent to renderer. */
    int(__cdecl* GetView)(AmodMod mod, AmodCameraView* out);

    /* Last world projection (fov/aspect/near/far). */
    int(__cdecl* GetLens)(AmodMod mod, AmodCameraLens* out);

    /* Field >0 taken, <=0 left as-is; ClearLens restores all. World only. */
    int(__cdecl* SetLens)(AmodMod mod, const AmodCameraLens* lens);
    int(__cdecl* ClearLens)(AmodMod mod);

    /* Before game snaps camera on level/room start. */
    int(__cdecl* SetEventHandler)(AmodMod mod, AmodCameraEventHandler handler, void* user);

    /* cam_free != 0: game flies camera; drivers ignored. */
    int(__cdecl* GetFreeMode)(AmodMod mod);

} AmodCameraApi;

AMOD_LAST_FIELD(AmodCameraApi, GetFreeMode, 0x7C);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Camera
    {
        inline const AmodCameraApi* g_camera = nullptr;
        inline AmodMod g_cameraSelf = nullptr;

        inline void                                                 Attach(const AmodCameraApi* api, AmodMod self)
        {
            g_camera = api;
            g_cameraSelf = self;
        }

        inline bool                                                 IsHere()
        {
            return g_camera != nullptr && g_camera->IsHere(g_cameraSelf) != 0;
        }

        inline bool                                                 Get(AmodCamera& out)
        {
            out.structSize = sizeof(AmodCamera);
            return g_camera != nullptr && g_camera->Get(g_cameraSelf, &out) != 0;
        }

        inline bool                                                 SetEye(const float* position)
        {
            return g_camera != nullptr && g_camera->SetEye(g_cameraSelf, position) != 0;
        }

        inline bool                                                 SetEye(float x, float y, float z)
        {
            const float position[3] = { x, y, z };
            return SetEye(position);
        }

        inline bool                                                 SetTarget(const float* position)
        {
            return g_camera != nullptr && g_camera->SetTarget(g_cameraSelf, position) != 0;
        }

        inline bool                                                 SetTarget(float x, float y, float z)
        {
            const float position[3] = { x, y, z };
            return SetTarget(position);
        }

        inline bool                                                 SetUp(const float* direction)
        {
            return g_camera != nullptr && g_camera->SetUp(g_cameraSelf, direction) != 0;
        }

        inline bool                                                 SetRoll(float radians)
        {
            return g_camera != nullptr && g_camera->SetRoll(g_cameraSelf, radians) != 0;
        }

        inline bool                                                 Follow(const float* point)
        {
            return g_camera != nullptr && g_camera->Follow(g_cameraSelf, point) != 0;
        }

        inline bool                                                 Place(const float* point)
        {
            return g_camera != nullptr && g_camera->Place(g_cameraSelf, point) != 0;
        }

        inline bool                                                 GetFocus(float* out)
        {
            return g_camera != nullptr && g_camera->GetFocus(g_cameraSelf, out) != 0;
        }

        inline bool                                                 SetFollowEye(bool follows)
        {
            return g_camera != nullptr && g_camera->SetFollowEye(g_cameraSelf, follows ? 1 : 0) != 0;
        }

        inline bool                                                 SetFollowTarget(bool follows)
        {
            return g_camera != nullptr && g_camera->SetFollowTarget(g_cameraSelf, follows ? 1 : 0) != 0;
        }

        inline bool                                                 GetProperty(const char* key, AmodValue& value)
        {
            return g_camera != nullptr && g_camera->GetProperty(g_cameraSelf, key, &value) != 0;
        }

        inline bool                                                 SetProperty(const char* key, const AmodValue& value)
        {
            return g_camera != nullptr && g_camera->SetProperty(g_cameraSelf, key, &value) != 0;
        }

        inline int                                                  PropertyCount()
        {
            return g_camera != nullptr ? g_camera->PropertyCount(g_cameraSelf) : 0;
        }

        inline const char*                                          PropertyNameAt(int at)
        {
            return g_camera != nullptr ? g_camera->PropertyNameAt(g_cameraSelf, at) : "";
        }

        inline bool                                                 Reload()
        {
            return g_camera != nullptr && g_camera->Reload(g_cameraSelf) != 0;
        }

        inline int                                                  GetEffect()
        {
            return g_camera != nullptr ? g_camera->GetEffect(g_cameraSelf) : AMOD_CAMERA_EFFECT_NONE;
        }

        inline bool                                                 StartEffect(int effect)
        {
            return g_camera != nullptr && g_camera->StartEffect(g_cameraSelf, effect) != 0;
        }

        inline bool                                                 StopEffect()
        {
            return g_camera != nullptr && g_camera->StopEffect(g_cameraSelf) != 0;
        }

        inline bool                                                 StartShake()
        {
            return g_camera != nullptr && g_camera->StartShake(g_cameraSelf) != 0;
        }

        inline bool                                                 StopShake()
        {
            return g_camera != nullptr && g_camera->StopShake(g_cameraSelf) != 0;
        }

        inline bool                                                 SetDriver(AmodCameraDriver driver)
        {
            return g_camera != nullptr && g_camera->SetDriver(g_cameraSelf, driver) != 0;
        }

        inline AmodCameraDriver                                     GetDriver()
        {
            return g_camera != nullptr ? g_camera->GetDriver(g_cameraSelf) : AMOD_CAMERA_DRIVER_GAME;
        }

        inline bool                                                 SetUpdateHandler(AmodCameraUpdateHandler handler, void* user = nullptr)
        {
            return g_camera != nullptr && g_camera->SetUpdateHandler(g_cameraSelf, handler, user) != 0;
        }

        inline bool                                                 SetViewHandler(AmodCameraViewHandler handler, void* user = nullptr)
        {
            return g_camera != nullptr && g_camera->SetViewHandler(g_cameraSelf, handler, user) != 0;
        }

        inline bool                                                 GetView(AmodCameraView& out)
        {
            out.structSize = sizeof(AmodCameraView);
            return g_camera != nullptr && g_camera->GetView(g_cameraSelf, &out) != 0;
        }

        inline bool                                                 GetLens(AmodCameraLens& out)
        {
            out.structSize = sizeof(AmodCameraLens);
            return g_camera != nullptr && g_camera->GetLens(g_cameraSelf, &out) != 0;
        }

        inline bool                                                 SetLens(const AmodCameraLens& lens)
        {
            return g_camera != nullptr && g_camera->SetLens(g_cameraSelf, &lens) != 0;
        }

        inline bool                                                 SetFov(float radians)
        {
            AmodCameraLens lens = {};
            lens.structSize = sizeof(lens);
            lens.fov = radians;
            return SetLens(lens);
        }

        inline bool                                                 SetPlanes(float nearPlane, float farPlane)
        {
            AmodCameraLens lens = {};
            lens.structSize = sizeof(lens);
            lens.nearPlane = nearPlane;
            lens.farPlane = farPlane;
            return SetLens(lens);
        }

        inline bool                                                 ClearLens()
        {
            return g_camera != nullptr && g_camera->ClearLens(g_cameraSelf) != 0;
        }

        inline bool                                                 SetEventHandler(AmodCameraEventHandler handler, void* user = nullptr)
        {
            return g_camera != nullptr && g_camera->SetEventHandler(g_cameraSelf, handler, user) != 0;
        }

        inline int                                                  GetFreeMode()
        {
            return g_camera != nullptr ? g_camera->GetFreeMode(g_cameraSelf) : 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_CAMERA_API_H */
