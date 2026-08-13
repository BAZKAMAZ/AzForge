#ifndef AZFORGE_MONSTERS_API_H
#define AZFORGE_MONSTERS_API_H

#include "WorldApi.h"
#include "MmdApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The ten stock movements; DefineKind borrows one. */
#define AMOD_MONSTER_MOVES_SKULL                                    "skull"
#define AMOD_MONSTER_MOVES_SNAKE                                    "snake"
#define AMOD_MONSTER_MOVES_SNAKE2                                   "snake2"
#define AMOD_MONSTER_MOVES_SNAKE3                                   "snake3"
#define AMOD_MONSTER_MOVES_SPIDER                                   "spider"
#define AMOD_MONSTER_MOVES_SCORPION                                 "scorpion"
#define AMOD_MONSTER_MOVES_JUMP_SKULL                               "j_skull"
#define AMOD_MONSTER_MOVES_BAT                                      "bat"
#define AMOD_MONSTER_MOVES_MUMMY                                    "mummy"
#define AMOD_MONSTER_MOVES_MUMMY2                                   "mummy2"

// Live index lasts one frame; id lasts the monster.
typedef uint32_t AmodMonsterId;
#define AMOD_MONSTER_NONE                                           0

// Which type box: MAIN / SECOND / OBJECT.
#define AMOD_MONSTER_BOX_MAIN                                       0
#define AMOD_MONSTER_BOX_SECOND                                     1
#define AMOD_MONSTER_BOX_OBJECT                                     2

/* Walk cycle state. */
#define AMOD_MONSTER_STATE_MOVE                                     0
#define AMOD_MONSTER_STATE_SIT                                      1

// mummy2 walk cycle phases.
#define AMOD_MONSTER_WALK_STEP                                      0
#define AMOD_MONSTER_WALK_STAY                                      1

/* One type from monster_res.txt. */
typedef struct AmodMonsterType
{
    uint32_t structSize;

    const char* name;           /* $name */

    float speed;                /* $speed; doubled on hard $level */
    float speedUp;              /* $speed_up */
    float vertSpeed;            /* $vert_speed */

    float partColor[4];         /* $part_color (death blast) */
    float partColorHard[4];     /* $part_color_hard ($level 1/3) */

    /* $model path (MmdApi::Find) */
    const char* model;

    /* New fields are appended, never inserted. */
} AmodMonsterType;

AMOD_LAST_FIELD(AmodMonsterType, model, 0x34);

/* Mod type copied from a known game type. */
typedef struct AmodMonsterTypeDef
{
    uint32_t structSize;

    const char* name;           /* level name / DefineKind match */
    const char* basedOn;        /* known $name; record is the base */

    const char* texEasy;
    const char* texHard;
    const char* model;

    float speed;                /* 0 = keep base */
    float speedUp;
    float vertSpeed;

    /* New fields are appended, never inserted. */
} AmodMonsterTypeDef;

AMOD_LAST_FIELD(AmodMonsterTypeDef, vertSpeed, 0x20);

/* One live monster in the current room. */
typedef struct AmodMonster
{
    uint32_t structSize;

    const char* name;           /* type name */
    const char* tag;            /* $tag */

    const char* moves;

    float position[3];
    int level;                  /* $level; 1/3 = hard */
    int facing;                 /* -1 or 1 */
    float speed;                /* live walk speed */

    /* mummy pair only */
    int hasState;
    int state;                  /* AMOD_MONSTER_STATE_* */
    float timeLeft;             /* until state changes */

    int isHard;                 /* $level 1 or 3 */
    int isDecor;                /* $decor: kill does not count */

    AmodMonsterId id;           /* stable for its lifetime */

    /* index = live list (Get/Kill/...); placed = room list (GetPlaced/...); either -1 if absent */
    int index;
    int placed;

    /* New fields are appended, never inserted. */
} AmodMonster;

AMOD_LAST_FIELD(AmodMonster, placed, 0x44);

/* Placement as a level script writes it. */
typedef struct AmodMonsterPlacement
{
    uint32_t structSize;

    const char* name;
    int column;
    int row;
    int extraA;                 /* $pos[2] */
    int extraB;                 /* $pos[3] */

    const char* tag;
    int level;                  /* 1/3 = hard */
    int facing;                 /* $dir -1/1; 0 means 1 */
    int decor;                  /* kill does not count */
    float attackDist;           /* cells; 0 = game default 5 */

    /* New fields are appended, never inserted. */
} AmodMonsterPlacement;

AMOD_LAST_FIELD(AmodMonsterPlacement, attackDist, 0x28);

typedef enum AmodMonsterKillResult
{
    AMOD_MONSTER_KILL_DEFAULT = 0,  /* game default death */
    AMOD_MONSTER_KILL_HANDLED = 1,  /* handled; skip game death */
    AMOD_MONSTER_KILL_REFUSE = 2    /* leave it alive */
} AmodMonsterKillResult;

typedef struct AmodMonsterKillInfo
{
    uint32_t structSize;

    const char* name;
    const char* tag;
    float position[3];
    int level;
    int isDecor;

    AmodMonsterId id;           /* dies; stops answering after return */

    /* New fields are appended, never inserted. */
} AmodMonsterKillInfo;

