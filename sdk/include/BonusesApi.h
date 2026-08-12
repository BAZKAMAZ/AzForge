#ifndef AZFORGE_BONUSES_API_H
#define AZFORGE_BONUSES_API_H

#include "MmdApi.h"
#include "TextureApi.h"
#include "WorldApi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodBonusType
{
    uint32_t structSize;

    const char* name;           /* what a level places it by */
    const char* resourceId;     /* $res_id; same as name unless the script said otherwise */

    int pointsOnTake;           /* $pts_take */
    int pointsOnRunOut;         /* $pts_out when the timer runs out */

    float time;                 /* $time for totem/torch; placement can override */
    float burnTime;             /* $burn_time, torch only */

    int isTotem;
    int isTorch;
    int makesTrain;
    int trainIndex;             /* $train_idx */

    float glowColor[4];
    float lightColor[3];
    float lightRadius;
    float bonusLightRadius;

    /* Draw pair; take both together. 0 if the type has none. */
    AmodModelId modelId;
    AmodTextureId textureId;
    AmodTextureId iconId;       /* HUD while carried */

    /* New fields are appended, never inserted. */
} AmodBonusType;

AMOD_LAST_FIELD(AmodBonusType, iconId, 0x58);

typedef enum AmodBonusTimer
{
    AMOD_BONUS_TIMER_NONE = 0,  /* stays until something removes it */
    AMOD_BONUS_TIMER_GAME = 1,  /* game counts down and clears (totem) */
    AMOD_BONUS_TIMER_MOD = 2    /* ring drawn; mod owns the countdown */
} AmodBonusTimer;

/* Which numbers in AmodBonusTypeDef to apply */
#define AMOD_BONUS_FIELD_POINTS_TAKE  0x00000001
#define AMOD_BONUS_FIELD_POINTS_OUT   0x00000002
#define AMOD_BONUS_FIELD_TIME         0x00000004
#define AMOD_BONUS_FIELD_TIMER        0x00000008
#define AMOD_BONUS_FIELD_TRAIN        0x00000010
#define AMOD_BONUS_FIELD_GLOW         0x00000020
#define AMOD_BONUS_FIELD_GLOW2        0x00000040
#define AMOD_BONUS_FIELD_ROCKING      0x00000080
#define AMOD_BONUS_FIELD_LIGHT        0x00000100

#define AMOD_BONUS_LIKE_SCORED  "bonus"
#define AMOD_BONUS_LIKE_MEGA    "mega_bonus"
#define AMOD_BONUS_LIKE_LIFE    "life"
#define AMOD_BONUS_LIKE_SILENT  "sys"
#define AMOD_BONUS_LIKE_QUEST   "quest"

typedef struct AmodBonusTypeDef
{
    uint32_t structSize;
    uint32_t fields;            /* AMOD_BONUS_FIELD_* for the numbers below */

    const char* resourceId;     /* required, and new */

    /* Null = from scratch (needs model or texture). Else copy unnamed fields from this type. */
    const char* basedOn;

    /* Take behaviour: SCORED/MEGA/LIFE/QUEST/SILENT, or null = carry in a slot. */
    const char* behavesAs;

    /* Null keeps basedOn. Unknown resource name is refused. */
    const char* model;
    const char* texture;
    const char* icon;           /* HUD while carried */
    const char* soundOnTake;
    const char* soundOnUse;     /* also the last-six-seconds tick */

    int pointsOnTake;
    int pointsOnRunOut;
    float time;                 /* duration once carried */
    AmodBonusTimer timer;
    int makesTrain;
    int trainIndex;

    float glowColor[4];
    float glowColor2[4];

    float rockingAmp;           /* swing: amp along axis at speed */
    float rockingSpeed;
    float rockingAxis[3];

    float lightColor[3];
    float lightRadius;

    /* New fields are appended, never inserted. */
} AmodBonusTypeDef;

AMOD_LAST_FIELD(AmodBonusTypeDef, lightRadius, 0x80);

