#ifndef AZFORGE_BONUSES_API_H
#define AZFORGE_BONUSES_API_H

#include "WorldApi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodBonusType
{
    uint32_t structSize;

    const char* name;           /* what a level places it by */
    const char* resourceId;     /* $res_id, the same as the name unless the script said otherwise */

    int pointsOnTake;           /* $pts_take - what taking it is worth */
    int pointsOnRunOut;         /* $pts_out - what is left when its timer runs out */

    /* $time: how long a totem or a torch lasts. A placement can override it with its own. */
    float time;
    float burnTime;             /* $burn_time, a torch's own */

    /* A totem, a torch, and whether taking it starts a train of bonuses behind the player. */
    int isTotem;
    int isTorch;
    int makesTrain;
    int trainIndex;             /* $train_idx, which train it belongs to */

    float glowColor[4];
    float lightColor[3];
    float lightRadius;
    float bonusLightRadius;

    /* New fields are appended, never inserted. */
} AmodBonusType;

AMOD_LAST_FIELD(AmodBonusType, bonusLightRadius, 0x4C);

typedef enum AmodBonusTimer
{
    AMOD_BONUS_TIMER_NONE = 0,  /* sits in the slot until something takes it out */
    AMOD_BONUS_TIMER_GAME = 1,  /* the game counts down and clears the slot, as a totem's */
    AMOD_BONUS_TIMER_MOD = 2    /* the ring is drawn, nothing counts down but the mod */
} AmodBonusTimer;

/* Which of the numbers in AmodBonusTypeDef to apply */
#define AMOD_BONUS_FIELD_POINTS_TAKE                                0x00000001
#define AMOD_BONUS_FIELD_POINTS_OUT                                 0x00000002
#define AMOD_BONUS_FIELD_TIME                                       0x00000004
#define AMOD_BONUS_FIELD_TIMER                                      0x00000008
#define AMOD_BONUS_FIELD_TRAIN                                      0x00000010
#define AMOD_BONUS_FIELD_GLOW                                       0x00000020
#define AMOD_BONUS_FIELD_GLOW2                                      0x00000040
#define AMOD_BONUS_FIELD_ROCKING                                    0x00000080
#define AMOD_BONUS_FIELD_LIGHT                                      0x00000100

#define AMOD_BONUS_LIKE_SCORED                                      "bonus"
#define AMOD_BONUS_LIKE_MEGA                                        "mega_bonus"
#define AMOD_BONUS_LIKE_LIFE                                        "life"
#define AMOD_BONUS_LIKE_SILENT                                      "sys"
#define AMOD_BONUS_LIKE_QUEST                                       "quest"

typedef struct AmodBonusTypeDef
{
    uint32_t structSize;
    uint32_t fields;            /* AMOD_BONUS_FIELD_*, for the numbers below */

    const char* resourceId;     /* required, and new */
    const char* basedOn;        /* required: the type every unnamed field comes from */

    /* What taking it does - one of AMOD_BONUS_LIKE_*, or any other name for a bonus that is * carried a... */
    const char* behavesAs;

    /* Each null keeps what basedOn had. A name the game has no resource for is refused rather * than left at nothing. */
    const char* model;
    const char* texture;
    const char* icon;           /* what the HUD draws while it is carried */
    const char* soundOnTake;
    const char* soundOnUse;     /* also the tick in the last six seconds of a timer */

    int pointsOnTake;
    int pointsOnRunOut;
    float time;                 /* how long it lasts once carried */
    AmodBonusTimer timer;

    int makesTrain;
    int trainIndex;

    float glowColor[4];
    float glowColor2[4];

    /* How it moves where it lies: the swing is amp along axis at speed */
    float rockingAmp;
    float rockingSpeed;
    float rockingAxis[3];

    float lightColor[3];
    float lightRadius;

    /* New fields are appended, never inserted. */
} AmodBonusTypeDef;

