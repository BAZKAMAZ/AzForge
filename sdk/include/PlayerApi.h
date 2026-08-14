#ifndef AZFORGE_PLAYER_API_H
#define AZFORGE_PLAYER_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* entity+0x178 */
#define AMOD_PLAYER_STATE_ONFOOT                                    0
#define AMOD_PLAYER_STATE_LADDER                                    1
#define AMOD_PLAYER_STATE_CHAIN                                     2
#define AMOD_PLAYER_STATE_PIPE                                      3

/* entity+0x17C */
#define AMOD_PLAYER_SUB_STILL                                       0
#define AMOD_PLAYER_SUB_FALLING                                     1
#define AMOD_PLAYER_SUB_WALK_LEFT                                   2
#define AMOD_PLAYER_SUB_WALK_RIGHT                                  3
#define AMOD_PLAYER_SUB_CLIMB_UP                                    4
#define AMOD_PLAYER_SUB_CLIMB_DOWN                                  5
#define AMOD_PLAYER_SUB_DROPPED                                     6

/* game only uses these two facings */
#define AMOD_PLAYER_FACING_LEFT                                     1.5707963f
#define AMOD_PLAYER_FACING_RIGHT                                    (-1.5707963f)

// Snapshot for this frame, not a handle
typedef struct AmodPlayer
{
    uint32_t structSize;

    int entity; /* his number in the room's entity list, or -1 */

    float position[3];
    float previousPosition[3];
    float facing; /* ~pi or ~0.31 */
    float fallSpeed; /* fall distance; game treats >0.1*cvar as a fall */

    int state; /* AMOD_PLAYER_STATE_* */
    int subState;

    int isDead; /* input ignored */
    int isCrashed; /* fell; picks crash death */

    /* Death kind; Player_Die picks "snd_fire_die" / "snd_shock_die". Fire also runs the scale. */
    int isOnFire;
    int isShocked;

} AmodPlayer;

AMOD_LAST_FIELD(AmodPlayer, isShocked, 0x3C);

typedef enum AmodPlayerEvent
{
    AMOD_PLAYER_EVENT_DIED = 0,
    AMOD_PLAYER_EVENT_REVIVED = 1,
    AMOD_PLAYER_EVENT_LIVES_CHANGED = 2,
    AMOD_PLAYER_EVENT_SCORE_CHANGED = 3
} AmodPlayerEvent;

typedef struct AmodPlayerEventInfo
{
    uint32_t structSize;
    AmodPlayerEvent event;

    int lives;
    int score;

    /* previous value for change events */
    int wasLives;
    int wasScore;

    /* DIED: fell rather than killed */
    int crashed;

} AmodPlayerEventInfo;

AMOD_LAST_FIELD(AmodPlayerEventInfo, crashed, 0x18);

/* valid only for the call */
typedef void(__cdecl* AmodPlayerEventHandler)(const AmodPlayerEventInfo* info, void* user);

/* who moves the player each frame */
typedef enum AmodPlayerDriver
{
    AMOD_PLAYER_DRIVER_GAME = 0,   /* game's Player_Update */
    AMOD_PLAYER_DRIVER_MOD = 1,    /* handler only; game update skipped */
    AMOD_PLAYER_DRIVER_BOTH = 2,   /* game update then handler */
    AMOD_PLAYER_DRIVER_NOBODY = 3  /* neither; no gravity */
} AmodPlayerDriver;

/* frame at the think slot */
typedef struct AmodPlayerFrame
{
    uint32_t structSize;

    float delta; /* delta with time scale */

    float position[3];

    /* equals position in-handler; keep own prev for rise/fall */
    float previousPosition[3];

    float facing;
    float fallSpeed; /* fall distance, never < 0 */

    int state; /* AMOD_PLAYER_STATE_* */
    int subState;
    int isDead;

} AmodPlayerFrame;

AMOD_LAST_FIELD(AmodPlayerFrame, isDead, 0x30);

/* valid only for the call */
typedef void(__cdecl* AmodPlayerUpdateHandler)(const AmodPlayerFrame* frame, void* user);

/* non-zero skips the game state handler */
typedef int(__cdecl* AmodPlayerStateHandler)(const AmodPlayerFrame* frame, void* user);

/* death kind; game only passes these three */
#define AMOD_PLAYER_DEATH_PLAIN                                     0
#define AMOD_PLAYER_DEATH_CRASHED                                   1
#define AMOD_PLAYER_DEATH_AT_ONCE                                   3 /* no anim; straight to death screen */