AMOD_LAST_FIELD(AmodMonsterKillInfo, id, 0x20);

typedef AmodMonsterKillResult (__cdecl* AmodMonsterKillHandler)(const AmodMonsterKillInfo* info,  void* user);

/* One frame of a behaviour-driven monster. */
typedef struct AmodMonsterThinkInfo
{
    uint32_t structSize;

    int index;                  /* live index this frame */
    float delta;                /* seconds since last frame */

    float position[3];
    int facing;

    AmodMonsterId id;           /* stable across frames */

    /* New fields are appended, never inserted. */
} AmodMonsterThinkInfo;

AMOD_LAST_FIELD(AmodMonsterThinkInfo, id, 0x1C);

typedef void (__cdecl* AmodMonsterThinkHandler)(const AmodMonsterThinkInfo* info, void* user);

typedef enum AmodMonsterTouchResult
{
    AMOD_MONSTER_TOUCH_DEFAULT = 0, /* game rules (totem/knife/death) */
    AMOD_MONSTER_TOUCH_HANDLED = 1, /* handled; skip game rules */
    AMOD_MONSTER_TOUCH_REFUSE = 2   /* no contact effect */
} AmodMonsterTouchResult;

/* Monster meeting something. */
typedef struct AmodMonsterTouchInfo
{
    uint32_t structSize;

    int index;                  /* live index */
    const char* name;
    const char* tag;
    float position[3];

    int isPlayer;               /* met the player */
    int toucherKind;            /* other kind if not player */
    int side;                   /* 1..4 side; 3/4 turn the monster */

    AmodMonsterId id;           /* stable past this meeting */

    /* New fields are appended, never inserted. */
} AmodMonsterTouchInfo;

AMOD_LAST_FIELD(AmodMonsterTouchInfo, id, 0x28);

typedef AmodMonsterTouchResult (__cdecl* AmodMonsterTouchHandler)(const AmodMonsterTouchInfo* info, void* user);

/* Just-built monster; set per-instance state here. */
typedef struct AmodMonsterSpawnInfo
{
    uint32_t structSize;

    AmodMonsterId id;
    int index;                  /* live index */
    int placed;                 /* room list index, or -1 */

    const char* name;
    const char* tag;
    float position[3];
    int level;
    int isDecor;

    /* New fields are appended, never inserted. */
} AmodMonsterSpawnInfo;

AMOD_LAST_FIELD(AmodMonsterSpawnInfo, isDecor, 0x28);

typedef void (__cdecl* AmodMonsterSpawnHandler)(const AmodMonsterSpawnInfo* info, void* user);

/* Placement before anything is built from it. */
typedef struct AmodMonsterPlaceInfo
{
    uint32_t structSize;

    const char* name;
    const char* tag;
    float position[3];
    int level;
    int isDecor;
    int roomIndex;

    /* New fields are appended, never inserted. */
} AmodMonsterPlaceInfo;

AMOD_LAST_FIELD(AmodMonsterPlaceInfo, roomIndex, 0x20);

/* Before a placement becomes a monster; return 0 to refuse. */
typedef int (__cdecl* AmodMonsterPlaceHandler)(const AmodMonsterPlaceInfo* info, void* user);

/* Whether the game tick still runs under a behaviour. */
#define AMOD_MONSTER_ALSO_GAME                                      0
#define AMOD_MONSTER_INSTEAD_OF_GAME                                1

/* Per-frame act; ALSO_GAME runs the game tick after, else suppresses it. */
typedef int (__cdecl* AmodMonsterActHandler)(const AmodMonsterThinkInfo* info, void* user);

/* Mod-owned kind: not in monster_res.txt; handler owns build/tick/touch. */

typedef enum AmodMonsterEvent
{
    /* Fill body from the record; nonzero to make it. */
    AMOD_MONSTER_EVENT_BUILD = 0,

    /* Every frame; return ignored (no game tick behind a kind). */
    AMOD_MONSTER_EVENT_UPDATE = 1,

    /* Contact; return 0 also runs actsLike, else handler only. */
    AMOD_MONSTER_EVENT_TOUCH = 2
} AmodMonsterEvent;

/* Body of a mod-owned monster. */
typedef struct AmodMonsterBody
{
    uint32_t structSize;

    const char* name;
    const char* tag;
    float position[3];
    int facing;                 /* -1 or 1; 0 means 1 */

    AmodModelId modelId;
    AmodTextureId textureId;

    AmodBox box;                /* collision with room/player */
    int isHidden;

    /* Knife hit box; zeroed = same as box. */
    AmodBox hitBox;

    /* Contact like this species ("mummy","bat",...); null = walk-through. */
    const char* actsLike;

    /* Animate like this species; null = still. actsLike already brings its anim. */
    const char* animatedLike;

    /* Own animation (Mmd::LoadAnimation); wins over animatedLike. */
    AmodAnimationId animationId;

    /* Starting frameset; null = "move". */
    const char* frameset;

    /* 1 = hitBox stays fixed; collision box always follows anim. */
    int keepHitBox;

    /* New fields are appended, never inserted. */
} AmodMonsterBody;

