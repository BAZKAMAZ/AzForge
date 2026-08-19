#ifndef AZFORGE_DEMO_API_H
#define AZFORGE_DEMO_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One $move_cmd: pause or move. */
typedef struct AmodDemoMove
{
    uint32_t structSize;

    float pause;            /* $pause, seconds */
    int horizontal;         /* $move_horz, -1, 0 or 1 */
    int vertical;           /* $move_vert */
    int jump;               /* $jump */
    float endCoord;         /* $end_coord */

    /* Read-only command counts. */
    int timeCount;
    int viewCount;
    int camCount;

} AmodDemoMove;

AMOD_LAST_FIELD(AmodDemoMove, camCount, 0x20);

/* $cam_cmd and $view_cmd share one record. */
typedef struct AmodDemoCamera
{
    uint32_t structSize;

    float position[3];      /* $pos */
    float speed[3];         /* $speed */
    float time;             /* $time */
    int giveBack;           /* $auto */

} AmodDemoCamera;

AMOD_LAST_FIELD(AmodDemoCamera, giveBack, 0x20);

/* $time_cmd. */
typedef struct AmodDemoTime
{
    uint32_t structSize;

    float pause;
    float factor;
    float time;

} AmodDemoTime;

AMOD_LAST_FIELD(AmodDemoTime, time, 0x0C);

/* $player_place. */
typedef struct AmodDemoPlace
{
    uint32_t structSize;

    int playerX;
    int playerY;
    int roomX;
    int roomY;

} AmodDemoPlace;

AMOD_LAST_FIELD(AmodDemoPlace, roomY, 0x10);

typedef enum AmodDemoEvent
{
    AMOD_DEMO_STARTED = 1,
    AMOD_DEMO_ENDED = 2, /* script ended */
    AMOD_DEMO_BROKEN = 3 /* stopped by key */
} AmodDemoEvent;

typedef void (__cdecl* AmodDemoCallback)(AmodDemoEvent event, int script, void* user);

/* Called on each $move_cmd. */
typedef void (__cdecl* AmodDemoMoveCallback)(int script, int move, void* user);

/* Overrides script input when non-zero is returned. */
typedef int (__cdecl* AmodDemoInputCallback)(const float position[3], int* horizontal, int* vertical, int* jump, float seconds, void* user);

typedef struct AmodDemoApi
{
    uint32_t structSize;

    /* Whether a demo is playing. */
    int (__cdecl* IsPlaying)(AmodMod mod);

    /* Script count and current index; -1 if none. */
    int (__cdecl* Count)(AmodMod mod);
    int (__cdecl* Current)(AmodMod mod);

    /* Elapsed script time. */
    float (__cdecl* Elapsed)(AmodMod mod);

    /* $break_time. */
    float (__cdecl* GetBreakTime)(AmodMod mod, int script);
    int (__cdecl* SetBreakTime)(AmodMod mod, int script, float seconds);

    int (__cdecl* MoveCount)(AmodMod mod, int script);

    /* Current $move_cmd; -1 if none. */
    int (__cdecl* MoveIndex)(AmodMod mod);

    int (__cdecl* GetMove)(AmodMod mod, int script, int move, AmodDemoMove* out);
    int (__cdecl* SetMove)(AmodMod mod, int script, int move, const AmodDemoMove* value);

    /* Camera and view per $move_cmd. */
    int (__cdecl* GetCamera)(AmodMod mod, int script, int move, int index, AmodDemoCamera* out);
    int (__cdecl* SetCamera)(AmodMod mod, int script, int move, int index, const AmodDemoCamera* value);
    int (__cdecl* GetView)(AmodMod mod, int script, int move, int index, AmodDemoCamera* out);
    int (__cdecl* SetView)(AmodMod mod, int script, int move, int index, const AmodDemoCamera* value);

    int (__cdecl* GetTime)(AmodMod mod, int script, int move, int index, AmodDemoTime* out);
    int (__cdecl* SetTime)(AmodMod mod, int script, int move, int index, const AmodDemoTime* value);

    /* $camera_start and $player_place. */
    int (__cdecl* GetCameraStart)(AmodMod mod, int script, float position[3], float view[3]);
    int (__cdecl* SetCameraStart)(AmodMod mod, int script, const float position[3], const float view[3]);
    int (__cdecl* GetPlace)(AmodMod mod, int script, AmodDemoPlace* out);

    /* Play, stop or advance the demo. */
    int (__cdecl* Play)(AmodMod mod, int script);
    int (__cdecl* Stop)(AmodMod mod);
    int (__cdecl* Next)(AmodMod mod);

    /* Add/remove a mod script. */
    int (__cdecl* Add)(AmodMod mod, const char* path);
    int (__cdecl* Remove)(AmodMod mod, int script);

    /* Suppress level $demo entries on load. */
    int (__cdecl* SuppressLevelDemos)(AmodMod mod, int suppress);

    AmodToken (__cdecl* OnDemo)(AmodMod mod, AmodDemoCallback callback, void* user);
    AmodToken (__cdecl* OnMove)(AmodMod mod, AmodDemoMoveCallback callback, void* user);
    AmodToken (__cdecl* OnInput)(AmodMod mod, AmodDemoInputCallback callback, void* user);
    void (__cdecl* Cancel)(AmodMod mod, AmodToken token);

    /* Creates empty scripts and appends records. */
    int (__cdecl* Create)(AmodMod mod);
    int (__cdecl* AddMove)(AmodMod mod, int script, const AmodDemoMove* value);
    int (__cdecl* AddCamera)(AmodMod mod, int script, int move, const AmodDemoCamera* value);
    int (__cdecl* AddView)(AmodMod mod, int script, int move, const AmodDemoCamera* value);
    int (__cdecl* AddTime)(AmodMod mod, int script, int move, const AmodDemoTime* value);

} AmodDemoApi;