typedef struct AmodPlayerDeathInfo
{
    uint32_t structSize;

    int kind; /* AMOD_PLAYER_DEATH_* */
    int lives;
    float position[3];

} AmodPlayerDeathInfo;

AMOD_LAST_FIELD(AmodPlayerDeathInfo, position, 0x0C);

/* non-zero refuses the death */
typedef int(__cdecl* AmodPlayerDeathHandler)(const AmodPlayerDeathInfo* info, void* user);

// Intent for this frame, before the step
typedef struct AmodPlayerMove
{
    uint32_t structSize;

    int dirX; /* -1, 0, +1: left / stand / right */
    int dirY; /* same for up/down; ladders and chain read this */

} AmodPlayerMove;

AMOD_LAST_FIELD(AmodPlayerMove, dirY, 0x08);

/* Edit in place before the game applies the window. Write 0 to strip input. */
typedef void(__cdecl* AmodPlayerMoveHandler)(AmodPlayerMove* move, void* user);

/* grid around him; same chars as World::Cell */
typedef struct AmodPlayerPlace
{
    uint32_t structSize;

    int column; /* patch origin column */
    int row;    /* patch origin row */

    /* his cell in the patch; asks use 2,3; often 2,4 */
    int atColumn;
    int atRow;

    int columns;
    int rows;

    char cells[40]; /* row-major, columns per row */

} AmodPlayerPlace;

AMOD_LAST_FIELD(AmodPlayerPlace, cells, 0x1C);

/* five PlaceAsk queries on the patch */
typedef enum AmodPlayerPlaceAsk
{
    AMOD_PLAYER_ASK_LEFT = 0,    /* ladder left; non-zero -> dx=-1 */
    AMOD_PLAYER_ASK_RIGHT = 1,   /* ladder right; non-zero -> dx=+1 */
    AMOD_PLAYER_ASK_CLIMB = 2,   /* climbable 'CC' on row or two above */
    AMOD_PLAYER_ASK_FOOTING = 3, /* footing two rows down */
    AMOD_PLAYER_ASK_PIPE = 4     /* 'PP', which the chain writes around its cross at load */
} AmodPlayerPlaceAsk;

typedef struct AmodPlayerApi
{
    uint32_t structSize;

    /* false in menu / between levels / loading */
    int(__cdecl* IsHere)(AmodMod mod);

    /* set out->structSize first */
    int(__cdecl* Get)(AmodMod mod, AmodPlayer* out);

    int(__cdecl* SetPosition)(AmodMod mod, const float* position);
    int(__cdecl* SetFacing)(AmodMod mod, float facing);

    /* dead stops input; crashed picks crash death */
    int(__cdecl* Kill)(AmodMod mod, int crashed);
    int(__cdecl* Revive)(AmodMod mod);

    int(__cdecl* GetLives)(AmodMod mod);
    int(__cdecl* SetLives)(AmodMod mod, int lives);

    int(__cdecl* GetScore)(AmodMod mod);

    /* via Score_Add; floats number, may award life; null pos = over player */
    int(__cdecl* AddScore)(AmodMod mod, int points, const float* position);

    /* raw write; no float number, no extra life */
    int(__cdecl* SetScore)(AmodMod mod, int score);

    /* after-the-fact; use SetDeathHandler to block a death */
    AmodToken(__cdecl* AddEventHandler)(AmodMod mod, AmodPlayerEvent event, AmodPlayerEventHandler handler, void* user);
    void(__cdecl* RemoveEventHandler)(AmodMod mod, AmodToken token);

    /* one mod at a time; without game update you own input/gravity/etc. */
    int(__cdecl* Drive)(AmodMod mod, AmodPlayerDriver driver, AmodPlayerUpdateHandler handler, void* user);

    /* hand back; unload does the same */
    int(__cdecl* Release)(AmodMod mod);

    AmodPlayerDriver(__cdecl* Driver)(AmodMod mod);

    /* Body_Step; dir -1/0/1; 0 stayed, 1..4 left side for CrossRoom */
    int(__cdecl* Step)(AmodMod mod, int dirX, int dirY, int jump, float delta);

    /* set out->structSize first */
    int(__cdecl* GetPlace)(AmodMod mod, AmodPlayerPlace* out);

    /* <0 when no player */
    int(__cdecl* Ask)(AmodMod mod, AmodPlayerPlaceAsk question);

    /* one state, one mod; rest of frame still the game's (unlike Drive) */
    int(__cdecl* SetStateHandler)(AmodMod mod, int state, AmodPlayerStateHandler handler, void* user);
    int(__cdecl* ClearStateHandler)(AmodMod mod, int state);

    /* in front of Player_Die; one mod */
    int(__cdecl* SetDeathHandler)(AmodMod mod, AmodPlayerDeathHandler handler, void* user);
    int(__cdecl* ClearDeathHandler)(AmodMod mod);

    /* One mod. Everything he does passes through this once a frame. */
    int(__cdecl* SetMoveHandler)(AmodMod mod, AmodPlayerMoveHandler handler, void* user);
    int(__cdecl* ClearMoveHandler)(AmodMod mod);

    /* Fire death via his kill slot (costs a life). Refused if already dead or alight. */
    int(__cdecl* Burn)(AmodMod mod);

    /* Where he is, without touching where he was. SetPosition moves both, which reads as a
       teleport and is right for one; a mod nudging him every frame wants this, because the game
       reckons his fall and his heading from the difference between the two. */
    int(__cdecl* SetPositionOnly)(AmodMod mod, const float* position);

    /* Whether the game pushes him out of the room's boxes for itself. It does that after his own
       update and whoever wrote that update, so a mod driving him was having its work undone in the
       same frame. Put it down and the collisions are the mod's to answer for - all of them. */
    int(__cdecl* SetCollides)(AmodMod mod, int collides);
    int(__cdecl* Collides)(AmodMod mod);

} AmodPlayerApi;

