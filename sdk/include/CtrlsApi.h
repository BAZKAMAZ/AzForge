#ifndef AZFORGE_CTRLS_API_H
#define AZFORGE_CTRLS_API_H

#include "ForgeTypes.h"
#include "MmdApi.h"
#include "TextureApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stable id on the record block; survives room leave/return. Counted from 1, never reused. */
typedef uint32_t AmodCtrlId;
#define AMOD_CTRL_NONE                                              0

/* Live ctrl in the room (lift, door, fire...). name = entity/block name. */
typedef struct AmodCtrl
{
    uint32_t structSize;

    int entity; /* entity list index */
    const char* name;

    float position[3];
    int column;
    int row;

    int isHidden;

    /* $tag or ""; notes look this up */
    const char* tag;

    /* tag on/off, or door open/shut */
    int tagOn;

    AmodCtrlId id; /* stable across frames/rooms */

} AmodCtrl;

AMOD_LAST_FIELD(AmodCtrl, id, 0x2C);

/* Placement record (block); bonuses/monsters excluded. */
typedef struct AmodCtrlPlacement
{
    uint32_t structSize;

    int kind; /* Ctrl_Spawn kind; not 9/10 */
    const char* kindName;
    const char* tag; /* "" if kind has no tag */

    AmodCtrlId id; /* same id as Get; follows across rooms */

} AmodCtrlPlacement;

AMOD_LAST_FIELD(AmodCtrlPlacement, id, 0x10);

/* Who updates this ctrl; per-thing. */
typedef enum AmodCtrlDriver
{
    AMOD_CTRL_DRIVER_GAME = 0,   /* game update */
    AMOD_CTRL_DRIVER_MOD = 1,    /* handler only */
    AMOD_CTRL_DRIVER_BOTH = 2,   /* game then handler */
    AMOD_CTRL_DRIVER_NOBODY = 3  /* neither; stands still */
} AmodCtrlDriver;

typedef struct AmodCtrlFrame
{
    uint32_t structSize;

    int index; /* index this frame; invalid after room change */
    const char* name;

    float delta;
    float position[3];

} AmodCtrlFrame;

AMOD_LAST_FIELD(AmodCtrlFrame, position, 0x10);

/* valid only for the call */
typedef void(__cdecl* AmodCtrlUpdateHandler)(const AmodCtrlFrame* frame, void* user);

/* Touch result; only silences this ctrl side. */
typedef enum AmodCtrlTouchResult
{
    AMOD_CTRL_TOUCH_DEFAULT = 0, /* game handler after mod */
    AMOD_CTRL_TOUCH_HANDLED = 1  /* mod only; skip game */
} AmodCtrlTouchResult;

/* touch info; every overlapping frame */
typedef struct AmodCtrlTouchInfo
{
    uint32_t structSize;

    int index; /* ctrl index */
    const char* name;
    float position[3];

    int isPlayer;             /* met the player */
    int toucherKind;          /* entity kind if not player */
    const char* toucherName;
    int toucher;              /* entity index for World::GetEntity */

    int side; /* 1 up, 2 down, 3 left, 4 right */

    /* New fields are appended, never inserted. */
} AmodCtrlTouchInfo;

AMOD_LAST_FIELD(AmodCtrlTouchInfo, side, 0x28);

typedef AmodCtrlTouchResult(__cdecl* AmodCtrlTouchHandler)(const AmodCtrlTouchInfo* info, void* user);

/* Body filled on BUILD from the mod record. */
typedef struct AmodCtrlBody
{
    uint32_t structSize;

    const char* name; /* name for Find */
    float position[3];

    AmodBox box;
    int isSolid; /* solid pushes; clear = report-only */

    AmodModelId model;
    AmodTextureId texture;

    int isHidden;
    const char* tag; /* may be null */

} AmodCtrlBody;

AMOD_LAST_FIELD(AmodCtrlBody, tag, 0x40);

/* Events for a mod-owned kind; new ones are appended. */
typedef enum AmodCtrlEvent
{
    /* Room spawn/return: fill body from record; nonzero = make it. */
    AMOD_CTRL_EVENT_BUILD = 0,

    /* Per-frame update; frame is the thing. */
    AMOD_CTRL_EVENT_UPDATE = 1,

    /* Touch; return AmodCtrlTouchResult. */
    AMOD_CTRL_EVENT_TOUCH = 2,

    /* Placement parse: text = block body; call Make to spawn. */
    AMOD_CTRL_EVENT_PARSE = 3
} AmodCtrlEvent;