AMOD_LAST_FIELD(AmodMonsterBody, keepHitBox, 0x70);

typedef struct AmodMonsterKindInfo
{
    uint32_t structSize;

    int event;                  /* AMOD_MONSTER_EVENT_* */
    int kind;
    const void* record;         /* placement; stable for its life */

    AmodMonsterBody* body;      /* BUILD only */
    const AmodMonsterThinkInfo* think; /* UPDATE only */
    const AmodMonsterTouchInfo* touch; /* TOUCH only */

    /* New fields are appended, never inserted. */
} AmodMonsterKindInfo;

AMOD_LAST_FIELD(AmodMonsterKindInfo, touch, 0x18);

/* Nonzero to build on BUILD; ignored on UPDATE. */
typedef int (__cdecl* AmodMonsterKindHandler)(const AmodMonsterKindInfo* info, void* user);

typedef struct AmodMonstersApi
{
    uint32_t structSize;

    /* Types from monster_res.txt. */
    int (__cdecl* GetTypeCount)(AmodMod mod);
    int (__cdecl* GetType)(AmodMod mod, int index, AmodMonsterType* out);
    int (__cdecl* FindType)(AmodMod mod, const char* name, AmodMonsterType* out);

    /* Append a type copied from a known one. */
    int (__cdecl* DefineType)(AmodMod mod, const AmodMonsterTypeDef* type);
    
    int (__cdecl* GetTypeProperty)(AmodMod mod, const char* name, const char* key, AmodValue* value);
    int (__cdecl* SetTypeProperty)(AmodMod mod, const char* name, const char* key, const AmodValue* value);
    int (__cdecl* HasTypeProperty)(AmodMod mod, const char* name, const char* key);
    int (__cdecl* TypePropertyCount)(AmodMod mod);
    const char* (__cdecl* TypePropertyNameAt)(AmodMod mod, int index);

    /* Teach the spawner a name. */

    /* Teach spawner a name moving like one of the ten AMOD_MONSTER_MOVES_*. */
    int (__cdecl* DefineKind)(AmodMod mod, const char* name, const char* movesLike);

    int (__cdecl* DefineBehaviour)(AmodMod mod, const char* name, AmodMonsterThinkHandler handler, void* user);

    /* Live monsters in the current room. */
    int (__cdecl* GetCount)(AmodMod mod);
    int (__cdecl* Get)(AmodMod mod, int index, AmodMonster* out);
    int (__cdecl* Place)(AmodMod mod, const char* name, int column, int row);
    int (__cdecl* PlaceEx)(AmodMod mod, const AmodMonsterPlacement* where);
    int (__cdecl* Kill)(AmodMod mod, int index);
    int (__cdecl* SetMovement)(AmodMod mod, int index, const char* movesLike);
    int (__cdecl* Move)(AmodMod mod, int index, int dirX, int dirY, int dirZ);
    
    int (__cdecl* GetProperty)(AmodMod mod, int index, const char* key, AmodValue* value);
    int (__cdecl* SetProperty)(AmodMod mod, int index, const char* key, const AmodValue* value);
    int (__cdecl* HasProperty)(AmodMod mod, int index, const char* key);
    int (__cdecl* PropertyCount)(AmodMod mod);
    const char* (__cdecl* PropertyNameAt)(AmodMod mod, int index);

    /* Level kill tally (game count). */
    int (__cdecl* GetKilledCount)(AmodMod mod);

    /* Placements in any room. */
    int (__cdecl* GetPlacedCount)(AmodMod mod, int roomIndex);
    int (__cdecl* GetPlaced)(AmodMod mod, int roomIndex, int index, AmodMonster* out);
    int (__cdecl* GetPlacedProperty)(AmodMod mod, int roomIndex, int index, const char* key, AmodValue* value);
    int (__cdecl* SetPlacedProperty)(AmodMod mod, int roomIndex, int index, const char* key, const AmodValue* value);

    /* Before any death (knife, fall, mod, ...). */
    AmodToken (__cdecl* AddKillHandler)(AmodMod mod, AmodMonsterKillHandler handler, void* user);
    void (__cdecl* RemoveKillHandler)(AmodMod mod, AmodToken token);

    /* Before a monster acts on contact. */
    AmodToken (__cdecl* AddTouchHandler)(AmodMod mod, AmodMonsterTouchHandler handler, void* user);
    void (__cdecl* RemoveTouchHandler)(AmodMod mod, AmodToken token);

    /* Boxes are calls (six floats do not fit AmodValue). */
    int (__cdecl* GetTypeBox)(AmodMod mod, const char* name, int which, AmodBox* out);
    int (__cdecl* SetTypeBox)(AmodMod mod, const char* name, int which, const AmodBox* box);

    /* Live object box for that monster alone. */
    int (__cdecl* GetBox)(AmodMod mod, int index, AmodBox* out);
    int (__cdecl* SetBox)(AmodMod mod, int index, const AmodBox* box);

    /* Index lasts a frame; id lasts the monster. */
    AmodMonsterId (__cdecl* IdAt)(AmodMod mod, int index);
    int (__cdecl* IndexOf)(AmodMod mod, AmodMonsterId id);

    /* Key "name_col_row" as a level file writes it. */
    AmodMonsterId (__cdecl* Find)(AmodMod mod, const char* key);
    int (__cdecl* FindPlaced)(AmodMod mod, int roomIndex, const char* key);

    /* After build, before first move. */
    AmodToken (__cdecl* AddSpawnHandler)(AmodMod mod, AmodMonsterSpawnHandler handler, void* user);
    void (__cdecl* RemoveSpawnHandler)(AmodMod mod, AmodToken token);

    /* Monster of a draw `item` from a model draw handler. */
    AmodMonsterId (__cdecl* IdOfDraw)(AmodMod mod, uint32_t item);

    /* Live MAIN/OBJECT boxes (animation writes over them). */
    int (__cdecl* GetLiveBox)(AmodMod mod, int index, int which, AmodBox* out);
    int (__cdecl* SetLiveBox)(AmodMod mod, int index, int which, const AmodBox* box);

    /* Before a placement becomes a monster; return 0 to refuse. */
    AmodToken (__cdecl* AddPlaceHandler)(AmodMod mod, AmodMonsterPlaceHandler handler, void* user);
    void (__cdecl* RemovePlaceHandler)(AmodMod mod, AmodToken token);

    /* Spend a placement (any room) and drop its live monster; not a scored death. */
    int (__cdecl* RemovePlaced)(AmodMod mod, int roomIndex, int index);

    /* Behaviour for any name (incl. game's); per-frame also/instead of game tick. */
    int (__cdecl* SetBehaviour)(AmodMod mod, const char* name, AmodMonsterActHandler handler, void* user);
    int (__cdecl* ClearBehaviour)(AmodMod mod, const char* name);

    /* One monster by index; holds across room leave/return. */
    int (__cdecl* BorrowMonster)(AmodMod mod, int index, AmodMonsterActHandler handler, void* user);
    int (__cdecl* ReturnMonster)(AmodMod mod, int index);

    /* Register mod kind; 0 if name taken. Not in the game table. */
    int (__cdecl* RegisterKind)(AmodMod mod, const char* name, AmodMonsterKindHandler handler, void* user);

    /* Place a registered kind; roomIndex < 0 = current room. */
    int (__cdecl* PlaceKind)(AmodMod mod, int kind, int roomIndex, const AmodMonsterPlacement* where);

    /* Place/kill in any room; unentered rooms get a placement spent as a death. */
    int (__cdecl* PlaceIn)(AmodMod mod, int roomIndex, const AmodMonsterPlacement* placement);
    int (__cdecl* KillIn)(AmodMod mod, int roomIndex, int index);

    /* Play frameset by monster index (not world entity index). */
    int (__cdecl* PlayAnim)(AmodMod mod, int index, const char* frameset);

    /* What it is playing, same index; caller sets out->structSize. */
    int (__cdecl* GetAnim)(AmodMod mod, int index, AmodAnimState* out);

} AmodMonstersApi;