AMOD_LAST_FIELD(AmodDemoApi, AddTime, 0x88);

#ifdef __cplusplus
} /* extern "C" */

namespace Amod
{
    namespace Demo
    {
        inline const AmodDemoApi* g_demo = nullptr;
        inline AmodMod g_demoSelf = nullptr;

        inline void                                                 Attach(const AmodDemoApi* api, AmodMod self)
        {
            g_demo = api;
            g_demoSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_demo != nullptr;
        }

        inline bool                                                 IsPlaying()
        {
            return g_demo != nullptr && g_demo->IsPlaying(g_demoSelf) != 0;
        }

        inline int                                                  Count()
        {
            return g_demo != nullptr ? g_demo->Count(g_demoSelf) : 0;
        }

        inline int                                                  Current()
        {
            return g_demo != nullptr ? g_demo->Current(g_demoSelf) : -1;
        }

        inline float                                                Elapsed()
        {
            return g_demo != nullptr ? g_demo->Elapsed(g_demoSelf) : 0.0f;
        }

        inline float                                                BreakTime(int script)
        {
            return g_demo != nullptr ? g_demo->GetBreakTime(g_demoSelf, script) : 0.0f;
        }

        inline bool                                                 SetBreakTime(int script, float seconds)
        {
            return g_demo != nullptr && g_demo->SetBreakTime(g_demoSelf, script, seconds) != 0;
        }

        inline int                                                  MoveCount(int script)
        {
            return g_demo != nullptr ? g_demo->MoveCount(g_demoSelf, script) : 0;
        }

        inline int                                                  MoveIndex()
        {
            return g_demo != nullptr ? g_demo->MoveIndex(g_demoSelf) : -1;
        }

        inline bool                                                 GetMove(int script, int move, AmodDemoMove* out)
        {
            if (g_demo == nullptr || out == nullptr)
            {
                return false;
            }
            out->structSize = sizeof(AmodDemoMove);
            return g_demo->GetMove(g_demoSelf, script, move, out) != 0;
        }

        inline bool                                                 SetMove(int script, int move, const AmodDemoMove& value)
        {
            return g_demo != nullptr && g_demo->SetMove(g_demoSelf, script, move, &value) != 0;
        }

        inline bool                                                 GetCamera(int script, int move, int index, AmodDemoCamera* out)
        {
            if (g_demo == nullptr || out == nullptr)
            {
                return false;
            }
            out->structSize = sizeof(AmodDemoCamera);
            return g_demo->GetCamera(g_demoSelf, script, move, index, out) != 0;
        }

        inline bool                                                 SetCamera(int script, int move, int index, const AmodDemoCamera& value)
        {
            return g_demo != nullptr && g_demo->SetCamera(g_demoSelf, script, move, index, &value) != 0;
        }

        inline bool                                                 GetView(int script, int move, int index, AmodDemoCamera* out)
        {
            if (g_demo == nullptr || out == nullptr)
            {
                return false;
            }
            out->structSize = sizeof(AmodDemoCamera);
            return g_demo->GetView(g_demoSelf, script, move, index, out) != 0;
        }

