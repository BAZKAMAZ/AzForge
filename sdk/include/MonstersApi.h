#ifndef AZFORGE_MONSTERS_API_H
#define AZFORGE_MONSTERS_API_H

#include "WorldApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The ten kinds of movement the game has. A defined kind borrows one of them. */
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

/* What a walking monster is doing */
#define AMOD_MONSTER_STATE_MOVE                                     0
#define AMOD_MONSTER_STATE_SIT                                      1

/* One definition out of monster_res.txt. */
typedef struct AmodMonsterType
{
    uint32_t structSize;

    const char* name;           /* $name, what a placement is matched by */

    float speed;                /* $speed, doubled for a hard $level */
    float speedUp;              /* $speed_up */
    float vertSpeed;            /* $vert_speed */

    float partColor[4];         /* $part_color, what the death blast is painted with */
    float partColorHard[4];     /* $part_color_hard, for $level 1 and 3 */

    /* New fields are appended, never inserted. */
} AmodMonsterType;

/* A monster defined by a mod, copied from one the game already knows */
typedef struct AmodMonsterTypeDef
{
    uint32_t structSize;

    const char* name;           /* what levels will name it, and what DefineKind matches */
    const char* basedOn;        /* a $name the game already knows; its record is the starting point */

    const char* texEasy;
    const char* texHard;
    const char* model;

    float speed;                /* zero leaves the base's */
    float speedUp;
    float vertSpeed;

    /* New fields are appended, never inserted. */
} AmodMonsterTypeDef;

/* One monster standing in the room the player is in. */
typedef struct AmodMonster
{
    uint32_t structSize;

    const char* name;           /* the definition it came from */
    const char* tag;            /* $tag, the level author's own label */

    
    const char* moves;

    float position[3];
    int level;                  /* $level; 1 and 3 are the hard variants */
    int facing;                 /* -1 or 1 */
    float speed;                /* what it is walking at now, not what the placement was given */

    /* The two-state cycle belongs to the mummy pair alone */
    int hasState;
    int state;                  /* AMOD_MONSTER_STATE_* */
    float timeLeft;             /* until the state changes */

    int isHard;                 /* $level 1 or 3 */
    int isDecor;                /* $decor: counts for nothing when killed */

    /* New fields are appended, never inserted. */
} AmodMonster;

/* Where to put one, in the shape a level script writes it */
typedef struct AmodMonsterPlacement
{
    uint32_t structSize;

    const char* name;
    int column;
    int row;
    int extraA;                 /* $pos[2] */
    int extraB;                 /* $pos[3] */

    const char* tag;
    int level;                  /* 1 and 3 are the hard variants */
    int facing;                 /* $dir, -1 or 1; zero means 1 */
    int decor;                  /* counts for nothing when killed */
    float attackDist;           /* in cells; zero takes the game's own 5 */

    /* New fields are appended, never inserted. */
} AmodMonsterPlacement;

typedef enum AmodMonsterKillResult
{
    AMOD_MONSTER_KILL_DEFAULT = 0,  /* the game does with it what it always does */
    AMOD_MONSTER_KILL_HANDLED = 1,  /* counted as dealt with; the game's own death is skipped */
    AMOD_MONSTER_KILL_REFUSE = 2    /* it lives - whatever hit it does not */
} AmodMonsterKillResult;

typedef struct AmodMonsterKillInfo
{
    uint32_t structSize;

    const char* name;
    const char* tag;
    float position[3];
    int level;
    int isDecor;

    /* New fields are appended, never inserted. */
} AmodMonsterKillInfo;

typedef AmodMonsterKillResult (__cdecl* AmodMonsterKillHandler)(const AmodMonsterKillInfo* info,
                                                                void* user);

/* One frame of one monster whose behaviour a mod took over. */
typedef struct AmodMonsterThinkInfo
{
    uint32_t structSize;

    int index;                  /* what Get and the properties address it by, this frame */
    float delta;                /* seconds since the last frame, as the game gave it */

    float position[3];
    int facing;

    /* New fields are appended, never inserted. */
} AmodMonsterThinkInfo;

typedef void (__cdecl* AmodMonsterThinkHandler)(const AmodMonsterThinkInfo* info, void* user);

typedef enum AmodMonsterTouchResult
{
    AMOD_MONSTER_TOUCH_DEFAULT = 0, /* the game's own rules: totem, knife, or somebody dies */
    AMOD_MONSTER_TOUCH_HANDLED = 1, /* the mod dealt with it; none of those rules run */
    AMOD_MONSTER_TOUCH_REFUSE = 2   /* nothing happens at all, as though they had not met */
} AmodMonsterTouchResult;