/* One placement: where and what, apart from its type. */
typedef struct AmodBonusPlacement
{
    uint32_t structSize;

    const char* name;
    int column;
    int row;

    float time;                 /* >0 overrides type $time */
    const char* tag;            /* null = none */
    int decor;                  /* does not count toward level wait */

    /* $own_model; either set carries both, null keeps the type's. */
    const char* model;
    const char* texture;

    /* New fields are appended, never inserted. */
} AmodBonusPlacement;

AMOD_LAST_FIELD(AmodBonusPlacement, texture, 0x20);

typedef enum AmodBonusTakeResult
{
    AMOD_BONUS_TAKE_DEFAULT = 0, /* game does its usual path */
    AMOD_BONUS_TAKE_HANDLED = 1, /* taken; skip the game path */
    AMOD_BONUS_TAKE_REFUSE = 2   /* leave it lying */
} AmodBonusTakeResult;

typedef struct AmodBonusTakeInfo
{
    uint32_t structSize;

    const char* name;           /* type name the game branches on */
    const char* resourceId;     /* which bonus this is */
    const char* tag;            /* placement $tag, "" if none */

    int column;
    int row;
    float position[3];

    int pointsOnTake;
    int isDecor;

    /* New fields are appended, never inserted. */
} AmodBonusTakeInfo;

AMOD_LAST_FIELD(AmodBonusTakeInfo, isDecor, 0x28);

/* Before the game handles a touch. */
typedef AmodBonusTakeResult (__cdecl* AmodBonusTakeHandler)(const AmodBonusTakeInfo* info, void* user);

/* One placement about to be stood up. */
typedef struct AmodBonusSpawnInfo
{
    uint32_t structSize;

    const char* name;
    const char* resourceId;     /* "" if unresolved */
    const char* tag;
    float position[3];

    int hasType;                /* 0 if $name matched no type */
    int isLive;                 /* 0 if already spent */
    int kind;                   /* 0 = game's; else mod kind */

    /* New fields are appended, never inserted. */
} AmodBonusSpawnInfo;

AMOD_LAST_FIELD(AmodBonusSpawnInfo, kind, 0x24);

/* Before a placement becomes a bonus. Return 0 to refuse. */
typedef int (__cdecl* AmodBonusSpawnHandler)(const AmodBonusSpawnInfo* info, void* user);

/* Mod-owned kind: not in the game table; handler owns build/tick/take. */
typedef enum AmodBonusEvent
{
    AMOD_BONUS_EVENT_BUILD = 0,  /* fill body; nonzero to make it */
    AMOD_BONUS_EVENT_UPDATE = 1, /* each frame; body written back. Borrowed: 0 = also run game tick */
    AMOD_BONUS_EVENT_TAKE = 2    /* return AmodBonusTakeResult; DEFAULT uses game touch */
} AmodBonusEvent;

typedef struct AmodBonusBody
{
    uint32_t structSize;

    const char* name;
    const char* tag;            /* may be null */
    float position[3];

    /* BUILD only: names resolved once into the ids below. */
    const char* model;
    const char* texture;

    /* Read every event; assign on UPDATE to change look without a lookup. */
    AmodModelId modelId;
    AmodTextureId textureId;

    AmodBox box;
    int isHidden;

    float glowColor[4];
    float glowColor2[4];

    float rockingAmp;
    float rockingSpeed;
    float rockingAxis[3];

    float lightColor[3];
    float lightRadius;

    int pointsOnTake;           /* for TAKE_DEFAULT */

    /* New fields are appended, never inserted. */
} AmodBonusBody;

AMOD_LAST_FIELD(AmodBonusBody, pointsOnTake, 0x8C);

typedef struct AmodBonusKindInfo
{
    uint32_t structSize;

    int event;                  /* AMOD_BONUS_EVENT_* */
    int kind;
    const void* record;         /* stable for the life of one bonus */

    AmodBonusBody* body;        /* BUILD/UPDATE; null on TAKE */
    const AmodBonusTakeInfo* take; /* TAKE only */
    float deltaTime;            /* UPDATE only */

    /* New fields are appended, never inserted. */
} AmodBonusKindInfo;