AMOD_LAST_FIELD(AmodBonusTypeDef, lightRadius, 0x80);

/* Where a bonus goes and what that particular one is, as against what its type is. */
typedef struct AmodBonusPlacement
{
    uint32_t structSize;

    const char* name;
    int column;
    int row;

    float time;                 /* > 0 overrides the type's, the way a room file's $time does */
    const char* tag;            /* null for none; a handler is told it, so it can tell one from another */
    int decor;                  /* counts towards nothing and is not what a level waits for */

    /* New fields are appended, never inserted. */
} AmodBonusPlacement;

AMOD_LAST_FIELD(AmodBonusPlacement, decor, 0x18);

/* What a handler says should happen to the bonus it was told about. */
typedef enum AmodBonusTakeResult
{
    AMOD_BONUS_TAKE_DEFAULT = 0,    /* the game does with it what it always does */
    AMOD_BONUS_TAKE_HANDLED = 1,    /* taken, and the game's own path skipped entirely */
    AMOD_BONUS_TAKE_REFUSE = 2      /* left lying there, as though it had not been touched */
} AmodBonusTakeResult;

typedef struct AmodBonusTakeInfo
{
    uint32_t structSize;

    const char* name;           /* the type's name, which is what the game itself branches on */
    const char* resourceId;     /* which bonus this is - what a room file placed */
    const char* tag;            /* the placement's $tag, "" when it has none */

    int column;
    int row;
    float position[3];

    int pointsOnTake;           /* what the type is worth, so a handler need not look it up */
    int isDecor;

    /* New fields are appended, never inserted. */
} AmodBonusTakeInfo;

AMOD_LAST_FIELD(AmodBonusTakeInfo, isDecor, 0x28);

/* Called when the player touches a bonus, before the game has done anything about it */
typedef AmodBonusTakeResult (__cdecl* AmodBonusTakeHandler)(const AmodBonusTakeInfo* info, void* user);