        inline bool                                                 SetView(int script, int move, int index, const AmodDemoCamera& value)
        {
            return g_demo != nullptr && g_demo->SetView(g_demoSelf, script, move, index, &value) != 0;
        }

        inline bool                                                 GetTime(int script, int move, int index, AmodDemoTime* out)
        {
            if (g_demo == nullptr || out == nullptr)
            {
                return false;
            }
            out->structSize = sizeof(AmodDemoTime);
            return g_demo->GetTime(g_demoSelf, script, move, index, out) != 0;
        }

        inline bool                                                 SetTime(int script, int move, int index, const AmodDemoTime& value)
        {
            return g_demo != nullptr && g_demo->SetTime(g_demoSelf, script, move, index, &value) != 0;
        }

        inline bool                                                 GetCameraStart(int script, float position[3], float view[3])
        {
            return g_demo != nullptr && g_demo->GetCameraStart(g_demoSelf, script, position, view) != 0;
        }

        inline bool                                                 SetCameraStart(int script, const float position[3], const float view[3])
        {
            return g_demo != nullptr && g_demo->SetCameraStart(g_demoSelf, script, position, view) != 0;
        }

        inline bool                                                 GetPlace(int script, AmodDemoPlace* out)
        {
            if (g_demo == nullptr || out == nullptr)
            {
                return false;
            }
            out->structSize = sizeof(AmodDemoPlace);
            return g_demo->GetPlace(g_demoSelf, script, out) != 0;
        }

        inline bool                                                 Play(int script)
        {
            return g_demo != nullptr && g_demo->Play(g_demoSelf, script) != 0;
        }

        inline bool                                                 Stop()
        {
            return g_demo != nullptr && g_demo->Stop(g_demoSelf) != 0;
        }

        inline bool                                                 Next()
        {
            return g_demo != nullptr && g_demo->Next(g_demoSelf) != 0;
        }

        inline int                                                  Add(const char* path)
        {
            return g_demo != nullptr ? g_demo->Add(g_demoSelf, path) : -1;
        }

        inline bool                                                 Remove(int script)
        {
            return g_demo != nullptr && g_demo->Remove(g_demoSelf, script) != 0;
        }

        inline bool                                                 SuppressLevelDemos(bool suppress = true)
        {
            return g_demo != nullptr && g_demo->SuppressLevelDemos(g_demoSelf, suppress ? 1 : 0) != 0;
        }

        inline AmodToken                                            OnDemo(AmodDemoCallback callback, void* user = nullptr)
        {
            return g_demo != nullptr ? g_demo->OnDemo(g_demoSelf, callback, user) : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnMove(AmodDemoMoveCallback callback, void* user = nullptr)
        {
            return g_demo != nullptr ? g_demo->OnMove(g_demoSelf, callback, user) : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnInput(AmodDemoInputCallback callback, void* user = nullptr)
        {
            return g_demo != nullptr ? g_demo->OnInput(g_demoSelf, callback, user) : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Cancel(AmodToken token)
        {
            if (g_demo != nullptr)
            {
                g_demo->Cancel(g_demoSelf, token);
            }
        }

        inline int                                                  Create()
        {
            return g_demo != nullptr && g_demo->structSize >= sizeof(AmodDemoApi)
                ? g_demo->Create(g_demoSelf)
                : -1;
        }

        inline int                                                  AddMove(int script, const AmodDemoMove& value)
        {
            return g_demo != nullptr && g_demo->structSize >= sizeof(AmodDemoApi)
                ? g_demo->AddMove(g_demoSelf, script, &value)
                : -1;
        }

        inline int                                                  AddCamera(int script, int move, const AmodDemoCamera& value)
        {
            return g_demo != nullptr && g_demo->structSize >= sizeof(AmodDemoApi)
                ? g_demo->AddCamera(g_demoSelf, script, move, &value)
                : -1;
        }

        inline int                                                  AddView(int script, int move, const AmodDemoCamera& value)
        {
            return g_demo != nullptr && g_demo->structSize >= sizeof(AmodDemoApi)
                ? g_demo->AddView(g_demoSelf, script, move, &value)
                : -1;
        }

        inline int                                                  AddTime(int script, int move, const AmodDemoTime& value)
        {
            return g_demo != nullptr && g_demo->structSize >= sizeof(AmodDemoApi)
                ? g_demo->AddTime(g_demoSelf, script, move, &value)
                : -1;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_DEMO_API_H */