AMOD_LAST_FIELD(AmodBonusKindInfo, deltaTime, 0x18);

/* Nonzero to build on BUILD; AmodBonusTakeResult on TAKE; ignored on UPDATE. */
typedef int (__cdecl* AmodBonusKindHandler)(const AmodBonusKindInfo* info, void* user);

typedef struct AmodBonusesApi
{
    uint32_t structSize;

    int (__cdecl* GetTypeCount)(AmodMod mod);
    int (__cdecl* GetType)(AmodMod mod, int index, AmodBonusType* out);

    /* Placement name lookup; nonzero if known. */
    int (__cdecl* FindType)(AmodMod mod, const char* name, AmodBonusType* out);

    int (__cdecl* Place)(AmodMod mod, const char* name, int column, int row);
    int (__cdecl* DefineType)(AmodMod mod, const AmodBonusTypeDef* def);

    /* Place with full placement fields. */
    int (__cdecl* PlaceEx)(AmodMod mod, const AmodBonusPlacement* placement);

    AmodToken (__cdecl* AddTakeHandler)(AmodMod mod, AmodBonusTakeHandler handler, void* user);
    void (__cdecl* RemoveTakeHandler)(AmodMod mod, AmodToken token);

    /* Score via the game routine (floating number). */
    int (__cdecl* AwardPoints)(AmodMod mod, int points, const float* position);
    int (__cdecl* TakeChainLink)(AmodMod mod, int points, const float* position);

    int (__cdecl* SetTexture)(AmodMod mod, const char* resourceId, const char* file);
    int (__cdecl* SetIcon)(AmodMod mod, const char* resourceId, const char* file);

    int (__cdecl* GetTypeProperty)(AmodMod mod, const char* resourceId, const char* key, AmodValue* value);
    int (__cdecl* SetTypeProperty)(AmodMod mod, const char* resourceId, const char* key, const AmodValue* value);
    int (__cdecl* HasTypeProperty)(AmodMod mod, const char* resourceId, const char* key);
    int (__cdecl* TypePropertyCount)(AmodMod mod);
    const char* (__cdecl* TypePropertyNameAt)(AmodMod mod, int index);

    AmodToken (__cdecl* AddSpawnHandler)(AmodMod mod, AmodBonusSpawnHandler handler, void* user);
    void (__cdecl* RemoveSpawnHandler)(AmodMod mod, AmodToken token);

    /* Placement behind a current-room entity index; set out->structSize first. */
    int (__cdecl* GetPlacement)(AmodMod mod, int index, AmodBonusSpawnInfo* out);

    /* Remove as collected: no score, no sound; level count drops. */
    int (__cdecl* RemovePlacement)(AmodMod mod, int index);

    /* Mod kind; 0 if name taken. Not in the game table. */
    int (__cdecl* RegisterKind)(AmodMod mod, const char* name, AmodBonusKindHandler handler, void* user);

    /* Place a registered kind in the current room. */
    int (__cdecl* PlaceKind)(AmodMod mod, int kind, const AmodBonusPlacement* placement);

    /* Drive a level-placed bonus with the kind handler; holds across room changes. */
    int (__cdecl* BorrowPlacement)(AmodMod mod, int kind, int index);

    /* Restore game tick/touch and original look. */
    int (__cdecl* ReturnPlacement)(AmodMod mod, int index);

    /* roomIndex < 0 = current room; any room of the level. */
    int (__cdecl* PlacementCount)(AmodMod mod, int roomIndex);
    int (__cdecl* GetPlacementIn)(AmodMod mod, int roomIndex, int index, AmodBonusSpawnInfo* out);
    int (__cdecl* RemovePlacementIn)(AmodMod mod, int roomIndex, int index);
    int (__cdecl* BorrowPlacementIn)(AmodMod mod, int kind, int roomIndex, int index);

    /* Room is an argument so a zeroed placement still means current room. */
    int (__cdecl* PlaceIn)(AmodMod mod, int roomIndex, const AmodBonusPlacement* placement);
    int (__cdecl* PlaceKindIn)(AmodMod mod, int kind, int roomIndex, const AmodBonusPlacement* placement);

    int (__cdecl* GetPlacementProperty)(AmodMod mod, int roomIndex, int index, const char* key, AmodValue* value);
    int (__cdecl* SetPlacementProperty)(AmodMod mod, int roomIndex, int index, const char* key, const AmodValue* value);
    int (__cdecl* PlacementPropertyCount)(AmodMod mod);
    const char* (__cdecl* PlacementPropertyNameAt)(AmodMod mod, int index);

    /* Fields added in later API versions go here. */
} AmodBonusesApi;