/* The bonus table: every kind of bonus the game knows, whether or not this level places one. * Reached through AmodApi::bonuses. */
typedef struct AmodBonusesApi
{
    uint32_t structSize;

    int (__cdecl* GetTypeCount)(AmodMod mod);
    int (__cdecl* GetType)(AmodMod mod, int index, AmodBonusType* out);

    /* The lookup a placement's name goes through. Non-zero when the name is one the game knows. */
    int (__cdecl* FindType)(AmodMod mod, const char* name, AmodBonusType* out);

    
    int (__cdecl* Place)(AmodMod mod, const char* name, int column, int row);

    
    int (__cdecl* DefineType)(AmodMod mod, const AmodBonusTypeDef* def);

    /* Place() with the things a room file can say about one placement. */
    int (__cdecl* PlaceEx)(AmodMod mod, const AmodBonusPlacement* placement);

    AmodToken (__cdecl* AddTakeHandler)(AmodMod mod, AmodBonusTakeHandler handler, void* user);
    void (__cdecl* RemoveTakeHandler)(AmodMod mod, AmodToken token);

    /* Scores through the game's own routine, so the number floats up where a bonus's would and the * ex... */
    int (__cdecl* AwardPoints)(AmodMod mod, int points, const float* position);

    
    int (__cdecl* TakeChainLink)(AmodMod mod, int points, const float* position);

    
    int (__cdecl* SetTexture)(AmodMod mod, const char* resourceId, const char* file);

    /* The icon it shows once carried */
    int (__cdecl* SetIcon)(AmodMod mod, const char* resourceId, const char* file);

    
    int (__cdecl* GetTypeProperty)(AmodMod mod, const char* resourceId, const char* key, AmodValue* value);
    int (__cdecl* SetTypeProperty)(AmodMod mod, const char* resourceId, const char* key, const AmodValue* value);
    int (__cdecl* HasTypeProperty)(AmodMod mod, const char* resourceId, const char* key);
    int (__cdecl* TypePropertyCount)(AmodMod mod);
    const char* (__cdecl* TypePropertyNameAt)(AmodMod mod, int index);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodBonusesApi;

AMOD_LAST_FIELD(AmodBonusesApi, TypePropertyNameAt, 0x44);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Bonuses
    {
        inline const AmodBonusesApi* g_bonuses = nullptr;
        inline AmodMod g_bonusSelf = nullptr;

        inline void                                                 Attach(const AmodBonusesApi* api, AmodMod self)
        {
            g_bonuses = api;
            g_bonusSelf = self;
        }

        inline int                                                  TypeCount()
        {
            return g_bonuses != nullptr ? g_bonuses->GetTypeCount(g_bonusSelf) : 0;
        }

        inline bool                                                 GetType(int index, AmodBonusType& out)
        {
            out = AmodBonusType{};
            out.structSize = sizeof(AmodBonusType);
            return g_bonuses != nullptr && g_bonuses->GetType(g_bonusSelf, index, &out) != 0;
        }

        inline bool                                                 FindType(const char* name, AmodBonusType& out)
        {
            out = AmodBonusType{};
            out.structSize = sizeof(AmodBonusType);
            return g_bonuses != nullptr && g_bonuses->FindType(g_bonusSelf, name, &out) != 0;
        }

        /* Puts one on a cell of the room the player is in. False when the game knows no type of * that name, or the cell is off the grid. */
        inline bool                                                 Place(const char* name, int column, int row)
        {
            return g_bonuses != nullptr && g_bonuses->Place(g_bonusSelf, name, column, row) != 0;
        }

        /* Places one and says more about it than its cell: its own timer, its tag, whether it * counts. Build it, set what matters, place it. */
        class NewPlacement
        {
        public:
            NewPlacement(const char* name, int column, int row)
            {
                m_data = AmodBonusPlacement{};
                m_data.structSize = sizeof(m_data);
                m_data.name = name;
                m_data.column = column;
                m_data.row = row;
            }

            NewPlacement&                                           Time(float seconds) { m_data.time = seconds; return *this; }
            NewPlacement&                                           Tag(const char* tag) { m_data.tag = tag; return *this; }
            NewPlacement&                                           Decor(bool decor = true) { m_data.decor = decor ? 1 : 0; return *this; }

            bool                                                    Put() const
            {
                return g_bonuses != nullptr && g_bonuses->PlaceEx != nullptr &&
                       g_bonuses->PlaceEx(g_bonusSelf, &m_data) != 0;
            }

        private:
            AmodBonusPlacement m_data;
        };

        
        class NewType
        {
        public:
            NewType(const char* resourceId, const char* basedOn)
            {
                m_data = AmodBonusTypeDef{};
                m_data.structSize = sizeof(m_data);
                m_data.resourceId = resourceId;
                m_data.basedOn = basedOn;
            }

            /* One of AMOD_BONUS_LIKE_*, or a name of the mod's own for a carried bonus it will * handle itself */
            NewType&                                                Behaves(const char* behavesAs)
            {
                m_data.behavesAs = behavesAs;
                return *this;
            }

            NewType&                                                Model(const char* file) { m_data.model = file; return *this; }
            NewType&                                                Texture(const char* file) { m_data.texture = file; return *this; }
            NewType&                                                Icon(const char* file) { m_data.icon = file; return *this; }
            NewType&                                                SoundOnTake(const char* file) { m_data.soundOnTake = file; return *this; }
            NewType&                                                SoundOnUse(const char* file) { m_data.soundOnUse = file; return *this; }

            NewType&                                                PointsOnTake(int points)
            {
                m_data.pointsOnTake = points;
                m_data.fields |= AMOD_BONUS_FIELD_POINTS_TAKE;
                return *this;
            }

            NewType&                                                PointsOnRunOut(int points)
            {
                m_data.pointsOnRunOut = points;
                m_data.fields |= AMOD_BONUS_FIELD_POINTS_OUT;
                return *this;
            }

            /* How long it lasts once it is carried, and who counts it down. */
            NewType&                                                Timer(float seconds, AmodBonusTimer who = AMOD_BONUS_TIMER_GAME)
            {
                m_data.time = seconds;
                m_data.timer = who;
                m_data.fields |= AMOD_BONUS_FIELD_TIME | AMOD_BONUS_FIELD_TIMER;
                return *this;
            }

            NewType&                                                Train(bool makes, int index = 0)
            {
                m_data.makesTrain = makes ? 1 : 0;
                m_data.trainIndex = index;
                m_data.fields |= AMOD_BONUS_FIELD_TRAIN;
                return *this;
            }

            NewType&                                                Glow(float r, float g, float b, float a = 1.0f)
            {
                m_data.glowColor[0] = r;
                m_data.glowColor[1] = g;
                m_data.glowColor[2] = b;
                m_data.glowColor[3] = a;
                m_data.fields |= AMOD_BONUS_FIELD_GLOW;
                return *this;
            }

            NewType&                                                Glow2(float r, float g, float b, float a = 1.0f)
            {
                m_data.glowColor2[0] = r;
                m_data.glowColor2[1] = g;
                m_data.glowColor2[2] = b;
                m_data.glowColor2[3] = a;
                m_data.fields |= AMOD_BONUS_FIELD_GLOW2;
                return *this;
            }

            /* axis 0 0 1 rises and falls; the game's own bonuses use 1 1 0 and sway. */
            NewType&                                                Rocking(float amp, float speed, float axisX, float axisY, float axisZ)
            {
                m_data.rockingAmp = amp;
                m_data.rockingSpeed = speed;
                m_data.rockingAxis[0] = axisX;
                m_data.rockingAxis[1] = axisY;
                m_data.rockingAxis[2] = axisZ;
                m_data.fields |= AMOD_BONUS_FIELD_ROCKING;
                return *this;
            }

            NewType&                                                Light(float r, float g, float b, float radius)
            {
                m_data.lightColor[0] = r;
                m_data.lightColor[1] = g;
                m_data.lightColor[2] = b;
                m_data.lightRadius = radius;
                m_data.fields |= AMOD_BONUS_FIELD_LIGHT;
                return *this;
            }

            
            bool                                                    Define() const
            {
                return g_bonuses != nullptr && g_bonuses->DefineType != nullptr &&
                       g_bonuses->DefineType(g_bonusSelf, &m_data) != 0;
            }

        private:
            AmodBonusTypeDef m_data;
        };

        inline AmodToken                                            OnTake(AmodBonusTakeHandler handler, void* user = nullptr)
        {
            return (g_bonuses != nullptr && g_bonuses->AddTakeHandler != nullptr)
                       ? g_bonuses->AddTakeHandler(g_bonusSelf, handler, user)
                       : AMOD_INVALID_TOKEN;
        }

        inline void                                                 RemoveOnTake(AmodToken token)
        {
            if (g_bonuses != nullptr && g_bonuses->RemoveTakeHandler != nullptr)
            {
                g_bonuses->RemoveTakeHandler(g_bonusSelf, token);
            }
        }

        /* Scores the way a bonus does, floating number and threshold life included. */
        inline bool                                                 AwardPoints(int points)
        {
            return g_bonuses != nullptr && g_bonuses->AwardPoints != nullptr &&
                   g_bonuses->AwardPoints(g_bonusSelf, points, nullptr) != 0;
        }

        
        inline int                                                  TakeChainLink(int points, const float* position = nullptr)
        {
            return (g_bonuses != nullptr && g_bonuses->TakeChainLink != nullptr)
                       ? g_bonuses->TakeChainLink(g_bonusSelf, points, position)
                       : 0;
        }

        
        class TypeProperty
        {
        public:
            TypeProperty(const char* resourceId, const char* key)
                : m_id(resourceId)
                , m_key(key)
            {
            }

            bool                                                    Exists() const
            {
                return g_bonuses != nullptr && g_bonuses->HasTypeProperty != nullptr &&
                       g_bonuses->HasTypeProperty(g_bonusSelf, m_id, m_key) != 0;
            }

            bool                                                    Read(AmodValue& value) const
            {
                return g_bonuses != nullptr && g_bonuses->GetTypeProperty != nullptr &&
                       g_bonuses->GetTypeProperty(g_bonusSelf, m_id, m_key, &value) != 0;
            }

            /* Each answers the fallback when the key is absent or holds another shape, which * Exists tells apart from a genuine zero. */
            int                                                     Integer(int fallback = 0) const
            {
                AmodValue value = {};
                if (!Read(value) ||
                    (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL))
                {
                    return fallback;
                }
                return value.integer;
            }

            bool                                                    Boolean(bool fallback = false) const
            {
                return Integer(fallback ? 1 : 0) != 0;
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

            bool                                                    Set(const AmodValue& value) const
            {
                return g_bonuses != nullptr && g_bonuses->SetTypeProperty != nullptr &&
                       g_bonuses->SetTypeProperty(g_bonusSelf, m_id, m_key, &value) != 0;
            }

            bool                                                    Set(int number) const { return Set(Value(number)); }
            bool                                                    Set(bool boolean) const { return Set(Value(boolean)); }
            bool                                                    Set(const char* text) const { return Set(Value(text)); }
            bool                                                    Set(float a) const { return Set(Value(a)); }
            bool                                                    Set(float a, float b) const { return Set(Value(a, b)); }
            bool                                                    Set(float a, float b, float c) const { return Set(Value(a, b, c)); }
            bool                                                    Set(float a, float b, float c, float d) const { return Set(Value(a, b, c, d)); }

        private:
            const char* m_id;
            const char* m_key;
        };

        /* A kind of bonus by the id a placement names it with. */
        class TypeRef
        {
        public:
            explicit TypeRef(const char* resourceId)
                : m_id(resourceId)
            {
            }

            TypeProperty                                            operator[](const char* key) const
            {
                return TypeProperty(m_id, key);
            }

            /* Whether the game knows this id at all. */
            bool                                                    IsValid() const
            {
                AmodBonusType type;
                return FindType(m_id, type);
            }

            int                                                     PropertyCount() const
            {
                return (g_bonuses != nullptr && g_bonuses->TypePropertyCount != nullptr)
                           ? g_bonuses->TypePropertyCount(g_bonusSelf)
                           : 0;
            }

            const char*                                             PropertyNameAt(int index) const
            {
                return (g_bonuses != nullptr && g_bonuses->TypePropertyNameAt != nullptr)
                           ? g_bonuses->TypePropertyNameAt(g_bonusSelf, index)
                           : "";
            }

        private:
            const char* m_id;
        };

        inline TypeRef                                              Type(const char* resourceId)
        {
            return TypeRef(resourceId);
        }

        /* Changes what this kind draws with, here and now, on the ones already in the room too. */
        inline bool                                                 SetTexture(const char* resourceId, const char* file)
        {
            return g_bonuses != nullptr && g_bonuses->SetTexture != nullptr &&
                   g_bonuses->SetTexture(g_bonusSelf, resourceId, file) != 0;
        }

        inline bool                                                 SetIcon(const char* resourceId, const char* file)
        {
            return g_bonuses != nullptr && g_bonuses->SetIcon != nullptr &&
                   g_bonuses->SetIcon(g_bonusSelf, resourceId, file) != 0;
        }

        inline bool                                                 AwardPointsAt(int points, const float position[3])
        {
            return g_bonuses != nullptr && g_bonuses->AwardPoints != nullptr &&
                   g_bonuses->AwardPoints(g_bonusSelf, points, position) != 0;
        }

        class Bonus : public World::Entity
        {
        public:
            Bonus() = default;
            explicit Bonus(const World::Entity& entity) : World::Entity(entity) {}

            /* What this bonus is worth and how it behaves, out of the game's bonus table */
            bool                                                    GetType(AmodBonusType& out) const
            {
                return Bonuses::FindType(Name(), out);
            }

            int                                                     GetPoints() const
            {
                AmodBonusType type;
                return GetType(type) ? type.pointsOnTake : 0;
            }

            /* How long it lasts, for the kinds that run on a timer. */
            float                                                   GetTime() const
            {
                AmodBonusType type;
                return GetType(type) ? type.time : 0.0f;
            }

            bool                                                    IsTotem() const
            {
                AmodBonusType type;
                return GetType(type) && type.isTotem != 0;
            }

            bool                                                    IsTorch() const
            {
                AmodBonusType type;
                return GetType(type) && type.isTorch != 0;
            }

            bool                                                    MakesTrain() const
            {
                AmodBonusType type;
                return GetType(type) && type.makesTrain != 0;
            }

            
            bool                                                    IsTaken() const
            {
                return LiveByte() == 0;
            }
        };

        
        inline World::Entity                                        CurrentSpawn(int index)
        {
            AmodWorldEntity data;
            return World::GetSpawn(index, data) ? World::Entity(data) : World::Entity();
        }

        /* A bonus placement, taken or not. */
        inline bool                                                 IsBonus(const World::Entity& entity)
        {
            return entity.IsValid() && entity.Kind() == AMOD_ENTITY_BONUS;
        }

        
        inline bool                                                 IsHere(const World::Entity& entity)
        {
            return IsBonus(entity) && entity.LiveByte() != 0;
        }

        inline int                                                  Count()
        {
            const int entities = World::CurrentSpawnCount();
            int count = 0;

            for (int index = 0; index < entities; ++index)
            {
                count += IsHere(CurrentSpawn(index)) ? 1 : 0;
            }
            return count;
        }

        inline Bonus                                                At(int index)
        {
            const int entities = World::CurrentSpawnCount();
            int seen = 0;

            for (int at = 0; at < entities; ++at)
            {
                const World::Entity entity = CurrentSpawn(at);
                if (!IsHere(entity))
                {
                    continue;
                }
                if (seen == index)
                {
                    return Bonus(entity);
                }
                ++seen;
            }
            return Bonus();
        }

        inline int                                                  CountNamed(const char* name)
        {
            const int entities = World::CurrentSpawnCount();
            int count = 0;

            for (int index = 0; index < entities; ++index)
            {
                const World::Entity entity = CurrentSpawn(index);
                count += (IsHere(entity) && entity.Is(name)) ? 1 : 0;
            }
            return count;
        }

        /* Every placement the room carries, taken ones included - what the level was told to * hold rather than what is left of it. */
        inline int                                                  PlacementCount()
        {
            const int entries = World::CurrentSpawnCount();
            int count = 0;

            for (int index = 0; index < entries; ++index)
            {
                count += IsBonus(CurrentSpawn(index)) ? 1 : 0;
            }
            return count;
        }

        inline Bonus                                                Placement(int index)
        {
            const int entries = World::CurrentSpawnCount();
            int seen = 0;

            for (int at = 0; at < entries; ++at)
            {
                const World::Entity entity = CurrentSpawn(at);
                if (!IsBonus(entity))
                {
                    continue;
                }
                if (seen == index)
                {
                    return Bonus(entity);
                }
                ++seen;
            }
            return Bonus();
        }

        inline Bonus                                                Named(const char* name, int index = 0)
        {
            const int entities = World::CurrentSpawnCount();
            int seen = 0;

            for (int at = 0; at < entities; ++at)
            {
                const World::Entity entity = CurrentSpawn(at);
                if (!IsHere(entity) || !entity.Is(name))
                {
                    continue;
                }
                if (seen == index)
                {
                    return Bonus(entity);
                }
                ++seen;
            }
            return Bonus();
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_BONUSES_API_H */