/* A monster meeting something */
typedef struct AmodMonsterTouchInfo
{
    uint32_t structSize;

    int index;                  /* the monster, as Get and the properties address it */
    const char* name;
    const char* tag;
    float position[3];

    int isPlayer;               /* whether what it met is the player */
    int toucherKind;            /* and what that thing is, if not */
    int side;                   /* 1..4, which way they met; 3 and 4 are what turn a monster */

    /* New fields are appended, never inserted. */
} AmodMonsterTouchInfo;

typedef AmodMonsterTouchResult (__cdecl* AmodMonsterTouchHandler)(const AmodMonsterTouchInfo* info,
                                                                  void* user);

typedef struct AmodMonstersApi
{
    uint32_t structSize;

    /* --- the definitions monster_res.txt was read into --------------------------------- */

    int (__cdecl* GetTypeCount)(AmodMod mod);
    int (__cdecl* GetType)(AmodMod mod, int index, AmodMonsterType* out);
    int (__cdecl* FindType)(AmodMod mod, const char* name, AmodMonsterType* out);

    /* Appends a definition copied from one the game knows */
    int (__cdecl* DefineType)(AmodMod mod, const AmodMonsterTypeDef* type);

    
    int (__cdecl* GetTypeProperty)(AmodMod mod, const char* name, const char* key, AmodValue* value);
    int (__cdecl* SetTypeProperty)(AmodMod mod, const char* name, const char* key, const AmodValue* value);
    int (__cdecl* HasTypeProperty)(AmodMod mod, const char* name, const char* key);
    int (__cdecl* TypePropertyCount)(AmodMod mod);
    const char* (__cdecl* TypePropertyNameAt)(AmodMod mod, int index);

    /* --- making a name spawn at all ---------------------------------------------------- */

    /* Teaches the spawner a name of the mod's own, moving like one of the ten AMOD_MONSTER_MOVES_* * kinds */
    int (__cdecl* DefineKind)(AmodMod mod, const char* name, const char* movesLike);

    
    int (__cdecl* DefineBehaviour)(AmodMod mod, const char* name, AmodMonsterThinkHandler handler,
                                   void* user);

    /* --- the monsters standing in the room --------------------------------------------- */

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

    /* How many monsters this level has lost so far, the game's own count. */
    int (__cdecl* GetKilledCount)(AmodMod mod);

    /* --- the rooms the player is not in ------------------------------------------------ */

    
    int (__cdecl* GetPlacedCount)(AmodMod mod, int roomIndex);
    int (__cdecl* GetPlaced)(AmodMod mod, int roomIndex, int index, AmodMonster* out);
    int (__cdecl* GetPlacedProperty)(AmodMod mod, int roomIndex, int index, const char* key, AmodValue* value);
    int (__cdecl* SetPlacedProperty)(AmodMod mod, int roomIndex, int index, const char* key, const AmodValue* value);

    /* --- being asked ------------------------------------------------------------------- */

    /* Asked before any monster dies, whatever killed it - the player's knife, a fall, a mod */
    AmodToken (__cdecl* AddKillHandler)(AmodMod mod, AmodMonsterKillHandler handler, void* user);
    void (__cdecl* RemoveKillHandler)(AmodMod mod, AmodToken token);

    /* Asked before a monster acts on what it has walked into, for every kind of monster there is */
    AmodToken (__cdecl* AddTouchHandler)(AmodMod mod, AmodMonsterTouchHandler handler, void* user);
    void (__cdecl* RemoveTouchHandler)(AmodMod mod, AmodToken token);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodMonstersApi;

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

        /* Which of the ten a name of the mod's own moves like. */
        inline bool                                                 DefineKind(const char* name, const char* movesLike)
        {
            return g_monsters != nullptr && g_monsters->DefineKind != nullptr &&
                   g_monsters->DefineKind(g_monsterSelf, name, movesLike) != 0;
        }

        /* And how it behaves, instead of what it borrowed. */
        inline bool                                                 DefineBehaviour(const char* name, AmodMonsterThinkHandler handler,
                                                                                    void* user = nullptr)
        {
            return g_monsters != nullptr && g_monsters->DefineBehaviour != nullptr &&
                   g_monsters->DefineBehaviour(g_monsterSelf, name, handler, user) != 0;
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