AMOD_LAST_FIELD(AmodBonusesApi, PlacementPropertyNameAt, 0x8C);

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

        /* Place on a cell of the current room. False if unknown type or off-grid. */
        inline bool                                                 Place(const char* name, int column, int row)
        {
            return g_bonuses != nullptr && g_bonuses->Place(g_bonusSelf, name, column, row) != 0;
        }

        /* Placement with timer, tag, decor, etc. Build, set fields, place. */
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

            /* This one's own look, whatever its type wears. Either alone is enough. */
            NewPlacement&                                           Model(const char* file) { m_data.model = file; return *this; }
            NewPlacement&                                           Texture(const char* file) { m_data.texture = file; return *this; }

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
            /* basedOn null = from scratch (needs model or texture or draws nothing). */
            NewType(const char* resourceId, const char* basedOn = nullptr)
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

        /* Lambda form; captures kept in static. */
        template <typename Handler>
        inline AmodToken                                            OnTake(Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return OnTake([](const AmodBonusTakeInfo* info, void*) -> AmodBonusTakeResult
            {
                return kept(info);
            }, nullptr);
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

        /* Placement behind entity index; roomIndex < 0 = current room. */
        inline bool                                                 GetPlacement(int index, AmodBonusSpawnInfo& out, int roomIndex = -1)
        {
            out = AmodBonusSpawnInfo{};
            out.structSize = sizeof(out);
            return g_bonuses != nullptr && g_bonuses->GetPlacementIn != nullptr &&
                g_bonuses->GetPlacementIn(g_bonusSelf, roomIndex, index, &out) != 0;
        }

        inline bool                                                 RemovePlacement(int index, int roomIndex = -1)
        {
            return g_bonuses != nullptr && g_bonuses->RemovePlacementIn != nullptr &&
                g_bonuses->RemovePlacementIn(g_bonusSelf, roomIndex, index) != 0;
        }

        /* How many placements a room holds. */
        inline int                                                  PlacementCountIn(int roomIndex = -1)
        {
            return (g_bonuses != nullptr && g_bonuses->PlacementCount != nullptr)
                ? g_bonuses->PlacementCount(g_bonusSelf, roomIndex) : 0;
        }

        /* Place in a room the player need not be in. */
        inline bool                                                 PlaceIn(int roomIndex, const AmodBonusPlacement& where)
        {
            return g_bonuses != nullptr && g_bonuses->PlaceIn != nullptr &&
                g_bonuses->PlaceIn(g_bonusSelf, roomIndex, &where) != 0;
        }

        inline bool                                                 PlaceIn(int roomIndex, const char* name, int column, int row, const char* tag = nullptr)
        {
            AmodBonusPlacement where = {};
            where.structSize = sizeof(where);
            where.name = name;
            where.column = column;
            where.row = row;
            where.tag = tag;
            return PlaceIn(roomIndex, where);
        }

        /* Placement fields ($tag, $decor, $time, position, live); keys via PlacementPropertyNameAt. */
        inline bool                                                 GetPlacementProperty(int index, const char* key, AmodValue& value, int roomIndex = -1)
        {
            value = AmodValue{};
            return g_bonuses != nullptr && g_bonuses->GetPlacementProperty != nullptr &&
                g_bonuses->GetPlacementProperty(g_bonusSelf, roomIndex, index, key, &value) != 0;
        }

        inline bool                                                 SetPlacementProperty(int index, const char* key, const AmodValue& value, int roomIndex = -1)
        {
            return g_bonuses != nullptr && g_bonuses->SetPlacementProperty != nullptr &&
                g_bonuses->SetPlacementProperty(g_bonusSelf, roomIndex, index, key, &value) != 0;
        }

        inline int                                                  PlacementPropertyCount()
        {
            return (g_bonuses != nullptr && g_bonuses->PlacementPropertyCount != nullptr)
                ? g_bonuses->PlacementPropertyCount(g_bonusSelf) : 0;
        }

        inline const char* PlacementPropertyNameAt(int index)
        {
            return (g_bonuses != nullptr && g_bonuses->PlacementPropertyNameAt != nullptr)
                ? g_bonuses->PlacementPropertyNameAt(g_bonusSelf, index) : nullptr;
        }

        inline bool                                                 SameText(const char* a, const char* b)
        {
            if (a == nullptr || b == nullptr)
            {
                return false;
            }
            while (*a != '\0' && *a == *b)
            {
                ++a;
                ++b;
            }
            return *a == *b;
        }

        /* Entity index of the nth live bonus with this $tag, or -1. */
        inline int                                                  IndexOfTagged(const char* tag, int nth = 0)
        {
            const int entities = World::CurrentSpawnCount();
            int seen = 0;
            for (int at = 0; at < entities; ++at)
            {
                AmodBonusSpawnInfo placement = {};
                if (!GetPlacement(at, placement) || placement.isLive == 0)
                {
                    continue;
                }
                if (placement.tag[0] != '\0' && SameText(placement.tag, tag))
                {
                    if (seen == nth)
                    {
                        return at;
                    }
                    ++seen;
                }
            }
            return -1;
        }

        /* Remove every live bonus with this $tag; returns how many. Indices stay stable until sweep. */
        inline int                                                  RemoveTagged(const char* tag)
        {
            const int entities = World::CurrentSpawnCount();
            int taken = 0;
            for (int at = 0; at < entities; ++at)
            {
                AmodBonusSpawnInfo placement = {};
                if (!GetPlacement(at, placement) || placement.isLive == 0)
                {
                    continue;
                }
                if (placement.tag[0] != '\0' && SameText(placement.tag, tag) && RemovePlacement(at))
                {
                    ++taken;
                }
            }
            return taken;
        }

        /* Remove every live bonus of this type name. */
        inline int                                                  RemoveNamed(const char* name)
        {
            const int entities = World::CurrentSpawnCount();
            int taken = 0;
            for (int at = 0; at < entities; ++at)
            {
                AmodBonusSpawnInfo placement = {};
                if (!GetPlacement(at, placement) || placement.isLive == 0)
                {
                    continue;
                }
                if (SameText(placement.name, name) && RemovePlacement(at))
                {
                    ++taken;
                }
            }
            return taken;
        }

        /* Entity index of the bonus on that cell, or -1. */
        inline int                                                  IndexOfCell(int column, int row)
        {
            const int entities = World::CurrentSpawnCount();
            for (int at = 0; at < entities; ++at)
            {
                const World::Entity entity = CurrentSpawn(at);
                if (IsHere(entity) && entity.GetColumn() == column && entity.GetRow() == row)
                {
                    return at;
                }
            }
            return -1;
        }

        /* On every placement stand-up; return 0 to refuse it. */
        inline AmodToken                                            OnSpawn(AmodBonusSpawnHandler handler, void* user = nullptr)
        {
            return (g_bonuses != nullptr && g_bonuses->AddSpawnHandler != nullptr)
                ? g_bonuses->AddSpawnHandler(g_bonusSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        /* Lambda form; captures kept in static. */
        template <typename Handler>
        inline AmodToken                                            OnSpawn(Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return OnSpawn([](const AmodBonusSpawnInfo* info, void*) -> int
                {
                    return kept(info);
                }, nullptr);
        }

        inline void                                                 RemoveSpawnHandler(AmodToken token)
        {
            if (g_bonuses != nullptr && g_bonuses->RemoveSpawnHandler != nullptr)
            {
                g_bonuses->RemoveSpawnHandler(g_bonusSelf, token);
            }
        }

        /* Register a mod kind (not in the game table). 0 if name taken. */
        inline int                                                  RegisterKind(const char* name, AmodBonusKindHandler handler, void* user = nullptr)
        {
            return (g_bonuses != nullptr && g_bonuses->RegisterKind != nullptr)
                ? g_bonuses->RegisterKind(g_bonusSelf, name, handler, user)
                : 0;
        }

        /* Lambda form; one static per lambda type. */
        template <typename Handler>
        inline int                                                  RegisterKind(const char* name, Handler&& handler)
        {
            static Handler kept = static_cast<Handler&&>(handler);
            return RegisterKind(name, [](const AmodBonusKindInfo* info, void*) -> int
                {
                    return kept(info);
                }, nullptr);
        }

        /* Place a registered kind in the current room. */
        inline bool                                                 PlaceKind(int kind, const AmodBonusPlacement& where)
        {
            return g_bonuses != nullptr && g_bonuses->PlaceKind != nullptr &&
                g_bonuses->PlaceKind(g_bonusSelf, kind, &where) != 0;
        }

        inline bool                                                 PlaceKind(int kind, int column, int row, const char* tag = nullptr)
        {
            AmodBonusPlacement where = {};
            where.structSize = sizeof(where);
            where.column = column;
            where.row = row;
            where.tag = tag;
            return PlaceKind(kind, where);
        }

        /* Take over a level-placed bonus; rooms not yet entered are claimed on stand-up. */
        inline bool                                                 BorrowPlacement(int kind, int index, int roomIndex = -1)
        {
            return g_bonuses != nullptr && g_bonuses->BorrowPlacementIn != nullptr &&
                g_bonuses->BorrowPlacementIn(g_bonusSelf, kind, roomIndex, index) != 0;
        }

        /* Place a registered kind in any room. */
        inline bool                                                 PlaceKindIn(int kind, int roomIndex, const AmodBonusPlacement& where)
        {
            return g_bonuses != nullptr && g_bonuses->PlaceKindIn != nullptr &&
                g_bonuses->PlaceKindIn(g_bonusSelf, kind, roomIndex, &where) != 0;
        }

        inline bool                                                 PlaceKindIn(int kind, int roomIndex, int column, int row, const char* tag = nullptr)
        {
            AmodBonusPlacement where = {};
            where.structSize = sizeof(where);
            where.column = column;
            where.row = row;
            where.tag = tag;
            return PlaceKindIn(kind, roomIndex, where);
        }

        /* Hand a borrowed bonus back to the game. */
        inline bool                                                 ReturnPlacement(int index)
        {
            return g_bonuses != nullptr && g_bonuses->ReturnPlacement != nullptr &&
                g_bonuses->ReturnPlacement(g_bonusSelf, index) != 0;
        }

        /* Return every borrowed bonus in the current room. */
        inline int                                                  ReturnAll()
        {
            const int entities = World::CurrentSpawnCount();
            int given = 0;
            for (int at = 0; at < entities; ++at)
            {
                if (ReturnPlacement(at))
                {
                    ++given;
                }
            }
            return given;
        }

        /* Borrow every live game bonus with this $tag. */
        inline int                                                  BorrowTagged(int kind, const char* tag)
        {
            const int entities = World::CurrentSpawnCount();
            int taken = 0;
            for (int at = 0; at < entities; ++at)
            {
                AmodBonusSpawnInfo placement = {};
                if (!GetPlacement(at, placement) || placement.isLive == 0 || placement.kind != 0)
                {
                    continue;
                }
                if (placement.tag[0] != '\0' && SameText(placement.tag, tag) && BorrowPlacement(kind, at))
                {
                    ++taken;
                }
            }
            return taken;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_BONUSES_API_H */