typedef struct AmodCtrlEventInfo
{
    uint32_t structSize;

    int event; /* AMOD_CTRL_EVENT_* */
    AmodCtrlId id;

    /* mod record from Make; always set */
    const void* record;

    /* BUILD only; null otherwise */
    AmodCtrlBody* body;

    /* UPDATE only */
    const AmodCtrlFrame* frame;

    /* TOUCH only */
    const AmodCtrlTouchInfo* touch;

    /* PARSE only: block body + roomIndex */
    const char* text;
    int roomIndex;

} AmodCtrlEventInfo;

AMOD_LAST_FIELD(AmodCtrlEventInfo, roomIndex, 0x20);

/* nonzero = build; TouchResult on touch; ignored on update */
typedef int(__cdecl* AmodCtrlKindHandler)(const AmodCtrlEventInfo* info, void* user);

typedef struct AmodCtrlsApi
{
    uint32_t structSize;

    int(__cdecl* GetCount)(AmodMod mod);

    /* set out->structSize first */
    int(__cdecl* Get)(AmodMod mod, int index, AmodCtrl* out);

    /* next name prefix match from `from`; -1 if none */
    int(__cdecl* Find)(AmodMod mod, const char* name, int from);

    int(__cdecl* SetPosition)(AmodMod mod, int index, const float* position);
    int(__cdecl* SetHidden)(AmodMod mod, int index, int hidden);

    /* Live properties; read-only keys were spent at spawn. */
    int(__cdecl* GetProperty)(AmodMod mod, int index, const char* key, AmodValue* value);
    int(__cdecl* SetProperty)(AmodMod mod, int index, const char* key, const AmodValue* value);
    int(__cdecl* PropertyCount)(AmodMod mod, int index);
    const char* (__cdecl* PropertyNameAt)(AmodMod mod, int index, int at);

    /* Live collision box (from spawn size keys). */
    int(__cdecl* GetBox)(AmodMod mod, int index, AmodBox* out);
    int(__cdecl* SetBox)(AmodMod mod, int index, const AmodBox* box);

    /* One mod per ctrl; ends on room/level change. */
    int(__cdecl* Drive)(AmodMod mod, int index, AmodCtrlDriver driver, AmodCtrlUpdateHandler handler, void* user);
    int(__cdecl* Release)(AmodMod mod, int index);
    AmodCtrlDriver(__cdecl* Driver)(AmodMod mod, int index);

    /* Touch before game handler; same claim as Drive. */
    int(__cdecl* OnTouch)(AmodMod mod, int index, AmodCtrlTouchHandler handler, void* user);

    /* Spawn from a _ctrl.txt-style block (placed kinds). Grid kinds use CreateOn. */
    int(__cdecl* Create)(AmodMod mod, const char* block);

    /* Grid kinds: pass cells like World::Cells; '\0' = skip. Returns whether block was read. */
    int(__cdecl* CreateOn)(AmodMod mod, const char* block, const char* cells, int count);

    /* Remove for good: drop record + mark entity dead. */
    int(__cdecl* Remove)(AmodMod mod, int index);

    /* First live ctrl with this tag; -1 if none. */
    int(__cdecl* FindByTag)(AmodMod mod, const char* tag);

    /* Set tag on/off; update may overwrite next frame. */
    int(__cdecl* SetTagOn)(AmodMod mod, int index, int on);

    /* Set tag on record+entity; only kinds with a tag field. */
    int(__cdecl* SetTag)(AmodMod mod, int index, const char* tag);

    /* Touchable flag; clear to stop collisions. */
    int(__cdecl* SetTouchable)(AmodMod mod, int index, int touchable);

    /* Pitch and yaw in radians; a door swings open by winding the yaw up. */
    int(__cdecl* SetAngles)(AmodMod mod, int index, float pitch, float yaw);

    /* Placement list for any room; roomIndex < 0 = current. */
    int(__cdecl* PlacementCount)(AmodMod mod, int roomIndex);
    int(__cdecl* GetPlacement)(AmodMod mod, int roomIndex, int at, AmodCtrlPlacement* out);

    int(__cdecl* GetPlacementProperty)(AmodMod mod, int roomIndex, int at, const char* key, AmodValue* value);
    int(__cdecl* SetPlacementProperty)(AmodMod mod, int roomIndex, int at, const char* key, const AmodValue* value);
    int(__cdecl* PlacementPropertyCount)(AmodMod mod, int roomIndex, int at);
    const char* (__cdecl* PlacementPropertyNameAt)(AmodMod mod, int roomIndex, int at, int which);

    int(__cdecl* CreateIn)(AmodMod mod, int roomIndex, const char* block);
    int(__cdecl* RemovePlacement)(AmodMod mod, int roomIndex, int at);

    /* Id <-> index; index -1 if not currently live. */
    AmodCtrlId(__cdecl* IdAt)(AmodMod mod, int index);
    int(__cdecl* IndexOf)(AmodMod mod, AmodCtrlId id);

    /* Room of this id; -1 if unknown. */
    int(__cdecl* RoomOf)(AmodMod mod, AmodCtrlId id);

    /* Register a mod kind; returns kind > game range, or 0. */
    int(__cdecl* RegisterKind)(AmodMod mod, const char* name, AmodCtrlKindHandler handler, void* user);

    /* Make a registered kind; record is mod bytes (kind written for you). */
    AmodCtrlId(__cdecl* Make)(AmodMod mod, int roomIndex, int kind, const void* record, int size);

    /* Change model/texture on a live ctrl. */
    int(__cdecl* SetLook)(AmodMod mod, int index, AmodModelId model, AmodTextureId texture);

    /* Borrow a game kind; handler gets UPDATE/TOUCH. Game block names only. */
    AmodCtrlId(__cdecl* Borrow)(AmodMod mod, int roomIndex, const char* block, AmodCtrlKindHandler handler, void* user);

} AmodCtrlsApi;