AMOD_LAST_FIELD(AmodMonstersApi, GetAnim, 0xE8);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Monsters
    {
        inline const AmodMonstersApi* g_monsters = nullptr;
        inline AmodMod g_monsterSelf = nullptr;

        inline void                                                 Attach(const AmodMonstersApi* api, AmodMod self)
        {
            g_monsters = api;
            g_monsterSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_monsters != nullptr;
        }

        /* A monster among the entities of the room, for anything that walks the world itself. */
        class Monster : public World::Entity
        {
        public:
            Monster() = default;
            explicit Monster(const World::Entity& entity) : World::Entity(entity) {}
        };

        inline bool                                                 IsMonster(const World::Entity& entity)
        {
            return entity.IsValid() && entity.Kind() == AMOD_ENTITY_MONSTER;
        }

        inline int                                                  Count()
        {
            return g_monsters != nullptr ? g_monsters->GetCount(g_monsterSelf) : 0;
        }

        inline bool                                                 Get(int index, AmodMonster& out)
        {
            out = AmodMonster{};
            out.structSize = sizeof(out);
            return g_monsters != nullptr && g_monsters->Get(g_monsterSelf, index, &out) != 0;
        }

        inline bool                                                 Place(const char* name, int column, int row)
        {
            return g_monsters != nullptr && g_monsters->Place != nullptr &&
                   g_monsters->Place(g_monsterSelf, name, column, row) != 0;
        }

        
        class PlacementBuilder
        {
        public:
            PlacementBuilder(const char* name, int column, int row)
            {
                m_where = AmodMonsterPlacement{};
                m_where.structSize = sizeof(m_where);
                m_where.name = name;
                m_where.column = column;
                m_where.row = row;
            }

            PlacementBuilder&                                       Span(int a, int b) { m_where.extraA = a; m_where.extraB = b; return *this; }
            PlacementBuilder&                                       Tag(const char* tag) { m_where.tag = tag; return *this; }
            PlacementBuilder&                                       Level(int level) { m_where.level = level; return *this; }
            PlacementBuilder&                                       Hard() { m_where.level = 1; return *this; }
            PlacementBuilder&                                       Facing(int facing) { m_where.facing = facing; return *this; }
            PlacementBuilder&                                       Decor() { m_where.decor = 1; return *this; }
            PlacementBuilder&                                       AttackDist(float cells) { m_where.attackDist = cells; return *this; }

            bool                                                    Put() const
            {
                return g_monsters != nullptr && g_monsters->PlaceEx != nullptr &&
                       g_monsters->PlaceEx(g_monsterSelf, &m_where) != 0;
            }

        private:
            AmodMonsterPlacement m_where;
        };

        inline PlacementBuilder                                     NewPlacement(const char* name, int column, int row)
        {
            return PlacementBuilder(name, column, row);
        }

        inline bool                                                 Kill(int index)
        {
            return g_monsters != nullptr && g_monsters->Kill(g_monsterSelf, index) != 0;
        }

        /* 0 free, 1 blocked, -1 no such monster. */
        inline int                                                  Move(int index, int dirX, int dirY = 0, int dirZ = 0)
        {
            return (g_monsters != nullptr && g_monsters->Move != nullptr)
                       ? g_monsters->Move(g_monsterSelf, index, dirX, dirY, dirZ)
                       : -1;
        }

        inline bool                                                 SetMovement(int index, const char* movesLike)
        {
            return g_monsters != nullptr && g_monsters->SetMovement != nullptr &&
                   g_monsters->SetMovement(g_monsterSelf, index, movesLike) != 0;
        }

        /* The placements of any room, whether the player is there or not. Below zero is his own. */
        inline int                                                  PlacedCount(int roomIndex = -1)
        {
            return (g_monsters != nullptr && g_monsters->GetPlacedCount != nullptr)
                       ? g_monsters->GetPlacedCount(g_monsterSelf, roomIndex)
                       : 0;
        }

        inline bool                                                 GetPlaced(int roomIndex, int index, AmodMonster& out)
        {
            out = AmodMonster{};
            out.structSize = sizeof(out);
            return g_monsters != nullptr && g_monsters->GetPlaced != nullptr &&
                   g_monsters->GetPlaced(g_monsterSelf, roomIndex, index, &out) != 0;
        }

        class PlacedRef
        {
        public:
            PlacedRef(int roomIndex, int index) : m_room(roomIndex), m_index(index) {}

            bool                                                    Set(const char* key, const AmodValue& value) const
            {
                return g_monsters != nullptr && g_monsters->SetPlacedProperty != nullptr &&
                       g_monsters->SetPlacedProperty(g_monsterSelf, m_room, m_index, key, &value) != 0;
            }

            bool                                                    Set(const char* key, int number) const { return Set(key, Amod::Value(number)); }
            bool                                                    Set(const char* key, float number) const { return Set(key, Amod::Value(number)); }

            float                                                   Number(const char* key, float fallback = 0.0f) const
            {
                AmodValue value = {};
                if (g_monsters == nullptr || g_monsters->GetPlacedProperty == nullptr ||
                    g_monsters->GetPlacedProperty(g_monsterSelf, m_room, m_index, key, &value) == 0 ||
                    value.kind != AMOD_VALUE_FLOAT || value.count < 1)
                {
                    return fallback;
                }
                return value.numbers[0];
            }

        private:
            int m_room;
            int m_index;
        };

        inline PlacedRef                                            Placed(int roomIndex, int index)
        {
            return PlacedRef(roomIndex, index);
        }

        inline int                                                  KilledCount()
        {
            return g_monsters != nullptr ? g_monsters->GetKilledCount(g_monsterSelf) : 0;
        }

        inline int                                                  TypeCount()
        {
            return g_monsters != nullptr ? g_monsters->GetTypeCount(g_monsterSelf) : 0;
        }

        inline bool                                                 GetType(int index, AmodMonsterType& out)
        {
            out = AmodMonsterType{};
            out.structSize = sizeof(out);
            return g_monsters != nullptr && g_monsters->GetType(g_monsterSelf, index, &out) != 0;
        }

        inline bool                                                 FindType(const char* name, AmodMonsterType& out)
        {
            out = AmodMonsterType{};
            out.structSize = sizeof(out);
            return g_monsters != nullptr && g_monsters->FindType(g_monsterSelf, name, &out) != 0;
        }

        /* Teach a name to move like one of the ten. */
        inline bool                                                 DefineKind(const char* name, const char* movesLike)
        {
            return g_monsters != nullptr && g_monsters->DefineKind != nullptr &&
                   g_monsters->DefineKind(g_monsterSelf, name, movesLike) != 0;
        }

        /* Own think instead of the borrowed movement. */
        inline bool                                                 DefineBehaviour(const char* name, AmodMonsterThinkHandler handler, void* user = nullptr)
        {
            return g_monsters != nullptr && g_monsters->DefineBehaviour != nullptr &&
                   g_monsters->DefineBehaviour(g_monsterSelf, name, handler, user) != 0;
        }

        /* Behaviour for any name (incl. game's); takes over already-standing too. */
        inline bool                                                 SetBehaviour(const char* name, AmodMonsterActHandler handler, void* user = nullptr)
        {
            return g_monsters != nullptr && g_monsters->SetBehaviour != nullptr &&
                   g_monsters->SetBehaviour(g_monsterSelf, name, handler, user) != 0;
        }

        /* Lambda form; captures kept in static. */
        template <typename Handler>
        inline bool                                                 SetBehaviour(const char* name, Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return SetBehaviour(name, [](const AmodMonsterThinkInfo* info, void*) -> int
            {
                return kept(info);
            }, nullptr);
        }

        /* Place in any room. */
        inline bool                                                 PlaceIn(int roomIndex, const AmodMonsterPlacement& where)
        {
            return g_monsters != nullptr && g_monsters->PlaceIn != nullptr &&
                   g_monsters->PlaceIn(g_monsterSelf, roomIndex, &where) != 0;
        }

        inline bool                                                 PlaceIn(int roomIndex, const char* name, int column, int row, const char* tag = nullptr)
        {
            AmodMonsterPlacement where = {};
            where.structSize = sizeof(where);
            where.name = name;
            where.column = column;
            where.row = row;
            where.tag = tag;
            return PlaceIn(roomIndex, where);
        }

        /* Play frameset by monster index. */
        inline bool                                                 PlayAnim(int index, const char* frameset)
        {
            return g_monsters != nullptr && g_monsters->PlayAnim != nullptr &&
                   g_monsters->PlayAnim(g_monsterSelf, index, frameset) != 0;
        }

        /* What that monster is playing right now. */
        inline bool                                                 GetAnim(int index, AmodAnimState& out)
        {
            out = AmodAnimState{};
            out.structSize = sizeof(out);
            return g_monsters != nullptr && g_monsters->GetAnim != nullptr &&
                   g_monsters->GetAnim(g_monsterSelf, index, &out) != 0;
        }

        /* Kill in any room; unentered rooms spend the placement. */
        inline bool                                                 KillIn(int roomIndex, int index)
        {
            return g_monsters != nullptr && g_monsters->KillIn != nullptr &&
                   g_monsters->KillIn(g_monsterSelf, roomIndex, index) != 0;
        }

        /* Register mod kind; 0 if name taken. */
        inline int                                                  RegisterKind(const char* name, AmodMonsterKindHandler handler, void* user = nullptr)
        {
            return (g_monsters != nullptr && g_monsters->RegisterKind != nullptr)
                ? g_monsters->RegisterKind(g_monsterSelf, name, handler, user) : 0;
        }

        /* Lambda form; captures kept in static. */
        template <typename Handler>
        inline int                                                  RegisterKind(const char* name, Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return RegisterKind(name, [](const AmodMonsterKindInfo* info, void*) -> int
            {
                return kept(info);
            }, nullptr);
        }

        inline bool                                                 PlaceKind(int kind, const AmodMonsterPlacement& where, int roomIndex = -1)
        {
            return g_monsters != nullptr && g_monsters->PlaceKind != nullptr &&
                   g_monsters->PlaceKind(g_monsterSelf, kind, roomIndex, &where) != 0;
        }

        inline bool                                                 PlaceKind(int kind, int column, int row, const char* tag = nullptr, int roomIndex = -1)
        {
            AmodMonsterPlacement where = {};
            where.structSize = sizeof(where);
            where.column = column;
            where.row = row;
            where.tag = tag;
            return PlaceKind(kind, where, roomIndex);
        }

        /* Drive this one monster; outranks name-wide behaviour. */
        inline bool                                                 Borrow(int index, AmodMonsterActHandler handler, void* user = nullptr)
        {
            return g_monsters != nullptr && g_monsters->BorrowMonster != nullptr &&
                   g_monsters->BorrowMonster(g_monsterSelf, index, handler, user) != 0;
        }

        /* Lambda form; captures kept in static. */
        template <typename Handler>
        inline bool                                                 Borrow(int index, Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return Borrow(index, [](const AmodMonsterThinkInfo* info, void*) -> int
            {
                return kept(info);
            }, nullptr);
        }

        inline bool                                                 Return(int index)
        {
            return g_monsters != nullptr && g_monsters->ReturnMonster != nullptr &&
                   g_monsters->ReturnMonster(g_monsterSelf, index) != 0;
        }

        /* Hand behaviour back to the game. */
        inline bool                                                 ClearBehaviour(const char* name)
        {
            return g_monsters != nullptr && g_monsters->ClearBehaviour != nullptr &&
                   g_monsters->ClearBehaviour(g_monsterSelf, name) != 0;
        }

        /* Before placement becomes a monster; return 0 to refuse. */
        inline AmodToken                                            OnPlace(AmodMonsterPlaceHandler handler, void* user = nullptr)
        {
            return (g_monsters != nullptr && g_monsters->AddPlaceHandler != nullptr)
                ? g_monsters->AddPlaceHandler(g_monsterSelf, handler, user) : AMOD_INVALID_TOKEN;
        }

        /* Lambda form; captures kept in static. */
        template <typename Handler>
        inline AmodToken                                            OnPlace(Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return OnPlace([](const AmodMonsterPlaceInfo* info, void*) -> int
            {
                return kept(info);
            }, nullptr);
        }

        inline void                                                 RemoveOnPlace(AmodToken token)
        {
            if (g_monsters != nullptr && g_monsters->RemovePlaceHandler != nullptr)
            {
                g_monsters->RemovePlaceHandler(g_monsterSelf, token);
            }
        }

        /* Spend placement (any room); not a scored death. */
        inline bool                                                 RemovePlaced(int index, int roomIndex = -1)
        {
            return g_monsters != nullptr && g_monsters->RemovePlaced != nullptr &&
                   g_monsters->RemovePlaced(g_monsterSelf, roomIndex, index) != 0;
        }

        /* Remove every placement in that room. */
        inline int                                                  RemoveAllPlaced(int roomIndex = -1)
        {
            const int held = (g_monsters != nullptr && g_monsters->GetPlacedCount != nullptr)
                ? g_monsters->GetPlacedCount(g_monsterSelf, roomIndex) : 0;
            int gone = 0;

            for (int at = 0; at < held; ++at)
            {
                if (RemovePlaced(at, roomIndex))
                {
                    ++gone;
                }
            }
            return gone;
        }

        inline bool                                                 GetTypeBox(const char* name, int which, AmodBox& out)
        {
            out = AmodBox{};
            out.structSize = sizeof(out);
            return g_monsters != nullptr && g_monsters->GetTypeBox != nullptr && g_monsters->GetTypeBox(g_monsterSelf, name, which, &out) != 0;
        }

        inline bool                                                 SetTypeBox(const char* name, int which, const AmodBox& box)
        {
            return g_monsters != nullptr && g_monsters->SetTypeBox != nullptr && g_monsters->SetTypeBox(g_monsterSelf, name, which, &box) != 0;
        }

        inline bool                                                 GetBox(int index, AmodBox& out, int which = AMOD_MONSTER_BOX_OBJECT)
        {
            out = AmodBox{};
            out.structSize = sizeof(out);
            return g_monsters != nullptr && g_monsters->GetLiveBox != nullptr && g_monsters->GetLiveBox(g_monsterSelf, index, which, &out) != 0;
        }

        inline bool                                                 SetBox(int index, const AmodBox& box, int which = AMOD_MONSTER_BOX_OBJECT)
        {
            return g_monsters != nullptr && g_monsters->SetLiveBox != nullptr && g_monsters->SetLiveBox(g_monsterSelf, index, which, &box) != 0;
        }

        inline AmodToken                                            OnKill(AmodMonsterKillHandler handler, void* user = nullptr)
        {
            return (g_monsters != nullptr && g_monsters->AddKillHandler != nullptr)
                       ? g_monsters->AddKillHandler(g_monsterSelf, handler, user)
                       : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnTouch(AmodMonsterTouchHandler handler, void* user = nullptr)
        {
            return (g_monsters != nullptr && g_monsters->AddTouchHandler != nullptr)
                       ? g_monsters->AddTouchHandler(g_monsterSelf, handler, user)
                       : AMOD_INVALID_TOKEN;
        }

        /* Inside a model draw handler: which monster this draw is. */
        inline AmodMonsterId                                        IdOfDraw(uint32_t item)
        {
            return (g_monsters != nullptr && g_monsters->IdOfDraw != nullptr) ? g_monsters->IdOfDraw(g_monsterSelf, item) : AMOD_MONSTER_NONE;
        }

        /* After each spawn when a room is entered. */
        inline AmodToken                                            OnSpawn(AmodMonsterSpawnHandler handler, void* user = nullptr)
        {
            return (g_monsters != nullptr && g_monsters->AddSpawnHandler != nullptr)
                       ? g_monsters->AddSpawnHandler(g_monsterSelf, handler, user)
                       : AMOD_INVALID_TOKEN;
        }

        
        class TypeBuilder
        {
        public:
            TypeBuilder(const char* name, const char* basedOn)
            {
                m_def = AmodMonsterTypeDef{};
                m_def.structSize = sizeof(m_def);
                m_def.name = name;
                m_def.basedOn = basedOn;
            }

            TypeBuilder&                                            Texture(const char* easy, const char* hard = nullptr)
            {
                m_def.texEasy = easy;
                m_def.texHard = hard != nullptr ? hard : easy;
                return *this;
            }

            TypeBuilder&                                            Model(const char* file) { m_def.model = file; return *this; }
            TypeBuilder&                                            Speed(float speed) { m_def.speed = speed; return *this; }
            TypeBuilder&                                            SpeedUp(float speed) { m_def.speedUp = speed; return *this; }
            TypeBuilder&                                            VertSpeed(float speed) { m_def.vertSpeed = speed; return *this; }

            bool                                                    Define() const
            {
                return g_monsters != nullptr && g_monsters->DefineType != nullptr &&
                       g_monsters->DefineType(g_monsterSelf, &m_def) != 0;
            }

        private:
            AmodMonsterTypeDef m_def;
        };

        inline TypeBuilder                                          NewType(const char* name, const char* basedOn)
        {
            return TypeBuilder(name, basedOn);
        }

        
        class Property
        {
        public:
            Property(const char* name, int index, const char* key)
                : m_name(name)
                , m_index(index)
                , m_key(key)
            {
            }

            bool                                                    Read(AmodValue& value) const
            {
                if (g_monsters == nullptr)
                {
                    return false;
                }
                return m_name != nullptr
                           ? g_monsters->GetTypeProperty(g_monsterSelf, m_name, m_key, &value) != 0
                           : g_monsters->GetProperty(g_monsterSelf, m_index, m_key, &value) != 0;
            }

            bool                                                    Set(const AmodValue& value) const
            {
                if (g_monsters == nullptr)
                {
                    return false;
                }
                return m_name != nullptr
                           ? g_monsters->SetTypeProperty(g_monsterSelf, m_name, m_key, &value) != 0
                           : g_monsters->SetProperty(g_monsterSelf, m_index, m_key, &value) != 0;
            }

            bool                                                    Set(int number) const { return Set(Amod::Value(number)); }
            bool                                                    Set(float a) const { return Set(Amod::Value(a)); }
            bool                                                    Set(const char* text) const { return Set(Amod::Value(text)); }
            bool                                                    Set(float a, float b, float c, float d) const { return Set(Amod::Value(a, b, c, d)); }

            int                                                     Integer(int fallback = 0) const
            {
                AmodValue value = {};
                if (!Read(value) || (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL))
                {
                    return fallback;
                }
                return value.integer;
            }

            float                                                   Number(int index = 0, float fallback = 0.0f) const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_FLOAT ||
                    index < 0 || static_cast<uint32_t>(index) >= value.count)
                {
                    return fallback;
                }
                return value.numbers[index];
            }

            const char*                                             Text(const char* fallback = "") const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_STRING || value.text == nullptr)
                {
                    return fallback;
                }
                return value.text;
            }

        private:
            const char* m_name;
            int m_index;
            const char* m_key;
        };

        class TypeRef
        {
        public:
            explicit TypeRef(const char* name) : m_name(name) {}
            Property                                                operator[](const char* key) const { return Property(m_name, -1, key); }

        private:
            const char* m_name;
        };

        class MonsterRef
        {
        public:
            explicit MonsterRef(int index) : m_index(index) {}
            Property                                                operator[](const char* key) const { return Property(nullptr, m_index, key); }

        private:
            int m_index;
        };

        inline TypeRef                                              Type(const char* name) { return TypeRef(name); }
        inline MonsterRef                                           At(int index) { return MonsterRef(index); }

        inline AmodMonsterId                                        IdAt(int index)
        {
            return (g_monsters != nullptr && g_monsters->IdAt != nullptr) ? g_monsters->IdAt(g_monsterSelf, index) : AMOD_MONSTER_NONE;
        }

        inline int                                                  IndexOf(AmodMonsterId id)
        {
            return (g_monsters != nullptr && g_monsters->IndexOf != nullptr) ? g_monsters->IndexOf(g_monsterSelf, id) : -1;
        }

        /* Held by id; Index() resolves the current live index. */
        class Tracked
        {
        public:
            Tracked() = default;
            explicit Tracked(AmodMonsterId id) : m_id(id) {}

            AmodMonsterId                                           Id() const { return m_id; }
            int                                                     Index() const { return IndexOf(m_id); }
            bool                                                    Alive() const { return Index() >= 0; }

            Property                                                operator[](const char* key) const { return Property(nullptr, Index(), key); }

            bool                                                    Get(AmodMonster& out) const { return Monsters::Get(Index(), out); }
            bool                                                    Kill() const { return Monsters::Kill(Index()); }
            int                                                     Move(int dirX, int dirY = 0, int dirZ = 0) const { return Monsters::Move(Index(), dirX, dirY, dirZ); }

        private:
            AmodMonsterId m_id = AMOD_MONSTER_NONE;
        };

        inline Tracked                                              Track(int index) { return Tracked(IdAt(index)); }
        inline Tracked                                              ById(AmodMonsterId id) { return Tracked(id); }

        /* "name_col_row" as a level file writes it. */
        inline Tracked                                              Find(const char* key)
        {
            return Tracked((g_monsters != nullptr && g_monsters->Find != nullptr) ? g_monsters->Find(g_monsterSelf, key) : AMOD_MONSTER_NONE);
        }

        /* Placement index in the room list. */
        inline int                                                  FindPlaced(const char* key, int roomIndex = -1)
        {
            return (g_monsters != nullptr && g_monsters->FindPlaced != nullptr) ? g_monsters->FindPlaced(g_monsterSelf, roomIndex, key) : -1;
        }

        inline int                                                  CountNamed(const char* name)
        {
            const int total = Count();
            int found = 0;

            for (int index = 0; index < total; ++index)
            {
                AmodMonster monster;
                if (Get(index, monster) && monster.name != nullptr && name != nullptr)
                {
                    const char* mine = monster.name;
                    const char* theirs = name;
                    while (*mine != '\0' && *theirs != '\0' && *mine == *theirs)
                    {
                        ++mine;
                        ++theirs;
                    }
                    found += (*mine == *theirs) ? 1 : 0;
                }
            }
            return found;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_MONSTERS_API_H */