AMOD_LAST_FIELD(AmodPlayerApi, Collides, 0x74);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Player
    {
        inline const AmodPlayerApi* g_player = nullptr;
        inline AmodMod g_playerSelf = nullptr;

        inline void                                                 Attach(const AmodPlayerApi* api, AmodMod self)
        {
            g_player = api;
            g_playerSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_player != nullptr;
        }

        inline bool                                                 IsHere()
        {
            return g_player != nullptr && g_player->IsHere(g_playerSelf) != 0;
        }

        inline bool                                                 Get(AmodPlayer& out)
        {
            out = AmodPlayer{};
            out.structSize = sizeof(out);
            return g_player != nullptr && g_player->Get(g_playerSelf, &out) != 0;
        }

        inline bool                                                 SetPosition(float x, float y, float z)
        {
            const float position[3] = { x, y, z };
            return g_player != nullptr && g_player->SetPosition(g_playerSelf, position) != 0;
        }

        inline bool                                                 SetCollides(bool collides)
        {
            return g_player != nullptr && g_player->SetCollides != nullptr &&
                g_player->SetCollides(g_playerSelf, collides ? 1 : 0) != 0;
        }

        inline bool                                                 Collides()
        {
            return g_player != nullptr && g_player->Collides != nullptr &&
                g_player->Collides(g_playerSelf) != 0;
        }

        /* Moves him and leaves where he was alone, so his fall and his heading still add up. */
        inline bool                                                 SetPositionOnly(float x, float y, float z)
        {
            const float position[3] = { x, y, z };
            return g_player != nullptr && g_player->SetPositionOnly != nullptr &&
                g_player->SetPositionOnly(g_playerSelf, position) != 0;
        }

        inline bool                                                 SetFacing(float facing)
        {
            return g_player != nullptr && g_player->SetFacing(g_playerSelf, facing) != 0;
        }

        inline bool                                                 Kill(bool crashed)
        {
            return g_player != nullptr && g_player->Kill(g_playerSelf, crashed ? 1 : 0) != 0;
        }

        inline bool                                                 Burn()
        {
            return g_player != nullptr && g_player->Burn != nullptr && g_player->Burn(g_playerSelf) != 0;
        }

        inline bool                                                 Revive()
        {
            return g_player != nullptr && g_player->Revive(g_playerSelf) != 0;
        }

        inline int                                                  Lives()
        {
            return g_player != nullptr ? g_player->GetLives(g_playerSelf) : 0;
        }

        inline bool                                                 SetLives(int lives)
        {
            return g_player != nullptr && g_player->SetLives(g_playerSelf, lives) != 0;
        }

        inline int                                                  Score()
        {
            return g_player != nullptr ? g_player->GetScore(g_playerSelf) : 0;
        }

        /* floats number; may award a life */
        inline bool                                                 AddScore(int points)
        {
            return g_player != nullptr && g_player->AddScore(g_playerSelf, points, nullptr) != 0;
        }

        inline bool                                                 AddScoreAt(int points, float x, float y, float z)
        {
            const float position[3] = { x, y, z };
            return g_player != nullptr && g_player->AddScore(g_playerSelf, points, position) != 0;
        }

        inline bool                                                 SetScore(int score)
        {
            return g_player != nullptr && g_player->SetScore(g_playerSelf, score) != 0;
        }

        inline AmodToken                                            On(AmodPlayerEvent event, AmodPlayerEventHandler handler, void* user = nullptr)
        {
            return g_player != nullptr ? g_player->AddEventHandler(g_playerSelf, event, handler, user) : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Off(AmodToken token)
        {
            if (g_player != nullptr)
            {
                g_player->RemoveEventHandler(g_playerSelf, token);
            }
        }

        inline bool                                                 Drive(AmodPlayerUpdateHandler handler, void* user = nullptr)
        {
            return g_player != nullptr && g_player->Drive != nullptr &&
                g_player->Drive(g_playerSelf, AMOD_PLAYER_DRIVER_MOD, handler, user) != 0;
        }

        inline bool                                                 DriveAlongside(AmodPlayerUpdateHandler handler, void* user = nullptr)
        {
            return g_player != nullptr && g_player->Drive != nullptr &&
                g_player->Drive(g_playerSelf, AMOD_PLAYER_DRIVER_BOTH, handler, user) != 0;
        }

        inline bool                                                 Freeze()
        {
            return g_player != nullptr && g_player->Drive != nullptr &&
                g_player->Drive(g_playerSelf, AMOD_PLAYER_DRIVER_NOBODY, nullptr, nullptr) != 0;
        }

        inline bool                                                 Release()
        {
            return g_player != nullptr && g_player->Release != nullptr && g_player->Release(g_playerSelf) != 0;
        }

        inline AmodPlayerDriver                                     Driver()
        {
            return g_player != nullptr && g_player->Driver != nullptr ? g_player->Driver(g_playerSelf)
                                                                      : AMOD_PLAYER_DRIVER_GAME;
        }

        inline int                                                  Step(int dirX, int dirY, bool jump, float delta)
        {
            return g_player != nullptr && g_player->Step != nullptr
                ? g_player->Step(g_playerSelf, dirX, dirY, jump ? 1 : 0, delta)
                : 0;
        }

        inline bool                                                 GetPlace(AmodPlayerPlace& out)
        {
            out = AmodPlayerPlace{};
            out.structSize = sizeof(out);
            return g_player != nullptr && g_player->GetPlace != nullptr &&
                g_player->GetPlace(g_playerSelf, &out) != 0;
        }

        inline int                                                  Ask(AmodPlayerPlaceAsk question)
        {
            return g_player != nullptr && g_player->Ask != nullptr ? g_player->Ask(g_playerSelf, question) : -1;
        }

        inline bool                                                 SetStateHandler(int state, AmodPlayerStateHandler handler, void* user = nullptr)
        {
            return g_player != nullptr && g_player->SetStateHandler != nullptr &&
                g_player->SetStateHandler(g_playerSelf, state, handler, user) != 0;
        }

        inline bool                                                 SetMoveHandler(AmodPlayerMoveHandler handler, void* user = nullptr)
        {
            return g_player != nullptr && g_player->SetMoveHandler != nullptr &&
                g_player->SetMoveHandler(g_playerSelf, handler, user) != 0;
        }

        inline bool                                                 ClearMoveHandler()
        {
            return g_player != nullptr && g_player->ClearMoveHandler != nullptr &&
                g_player->ClearMoveHandler(g_playerSelf) != 0;
        }

        inline bool                                                 ClearStateHandler(int state)
        {
            return g_player != nullptr && g_player->ClearStateHandler != nullptr &&
                g_player->ClearStateHandler(g_playerSelf, state) != 0;
        }

        inline bool                                                 SetDeathHandler(AmodPlayerDeathHandler handler, void* user = nullptr)
        {
            return g_player != nullptr && g_player->SetDeathHandler != nullptr &&
                g_player->SetDeathHandler(g_playerSelf, handler, user) != 0;
        }

        inline bool                                                 ClearDeathHandler()
        {
            return g_player != nullptr && g_player->ClearDeathHandler != nullptr &&
                g_player->ClearDeathHandler(g_playerSelf) != 0;
        }
    }
}

#endif

#endif