AMOD_LAST_FIELD(AmodCtrlsApi, Borrow, 0x98);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Ctrls
    {
        inline const AmodCtrlsApi* g_ctrls = nullptr;
        inline AmodMod g_ctrlsSelf = nullptr;

        inline void                                                 Attach(const AmodCtrlsApi* api, AmodMod self)
        {
            g_ctrls = api;
            g_ctrlsSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_ctrls != nullptr;
        }

        inline int                                                  Count()
        {
            return g_ctrls != nullptr ? g_ctrls->GetCount(g_ctrlsSelf) : 0;
        }

        inline bool                                                 Get(int index, AmodCtrl& out)
        {
            out = AmodCtrl{};
            out.structSize = sizeof(out);
            return g_ctrls != nullptr && g_ctrls->Get(g_ctrlsSelf, index, &out) != 0;
        }

        inline int                                                  Find(const char* name, int from = 0)
        {
            return g_ctrls != nullptr ? g_ctrls->Find(g_ctrlsSelf, name, from) : -1;
        }

        inline bool                                                 SetPosition(int index, float x, float y, float z)
        {
            const float position[3] = { x, y, z };
            return g_ctrls != nullptr && g_ctrls->SetPosition(g_ctrlsSelf, index, position) != 0;
        }

        inline bool                                                 SetHidden(int index, bool hidden)
        {
            return g_ctrls != nullptr && g_ctrls->SetHidden(g_ctrlsSelf, index, hidden ? 1 : 0) != 0;
        }

        inline bool                                                 GetProperty(int index, const char* key, AmodValue& value)
        {
            return g_ctrls != nullptr && g_ctrls->GetProperty(g_ctrlsSelf, index, key, &value) != 0;
        }

        inline bool                                                 SetProperty(int index, const char* key, const AmodValue& value)
        {
            return g_ctrls != nullptr && g_ctrls->SetProperty(g_ctrlsSelf, index, key, &value) != 0;
        }

        inline bool                                                 SetFloat(int index, const char* key, float number)
        {
            AmodValue value = {};
            value.kind = AMOD_VALUE_FLOAT;
            value.count = 1;
            value.numbers[0] = number;
            return SetProperty(index, key, value);
        }

        inline int                                                  PropertyCount(int index)
        {
            return g_ctrls != nullptr ? g_ctrls->PropertyCount(g_ctrlsSelf, index) : 0;
        }

        inline const char*                                          PropertyNameAt(int index, int at)
        {
            return g_ctrls != nullptr ? g_ctrls->PropertyNameAt(g_ctrlsSelf, index, at) : "";
        }

        inline bool                                                 GetBox(int index, AmodBox& out)
        {
            out = AmodBox{};
            out.structSize = sizeof(out);
            return g_ctrls != nullptr && g_ctrls->GetBox != nullptr &&
                g_ctrls->GetBox(g_ctrlsSelf, index, &out) != 0;
        }

        inline bool                                                 SetBox(int index, const AmodBox& box)
        {
            return g_ctrls != nullptr && g_ctrls->SetBox != nullptr &&
                g_ctrls->SetBox(g_ctrlsSelf, index, &box) != 0;
        }

        inline bool                                                 Drive(int index, AmodCtrlUpdateHandler handler, void* user = nullptr)
        {
            return g_ctrls != nullptr && g_ctrls->Drive != nullptr &&
                g_ctrls->Drive(g_ctrlsSelf, index, AMOD_CTRL_DRIVER_MOD, handler, user) != 0;
        }

        inline bool                                                 DriveAlongside(int index, AmodCtrlUpdateHandler handler, void* user = nullptr)
        {
            return g_ctrls != nullptr && g_ctrls->Drive != nullptr &&
                g_ctrls->Drive(g_ctrlsSelf, index, AMOD_CTRL_DRIVER_BOTH, handler, user) != 0;
        }

        inline bool                                                 Freeze(int index)
        {
            return g_ctrls != nullptr && g_ctrls->Drive != nullptr &&
                g_ctrls->Drive(g_ctrlsSelf, index, AMOD_CTRL_DRIVER_NOBODY, nullptr, nullptr) != 0;
        }

        inline int                                                  RegisterKind(const char* name, AmodCtrlKindHandler handler, void* user = nullptr)
        {
            return g_ctrls != nullptr && g_ctrls->RegisterKind != nullptr
                ? g_ctrls->RegisterKind(g_ctrlsSelf, name, handler, user) : 0;
        }

        /* Lambda form; captures kept in static. */
        template <typename Handler>
        inline int                                                  RegisterKind(const char* name, Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return RegisterKind(name, [](const AmodCtrlEventInfo* info, void*) -> int
            {
                return kept(info);
            }, nullptr);
        }

        inline AmodCtrlId                                           Make(int roomIndex, int kind, const void* record, int size)
        {
            return g_ctrls != nullptr && g_ctrls->Make != nullptr
                ? g_ctrls->Make(g_ctrlsSelf, roomIndex, kind, record, size) : AMOD_CTRL_NONE;
        }

        inline AmodCtrlId                                           Borrow(int roomIndex, const char* block, AmodCtrlKindHandler handler, void* user = nullptr)
        {
            return g_ctrls != nullptr && g_ctrls->Borrow != nullptr
                ? g_ctrls->Borrow(g_ctrlsSelf, roomIndex, block, handler, user) : AMOD_CTRL_NONE;
        }

        inline bool                                                 SetLook(int index, AmodModelId model, AmodTextureId texture)
        {
            return g_ctrls != nullptr && g_ctrls->SetLook != nullptr &&
                g_ctrls->SetLook(g_ctrlsSelf, index, model, texture) != 0;
        }

        inline AmodCtrlId                                           IdAt(int index)
        {
            return g_ctrls != nullptr && g_ctrls->IdAt != nullptr
                ? g_ctrls->IdAt(g_ctrlsSelf, index) : AMOD_CTRL_NONE;
        }

        /* -1 if not currently live */
        inline int                                                  IndexOf(AmodCtrlId id)
        {
            return g_ctrls != nullptr && g_ctrls->IndexOf != nullptr
                ? g_ctrls->IndexOf(g_ctrlsSelf, id) : -1;
        }

        inline int                                                  RoomOf(AmodCtrlId id)
        {
            return g_ctrls != nullptr && g_ctrls->RoomOf != nullptr
                ? g_ctrls->RoomOf(g_ctrlsSelf, id) : -1;
        }

        /* roomIndex < 0 = current room */
        inline int                                                  PlacementCount(int roomIndex = -1)
        {
            return g_ctrls != nullptr && g_ctrls->PlacementCount != nullptr
                ? g_ctrls->PlacementCount(g_ctrlsSelf, roomIndex) : 0;
        }

        inline bool                                                 GetPlacement(int at, AmodCtrlPlacement& out, int roomIndex = -1)
        {
            out = AmodCtrlPlacement{};
            out.structSize = sizeof(out);
            return g_ctrls != nullptr && g_ctrls->GetPlacement != nullptr &&
                g_ctrls->GetPlacement(g_ctrlsSelf, roomIndex, at, &out) != 0;
        }

        inline bool                                                 GetPlacementProperty(int at, const char* key, AmodValue& value, int roomIndex = -1)
        {
            return g_ctrls != nullptr && g_ctrls->GetPlacementProperty != nullptr &&
                g_ctrls->GetPlacementProperty(g_ctrlsSelf, roomIndex, at, key, &value) != 0;
        }

        inline bool                                                 SetPlacementProperty(int at, const char* key, const AmodValue& value, int roomIndex = -1)
        {
            return g_ctrls != nullptr && g_ctrls->SetPlacementProperty != nullptr &&
                g_ctrls->SetPlacementProperty(g_ctrlsSelf, roomIndex, at, key, &value) != 0;
        }

        inline bool                                                 SetPlacementFloat(int at, const char* key, float number, int roomIndex = -1)
        {
            AmodValue value = {};
            value.kind = AMOD_VALUE_FLOAT;
            value.count = 1;
            value.numbers[0] = number;
            return SetPlacementProperty(at, key, value, roomIndex);
        }

        inline int                                                  PlacementPropertyCount(int at, int roomIndex = -1)
        {
            return g_ctrls != nullptr && g_ctrls->PlacementPropertyCount != nullptr
                ? g_ctrls->PlacementPropertyCount(g_ctrlsSelf, roomIndex, at) : 0;
        }

        inline const char*                                          PlacementPropertyNameAt(int at, int which, int roomIndex = -1)
        {
            return g_ctrls != nullptr && g_ctrls->PlacementPropertyNameAt != nullptr
                ? g_ctrls->PlacementPropertyNameAt(g_ctrlsSelf, roomIndex, at, which) : "";
        }

        inline int                                                  CreateIn(int roomIndex, const char* block)
        {
            return g_ctrls != nullptr && g_ctrls->CreateIn != nullptr
                ? g_ctrls->CreateIn(g_ctrlsSelf, roomIndex, block) : 0;
        }

        inline bool                                                 RemovePlacement(int at, int roomIndex = -1)
        {
            return g_ctrls != nullptr && g_ctrls->RemovePlacement != nullptr &&
                g_ctrls->RemovePlacement(g_ctrlsSelf, roomIndex, at) != 0;
        }

        inline int                                                  FindByTag(const char* tag)
        {
            return g_ctrls != nullptr && g_ctrls->FindByTag != nullptr
                ? g_ctrls->FindByTag(g_ctrlsSelf, tag) : -1;
        }

        inline bool                                                 SetTag(int index, const char* tag)
        {
            return g_ctrls != nullptr && g_ctrls->SetTag != nullptr &&
                g_ctrls->SetTag(g_ctrlsSelf, index, tag) != 0;
        }

        inline bool                                                 SetAngles(int index, float pitch, float yaw)
        {
            return g_ctrls != nullptr && g_ctrls->SetAngles != nullptr &&
                g_ctrls->SetAngles(g_ctrlsSelf, index, pitch, yaw) != 0;
        }

        inline bool                                                 SetTouchable(int index, bool touchable)
        {
            return g_ctrls != nullptr && g_ctrls->SetTouchable != nullptr &&
                g_ctrls->SetTouchable(g_ctrlsSelf, index, touchable ? 1 : 0) != 0;
        }

        inline bool                                                 SetTagOn(int index, bool on)
        {
            return g_ctrls != nullptr && g_ctrls->SetTagOn != nullptr &&
                g_ctrls->SetTagOn(g_ctrlsSelf, index, on ? 1 : 0) != 0;
        }

        inline int                                                  Create(const char* block)
        {
            return g_ctrls != nullptr && g_ctrls->Create != nullptr
                ? g_ctrls->Create(g_ctrlsSelf, block) : 0;
        }

        inline int                                                  CreateOn(const char* block, const char* cells, int count)
        {
            return g_ctrls != nullptr && g_ctrls->CreateOn != nullptr
                ? g_ctrls->CreateOn(g_ctrlsSelf, block, cells, count) : 0;
        }

        inline bool                                                 Remove(int index)
        {
            return g_ctrls != nullptr && g_ctrls->Remove != nullptr &&
                g_ctrls->Remove(g_ctrlsSelf, index) != 0;
        }

        inline bool                                                 OnTouch(int index, AmodCtrlTouchHandler handler, void* user = nullptr)
        {
            return g_ctrls != nullptr && g_ctrls->OnTouch != nullptr &&
                g_ctrls->OnTouch(g_ctrlsSelf, index, handler, user) != 0;
        }

        inline bool                                                 Release(int index)
        {
            return g_ctrls != nullptr && g_ctrls->Release != nullptr &&
                g_ctrls->Release(g_ctrlsSelf, index) != 0;
        }

        inline AmodCtrlDriver                                       Driver(int index)
        {
            return g_ctrls != nullptr && g_ctrls->Driver != nullptr ? g_ctrls->Driver(g_ctrlsSelf, index)
                                                                     : AMOD_CTRL_DRIVER_GAME;
        }
    }
}

#endif

#endif
