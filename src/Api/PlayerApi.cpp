#include <windows.h>

#include "src/Api/PlayerApi.h"
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
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        AmodPlayerApi g_table = {};

        // Score_Add: __thiscall, RET 8. As cdecl it would unwind twice.
        typedef void(__fastcall* ScoreAddRoutine)(void* session, void* unused, int points, const float* position);
        typedef void(__cdecl* EntityThinkRoutine)(void* entity);

        // Body_Step: 6 args, last is the game's zero flags.
        typedef int(__cdecl* BodyStepRoutine)(void* body, int dirX, int dirY, int jump, float delta, int flags);

        // PlaceFill: __cdecl, 3 args (ADD ESP,0xC at p_draw_place site).
        typedef void(__cdecl* PlaceFillRoutine)(const void* position, const void* extent, void* window);

        typedef void(__cdecl* StateUpdateRoutine)(void* entity, void* extra);
        typedef void(__cdecl* LadderUpdateRoutine)(void* entity);
        typedef void(__cdecl* DieRoutine)(void* entity, int kind);

        // Player_AdjustMove: __cdecl, caller cleans 0xC
        typedef void(__cdecl* AdjustMoveRoutine)(void* player, int* dirX, int* dirY);

        struct Driving
        {
            ModContext* owner;
            AmodPlayerDriver driver;
            AmodPlayerUpdateHandler handler;
            void* user;
            EntityThinkRoutine gameThink;
            uint32_t token;
        };

        struct Moving
        {
            ModContext* owner;
            AmodPlayerMoveHandler handler;
            void* user;
        };

        Moving g_moving = {};
        InlineHook g_moveHook;
        AdjustMoveRoutine g_gameAdjustMove = nullptr;
        bool g_moveHookTried = false;

        Driving g_driving = {};

        struct Subscription
        {
            uint32_t id;
            ModContext* owner;
            AmodPlayerEvent event;
            AmodPlayerEventHandler handler;
            void* user;
        };

        std::vector<Subscription> g_subscriptions;
        uint32_t g_nextSubscriptionId = 1;

        // Last-tick snapshot for change detection.
        int g_wasLives = 0;
        int g_wasScore = 0;
        int g_wasDead = -1;

        uint8_t* Player()
        {
            return const_cast<uint8_t*>(WorldApi::Player());
        }

        int* LivesAt()
        {
            return static_cast<int*>(GameModule::Resolve(GameAddress::kPlayerLives));
        }

        int* ScoreAt()
        {
            uint8_t* const session = WorldApi::Session();
            return session != nullptr ? reinterpret_cast<int*>(session + GameAddress::kSessionScoreOffset) : nullptr;
        }

        int EntityIndex(const uint8_t* player)
        {
            int at = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                entity != nullptr && at < static_cast<int>(GameAddress::kEntityPoolCount);
                entity = WorldApi::NextLiveEntity(entity), ++at)
            {
                if (entity == player)
                {
                    return at;
                }
            }
            return -1;
        }

        int __cdecl ApiIsHere(AmodMod mod)
        {
            return (Caller(mod) != nullptr && Player() != nullptr) ? 1 : 0;
        }

        int __cdecl ApiGet(AmodMod mod, AmodPlayer* out)
        {
            const uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player right now");
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Get was given nowhere to put it");
            }

            const uint32_t flags = Read<uint32_t>(player, GameAddress::kPlayerFlagsOffset);

            AmodPlayer filled = {};
            filled.structSize = sizeof(filled);
            filled.entity = EntityIndex(player);

            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t offset = static_cast<uintptr_t>(axis) * sizeof(float);
                filled.position[axis] = Read<float>(player, GameAddress::kEntityPositionOffset + offset);
                filled.previousPosition[axis] = Read<float>(player, GameAddress::kEntityPreviousPositionOffset + offset);
            }

            filled.facing = Read<float>(player, GameAddress::kPlayerFacingOffset);
            filled.fallSpeed = Read<float>(player, GameAddress::kPlayerFallSpeedOffset);
            filled.state = Read<int>(player, GameAddress::kEntityStateOffset);
            filled.subState = Read<int>(player, GameAddress::kEntitySubStateOffset);
            filled.isDead = (flags & GameAddress::kPlayerFlagDead) != 0 ? 1 : 0;
            filled.isCrashed = (flags & GameAddress::kPlayerFlagCrashed) != 0 ? 1 : 0;
            filled.isOnFire = (flags & GameAddress::kPlayerFlagOnFire) != 0 ? 1 : 0;
            filled.isShocked = (flags & GameAddress::kPlayerFlagShocked) != 0 ? 1 : 0;
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetPosition(AmodMod mod, const float* position)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to move");
            }
            if (position == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetPosition was given nowhere to move him to");
            }

            // Move previous with him; leaving it makes one step look like a leap.
            memcpy(player + GameAddress::kEntityPositionOffset, position, 3 * sizeof(float));
            memcpy(player + GameAddress::kEntityPreviousPositionOffset, position, 3 * sizeof(float));
            return 1;
        }

        int __cdecl ApiSetPositionOnly(AmodMod mod, const float* position)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to move");
            }
            if (position == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetPositionOnly was given nowhere to move him to");
            }

            memcpy(player + GameAddress::kEntityPositionOffset, position, 3 * sizeof(float));
            return 1;
        }

        int __cdecl ApiSetCollides(AmodMod mod, int collides)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to collide or not");
            }

            const uint8_t on = collides != 0 ? 1 : 0;
            memcpy(player + GameAddress::kEntityCollidesOffset, &on, sizeof(on));
            return 1;
        }

        int __cdecl ApiCollides(AmodMod mod)
        {
            const uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;
            return (player != nullptr && player[GameAddress::kEntityCollidesOffset] != 0) ? 1 : 0;
        }

        int __cdecl ApiSetFacing(AmodMod mod, float facing)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to turn");
            }

            memcpy(player + GameAddress::kPlayerFacingOffset, &facing, sizeof(facing));
            return 1;
        }

        int __cdecl ApiKill(AmodMod mod, int crashed)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to kill");
            }

            // Same bits Player_Update sets: dead blocks input, crashed picks "crash" anim.
            uint32_t flags = Read<uint32_t>(player, GameAddress::kPlayerFlagsOffset);
            flags |= GameAddress::kPlayerFlagDead;
            if (crashed != 0)
            {
                flags |= GameAddress::kPlayerFlagCrashed;
            }
            memcpy(player + GameAddress::kPlayerFlagsOffset, &flags, sizeof(flags));
            return 1;
        }

        typedef void(__cdecl* DieRoutine)(void* entity, int kind);

        int __cdecl ApiBurn(AmodMod mod)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to burn");
            }

            uint32_t flags = Read<uint32_t>(player, GameAddress::kPlayerFlagsOffset);

            // The game's own fire refuses a second light, and Player_Die refuses the dead
            if ((flags & (GameAddress::kPlayerFlagDead | GameAddress::kPlayerFlagOnFire)) != 0)
            {
                return 0;
            }

            const DieRoutine die = Read<DieRoutine>(player, GameAddress::kEntityDieOffset);
            if (die == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the player has no kill slot to go through");
            }

            const float scale = GameAddress::kPlayerBurnScale;
            memcpy(player + GameAddress::kEntityScaleOffset, &scale, sizeof(scale));

            flags |= GameAddress::kPlayerFlagOnFire;
            memcpy(player + GameAddress::kPlayerFlagsOffset, &flags, sizeof(flags));

            die(player, 0);
            return 1;
        }

        int __cdecl ApiRevive(AmodMod mod)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to revive");
            }

            // Clear the four flags; does not undo mid-animation.
            uint32_t flags = Read<uint32_t>(player, GameAddress::kPlayerFlagsOffset);
            flags &= ~(GameAddress::kPlayerFlagDead | GameAddress::kPlayerFlagCrashed |
                       GameAddress::kPlayerFlagOnFire | GameAddress::kPlayerFlagShocked);
            memcpy(player + GameAddress::kPlayerFlagsOffset, &flags, sizeof(flags));

            uint32_t entityFlags = Read<uint32_t>(player, GameAddress::kEntityFlagsOffset);
            entityFlags &= ~GameAddress::kEntityHiddenFlag;
            memcpy(player + GameAddress::kEntityFlagsOffset, &entityFlags, sizeof(entityFlags));
            return 1;
        }

        int __cdecl ApiGetLives(AmodMod mod)
        {
            const int* const lives = Caller(mod) != nullptr ? LivesAt() : nullptr;
            return lives != nullptr ? *lives : 0;
        }

        int __cdecl ApiSetLives(AmodMod mod, int lives)
        {
            int* const at = Caller(mod) != nullptr ? LivesAt() : nullptr;

            if (at == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no game running to have lives");
            }

            // No clamp, same as p_life_count.
            *at = lives;
            return 1;
        }

        int __cdecl ApiGetScore(AmodMod mod)
        {
            const int* const score = Caller(mod) != nullptr ? ScoreAt() : nullptr;
            return score != nullptr ? *score : 0;
        }

        int __cdecl ApiAddScore(AmodMod mod, int points, const float* position)
        {
            uint8_t* const session = Caller(mod) != nullptr ? WorldApi::Session() : nullptr;
            const ScoreAddRoutine add = reinterpret_cast<ScoreAddRoutine>(GameModule::Resolve(GameAddress::kScoreAdd));

            if (session == nullptr || add == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no game running to score in");
            }

            add(session, nullptr, points, position);
            return 1;
        }

        int __cdecl ApiSetScore(AmodMod mod, int score)
        {
            int* const at = Caller(mod) != nullptr ? ScoreAt() : nullptr;

            if (at == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no game running to have a score");
            }

            *at = score < 0 ? 0 : score;
            return 1;
        }

        void FillFrame(const uint8_t* player, AmodPlayerFrame& frame)
        {
            frame.structSize = sizeof(frame);
            frame.delta = Read<float>(player, GameAddress::kEntityDeltaOffset);

            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t offset = static_cast<uintptr_t>(axis) * sizeof(float);
                frame.position[axis] = Read<float>(player, GameAddress::kEntityPositionOffset + offset);
                frame.previousPosition[axis] = Read<float>(player, GameAddress::kEntityPreviousPositionOffset + offset);
            }

            frame.facing = Read<float>(player, GameAddress::kPlayerFacingOffset);
            frame.fallSpeed = Read<float>(player, GameAddress::kPlayerFallSpeedOffset);
            frame.state = Read<int>(player, GameAddress::kEntityStateOffset);
            frame.subState = Read<int>(player, GameAddress::kEntitySubStateOffset);
            frame.isDead = (Read<uint32_t>(player, GameAddress::kPlayerFlagsOffset) & GameAddress::kPlayerFlagDead) != 0 ? 1 : 0;
        }

        // Replaces Player_Update in the think slot; delta/prev already written.
        void __cdecl ThinkThunk(void* self)
        {
            uint8_t* const player = static_cast<uint8_t*>(self);
            const Driving driving = g_driving;

            if (driving.owner == nullptr)
            {
                // Slot outlived the claim; fall back to game think.
                if (driving.gameThink != nullptr)
                {
                    driving.gameThink(self);
                }
                return;
            }

            if (driving.driver == AMOD_PLAYER_DRIVER_GAME || driving.driver == AMOD_PLAYER_DRIVER_BOTH)
            {
                if (driving.gameThink != nullptr)
                {
                    driving.gameThink(self);
                }
            }

            if (driving.handler == nullptr ||
                (driving.driver != AMOD_PLAYER_DRIVER_MOD && driving.driver != AMOD_PLAYER_DRIVER_BOTH))
            {
                return;
            }

            // Game update may have removed him from the level.
            if (driving.driver == AMOD_PLAYER_DRIVER_BOTH && Player() != player)
            {
                return;
            }

            AmodPlayerFrame frame = {};
            FillFrame(player, frame);
            driving.handler(&frame, driving.user);
        }

        void TakeSlot(uint8_t* player)
        {
            const EntityThinkRoutine now = Read<EntityThinkRoutine>(player, GameAddress::kEntityThinkOffset);
            if (now == &ThinkThunk)
            {
                return;
            }

            g_driving.gameThink = now;
            const EntityThinkRoutine mine = &ThinkThunk;
            memcpy(player + GameAddress::kEntityThinkOffset, &mine, sizeof(mine));
        }

        void GiveSlotBack()
        {
            uint8_t* const player = Player();

            if (player != nullptr && Read<EntityThinkRoutine>(player, GameAddress::kEntityThinkOffset) == &ThinkThunk)
            {
                const EntityThinkRoutine back = g_driving.gameThink;
                memcpy(player + GameAddress::kEntityThinkOffset, &back, sizeof(back));
            }
        }

        struct Dying
        {
            ModContext* owner;
            AmodPlayerDeathHandler handler;
            void* user;
            DieRoutine gameDie;
            uint32_t token;
        };

        Dying g_dying = {};

        void __cdecl DieThunk(void* self, int kind)
        {
            const Dying dying = g_dying;
            const uint8_t* const player = static_cast<const uint8_t*>(self);

            if (dying.owner != nullptr && dying.handler != nullptr)
            {
                AmodPlayerDeathInfo info = {};
                info.structSize = sizeof(info);
                info.kind = kind;
                info.lives = LivesAt() != nullptr ? *LivesAt() : 0;

                for (int axis = 0; axis < 3; ++axis)
                {
                    info.position[axis] = Read<float>(player, GameAddress::kEntityPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
                }

                if (dying.handler(&info, dying.user) != 0)
                {
                    return;
                }
            }

            if (dying.gameDie != nullptr)
            {
                dying.gameDie(self, kind);
            }
        }

        void TakeDieSlot(uint8_t* player)
        {
            const DieRoutine now = Read<DieRoutine>(player, GameAddress::kEntityHitHandlerOffset);
            if (now == &DieThunk)
            {
                return;
            }

            g_dying.gameDie = now;
            const DieRoutine mine = &DieThunk;
            memcpy(player + GameAddress::kEntityHitHandlerOffset, &mine, sizeof(mine));
        }

        void GiveDieSlotBack()
        {
            uint8_t* const player = Player();

            if (player != nullptr &&
                Read<DieRoutine>(player, GameAddress::kEntityHitHandlerOffset) == &DieThunk)
            {
                const DieRoutine back = g_dying.gameDie;
                memcpy(player + GameAddress::kEntityHitHandlerOffset, &back, sizeof(back));
            }
        }

        void ReleaseDying(void*)
        {
            GiveDieSlotBack();
            g_dying = Dying{};
        }

        int __cdecl ApiSetDeathHandler(AmodMod mod, AmodPlayerDeathHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (handler == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "standing in front of a death needs a handler");
            }
            if (g_dying.owner != nullptr && g_dying.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is already standing in front of it");
            }

            uint8_t* const player = Player();
            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to keep alive");
            }

            const bool fresh = g_dying.owner == nullptr;

            g_dying.owner = caller;
            g_dying.handler = handler;
            g_dying.user = user;
            TakeDieSlot(player);

            if (fresh)
            {
                g_dying.token = caller->owned.Add(&ReleaseDying, nullptr);
            }
            return 1;
        }

        int __cdecl ApiClearDeathHandler(AmodMod mod)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (g_dying.owner == nullptr)
            {
                return 1;
            }
            if (g_dying.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "that death is another mod's to give back");
            }

            if (!caller->owned.Remove(g_dying.token))
            {
                ReleaseDying(nullptr);
            }
            return 1;
        }

        // Player_Spawn refills the slot each respawn; renew the claim.
        void __cdecl HoldSlot()
        {
            uint8_t* const player = (g_driving.owner != nullptr || g_dying.owner != nullptr) ? Player() : nullptr;
            if (player == nullptr)
            {
                return;
            }
            if (g_driving.owner != nullptr)
            {
                TakeSlot(player);
            }
            if (g_dying.owner != nullptr)
            {
                TakeDieSlot(player);
            }
        }

        void ReleaseDriving(void*)
        {
            GiveSlotBack();
            g_driving = Driving{};
        }

        int __cdecl ApiDrive(AmodMod mod, AmodPlayerDriver driver, AmodPlayerUpdateHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (driver < AMOD_PLAYER_DRIVER_GAME || driver > AMOD_PLAYER_DRIVER_NOBODY)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not one of the four drivers", static_cast<int>(driver));
            }
            if (handler == nullptr &&
                (driver == AMOD_PLAYER_DRIVER_MOD || driver == AMOD_PLAYER_DRIVER_BOTH))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "driving him needs a handler to drive with");
            }
            if (g_driving.owner != nullptr && g_driving.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is already driving the player");
            }

            uint8_t* const player = Player();
            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to drive");
            }

            const bool fresh = g_driving.owner == nullptr;

            g_driving.owner = caller;
            g_driving.driver = driver;
            g_driving.handler = handler;
            g_driving.user = user;
            TakeSlot(player);

            if (fresh)
            {
                g_driving.token = caller->owned.Add(&ReleaseDriving, nullptr);
            }
            return 1;
        }

        int __cdecl ApiRelease(AmodMod mod)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (g_driving.owner == nullptr)
            {
                return 1;
            }
            if (g_driving.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "the player is another mod's to hand back");
            }

            // Remove clears via ReleaseDriving, same as unload.
            const uint32_t token = g_driving.token;
            if (!caller->owned.Remove(token))
            {
                ReleaseDriving(nullptr);
            }
            return 1;
        }

        AmodPlayerDriver __cdecl ApiDriver(AmodMod mod)
        {
            return (Caller(mod) != nullptr && g_driving.owner != nullptr) ? g_driving.driver : AMOD_PLAYER_DRIVER_GAME;
        }

        int __cdecl ApiStep(AmodMod mod, int dirX, int dirY, int jump, float delta)
        {
            uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;
            const BodyStepRoutine step = reinterpret_cast<BodyStepRoutine>(GameModule::Resolve(GameAddress::kBodyStep));

            if (player == nullptr || step == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to step");
                return -1;
            }

            // Collision only accepts dir -1, 0, 1.
            const int x = dirX < 0 ? -1 : (dirX > 0 ? 1 : 0);
            const int y = dirY < 0 ? -1 : (dirY > 0 ? 1 : 0);
            return step(player + GameAddress::kPlayerBodyOffset, x, y, jump != 0 ? 1 : 0, delta, 0);
        }

        // Same grid as SDK ColumnAt/RowAt.
        constexpr float kRoomCellSize = 0.1f;
        constexpr float kRoomOriginX = -2.0f;
        constexpr float kRoomOriginY = 1.45f;

        int FloorToInt(float value)
        {
            const int truncated = static_cast<int>(value);
            return (value < 0.0f && static_cast<float>(truncated) != value) ? truncated - 1 : truncated;
        }

        int __cdecl ApiGetPlace(AmodMod mod, AmodPlayerPlace* out)
        {
            const uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (player == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to stand anywhere");
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetPlace was given nowhere to put it");
            }

            const PlaceFillRoutine fill = reinterpret_cast<PlaceFillRoutine>(GameModule::Resolve(GameAddress::kPlayerPlaceFill));
            if (fill == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "this build has no place to fill");
            }

            // Fill fresh (p_draw_place style); entity window is one step stale.
            uint8_t window[GameAddress::kPlayerCellWindowSize] = {};
            fill(player + GameAddress::kEntityPositionOffset, player + GameAddress::kPlayerBodyExtentOffset, window);

            AmodPlayerPlace filled = {};
            filled.structSize = sizeof(filled);
            filled.column = Read<int>(window, GameAddress::kPlayerCellWindowColumnOffset);
            filled.row = Read<int>(window, GameAddress::kPlayerCellWindowRowOffset);
            const float x = Read<float>(player, GameAddress::kEntityPositionOffset);
            const float y = Read<float>(player, GameAddress::kEntityPositionOffset + sizeof(float));
            // PlaceFill rounds nearest; World::Cell floors -- half-cell can disagree.
            filled.atColumn = FloorToInt((x - kRoomOriginX) / kRoomCellSize) - filled.column;
            filled.atRow = FloorToInt((kRoomOriginY - y) / kRoomCellSize) - filled.row;
            filled.columns = static_cast<int>(GameAddress::kPlayerCellWindowColumns);
            filled.rows = static_cast<int>(GameAddress::kPlayerCellWindowRows);
            memcpy(filled.cells, window + GameAddress::kPlayerCellWindowCellsOffset, sizeof(filled.cells));
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiAsk(AmodMod mod, AmodPlayerPlaceAsk question)
        {
            // PlaceAsk: __cdecl, arg [ESP+4], bare RET, returns int in EAX.
            typedef int(__cdecl* AskRoutine)(const void* window);

            static const uintptr_t kAsks[] = {
                GameAddress::kPlaceAskLeft,
                GameAddress::kPlaceAskRight,
                GameAddress::kPlaceAskClimb,
                GameAddress::kPlaceAskFooting,
                GameAddress::kPlaceAskPipe
            };

            const uint8_t* const player = Caller(mod) != nullptr ? Player() : nullptr;

            if (question < AMOD_PLAYER_ASK_LEFT || question > AMOD_PLAYER_ASK_PIPE)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not one of the five", static_cast<int>(question));
                return -1;
            }
            if (player == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "there is no player to ask about");
                return -1;
            }

            const AskRoutine ask = reinterpret_cast<AskRoutine>(GameModule::Resolve(kAsks[question]));
            const PlaceFillRoutine fill = reinterpret_cast<PlaceFillRoutine>(GameModule::Resolve(GameAddress::kPlayerPlaceFill));
            if (ask == nullptr || fill == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "that question is not in this build");
                return -1;
            }

            // Fill fresh like GetPlace; entity window is one step behind.
            uint8_t window[GameAddress::kPlayerCellWindowSize] = {};
            fill(player + GameAddress::kEntityPositionOffset, player + GameAddress::kPlayerBodyExtentOffset, window);
            return ask(window);
        }

        struct StateClaim
        {
            ModContext* owner;
            AmodPlayerStateHandler handler;
            void* user;
            uint32_t token;
        };

        StateClaim g_states[4] = {};
        InlineHook g_stateHooks[4];
        void* g_originalStates[4] = {};
        bool g_stateHooksTried = false;

        // true = mod took the frame; skip the game handler.
        bool RunState(int state, void* entity)
        {
            const StateClaim claim = g_states[state];

            if (claim.owner == nullptr || claim.handler == nullptr)
            {
                return false;
            }

            AmodPlayerFrame frame = {};
            FillFrame(static_cast<const uint8_t*>(entity), frame);
            return claim.handler(&frame, claim.user) != 0;
        }

        void __cdecl OnFootDetour(void* entity, void* extra)
        {
            if (!RunState(AMOD_PLAYER_STATE_ONFOOT, entity))
            {
                reinterpret_cast<StateUpdateRoutine>(g_originalStates[AMOD_PLAYER_STATE_ONFOOT])(entity, extra);
            }
        }

        void __cdecl LadderDetour(void* entity)
        {
            if (!RunState(AMOD_PLAYER_STATE_LADDER, entity))
            {
                reinterpret_cast<LadderUpdateRoutine>(g_originalStates[AMOD_PLAYER_STATE_LADDER])(entity);
            }
        }

        void __cdecl ChainDetour(void* entity, void* extra)
        {
            if (!RunState(AMOD_PLAYER_STATE_CHAIN, entity))
            {
                reinterpret_cast<StateUpdateRoutine>(g_originalStates[AMOD_PLAYER_STATE_CHAIN])(entity, extra);
            }
        }

        void __cdecl PipeDetour(void* entity, void* extra)
        {
            if (!RunState(AMOD_PLAYER_STATE_PIPE, entity))
            {
                reinterpret_cast<StateUpdateRoutine>(g_originalStates[AMOD_PLAYER_STATE_PIPE])(entity, extra);
            }
        }

        // Install state hooks on first claim only.
        bool EnsureStateHooks()
        {
            struct Site
            {
                uintptr_t address;
                void* detour;
                const uint8_t* prologue;
                size_t length;
                const char* name;
            };

            const Site sites[4] = {
                { GameAddress::kPlayerOnFootUpdate, reinterpret_cast<void*>(&OnFootDetour),
                  kPlayerOnFootUpdatePrologue, sizeof(kPlayerOnFootUpdatePrologue), "on foot" },
                { GameAddress::kPlayerLadderUpdate, reinterpret_cast<void*>(&LadderDetour),
                  kPlayerLadderUpdatePrologue, sizeof(kPlayerLadderUpdatePrologue), "ladder" },
                { GameAddress::kPlayerChainUpdate, reinterpret_cast<void*>(&ChainDetour),
                  kPlayerChainUpdatePrologue, sizeof(kPlayerChainUpdatePrologue), "chain" },
                { GameAddress::kPlayerPipeUpdate, reinterpret_cast<void*>(&PipeDetour),
                  kPlayerPipeUpdatePrologue, sizeof(kPlayerPipeUpdatePrologue), "pipe" }
            };

            if (g_stateHooksTried)
            {
                return g_stateHooks[0].IsInstalled();
            }
            g_stateHooksTried = true;

            for (int state = 0; state < 4; ++state)
            {
                void* const target = GameModule::Resolve(sites[state].address);
                std::string mismatch;

                if (target == nullptr ||
                    !GameModule::VerifyBytes(target, sites[state].prologue, sites[state].length, mismatch))
                {
                    FORGE_LOG_ERROR("Player_Update%s does not open as expected (%s)", sites[state].name, mismatch.c_str());
                    break;
                }

                std::string error;
                if (!g_stateHooks[state].Install(target, sites[state].detour, sites[state].prologue, sites[state].length, error))
                {
                    FORGE_LOG_ERROR("Cannot hook the %s state: %s", sites[state].name, error.c_str());
                    break;
                }
                g_originalStates[state] = g_stateHooks[state].Trampoline();
            }

            // All four hooks or none.
            for (int state = 0; state < 4; ++state)
            {
                if (!g_stateHooks[state].IsInstalled())
                {
                    for (int undo = 0; undo < 4; ++undo)
                    {
                        g_stateHooks[undo].Uninstall();
                        g_originalStates[undo] = nullptr;
                    }
                    return false;
                }
            }
            return true;
        }

        void ReleaseStateClaim(void* payload)
        {
            const int state = static_cast<int>(reinterpret_cast<uintptr_t>(payload));
            if (state >= 0 && state < 4)
            {
                g_states[state] = StateClaim{};
            }
        }

        int __cdecl ApiSetStateHandler(AmodMod mod, int state, AmodPlayerStateHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (state < AMOD_PLAYER_STATE_ONFOOT || state > AMOD_PLAYER_STATE_PIPE)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not one of the four states", state);
            }
            if (handler == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a state needs a handler to stand in for it");
            }
            if (g_states[state].owner != nullptr && g_states[state].owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod already stands in for that state");
            }
            if (!EnsureStateHooks())
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the state handlers could not be stood in front of");
            }

            const bool fresh = g_states[state].owner == nullptr;

            g_states[state].owner = caller;
            g_states[state].handler = handler;
            g_states[state].user = user;

            if (fresh)
            {
                g_states[state].token = caller->owned.Add(&ReleaseStateClaim,
                    reinterpret_cast<void*>(static_cast<uintptr_t>(state)));
            }
            return 1;
        }

        int __cdecl ApiClearStateHandler(AmodMod mod, int state)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (state < AMOD_PLAYER_STATE_ONFOOT || state > AMOD_PLAYER_STATE_PIPE)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not one of the four states", state);
            }
            if (g_states[state].owner == nullptr)
            {
                return 1;
            }
            if (g_states[state].owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "that state is another mod's to give back");
            }

            const uint32_t token = g_states[state].token;
            if (!caller->owned.Remove(token))
            {
                g_states[state] = StateClaim{};
            }
            return 1;
        }

        void ReleaseSubscription(void* token)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(token));

            for (auto it = g_subscriptions.begin(); it != g_subscriptions.end(); ++it)
            {
                if (it->id == id)
                {
                    g_subscriptions.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiAddEventHandler(AmodMod mod, AmodPlayerEvent event, AmodPlayerEventHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            if (event != AMOD_PLAYER_EVENT_DIED && event != AMOD_PLAYER_EVENT_REVIVED &&
                event != AMOD_PLAYER_EVENT_LIVES_CHANGED && event != AMOD_PLAYER_EVENT_SCORE_CHANGED)
            {
                return AMOD_INVALID_TOKEN;
            }

            Subscription item;
            item.id = g_nextSubscriptionId++;
            item.owner = caller;
            item.event = event;
            item.handler = handler;
            item.user = user;
            g_subscriptions.push_back(item);
            return caller->owned.Add(&ReleaseSubscription, reinterpret_cast<void*>(static_cast<uintptr_t>(item.id)));
        }

        void __cdecl ApiRemoveEventHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const caller = Caller(mod);
            if (caller != nullptr)
            {
                caller->owned.Remove(token);
            }
        }

        void Raise(AmodPlayerEvent event, int lives, int score, int wasLives, int wasScore, int crashed)
        {
            AmodPlayerEventInfo info = {};
            info.structSize = sizeof(info);
            info.event = event;
            info.lives = lives;
            info.score = score;
            info.wasLives = wasLives;
            info.wasScore = wasScore;
            info.crashed = crashed;

            // Snapshot so unsubscribe mid-walk is safe.
            const std::vector<Subscription> snapshot = g_subscriptions;
            for (const Subscription& item : snapshot)
            {
                if (item.event == event && item.handler != nullptr)
                {
                    item.handler(&info, item.user);
                }
            }
        }

        // Watched, not hooked: death flag set in several places; handler is after-the-fact.
        void __cdecl Watch()
        {
            const uint8_t* const player = Player();
            const int* const livesAt = LivesAt();
            const int* const scoreAt = ScoreAt();

            const int lives = livesAt != nullptr ? *livesAt : g_wasLives;
            const int score = scoreAt != nullptr ? *scoreAt : g_wasScore;

            if (player == nullptr)
            {
                g_wasDead = -1;
                g_wasLives = lives;
                g_wasScore = score;
                return;
            }

            const uint32_t flags = Read<uint32_t>(player, GameAddress::kPlayerFlagsOffset);
            const int dead = (flags & GameAddress::kPlayerFlagDead) != 0 ? 1 : 0;
            const int crashed = (flags & GameAddress::kPlayerFlagCrashed) != 0 ? 1 : 0;

            if (g_wasDead >= 0 && dead != g_wasDead)
            {
                Raise(dead != 0 ? AMOD_PLAYER_EVENT_DIED : AMOD_PLAYER_EVENT_REVIVED,
                      lives, score, g_wasLives, g_wasScore, crashed);
            }
            if (g_wasDead >= 0 && lives != g_wasLives)
            {
                Raise(AMOD_PLAYER_EVENT_LIVES_CHANGED, lives, score, g_wasLives, g_wasScore, crashed);
            }
            if (g_wasDead >= 0 && score != g_wasScore)
            {
                Raise(AMOD_PLAYER_EVENT_SCORE_CHANGED, lives, score, g_wasLives, g_wasScore, crashed);
            }

            g_wasDead = dead;
            g_wasLives = lives;
            g_wasScore = score;
        }

        void __cdecl AdjustMoveDetour(void* player, int* dirX, int* dirY)
        {
            const Moving claim = g_moving;

            if (claim.owner != nullptr && claim.handler != nullptr && dirX != nullptr && dirY != nullptr)
            {
                AmodPlayerMove move = {};
                move.structSize = sizeof(move);
                move.dirX = *dirX;
                move.dirY = *dirY;

                claim.handler(&move, claim.user);

                *dirX = move.dirX;
                *dirY = move.dirY;
            }

            // The game still squares it with the window he is standing in
            if (g_gameAdjustMove != nullptr)
            {
                g_gameAdjustMove(player, dirX, dirY);
            }
        }

        bool HoldMoveHook()
        {
            if (g_moveHookTried)
            {
                return g_moveHook.IsInstalled();
            }
            g_moveHookTried = true;

            void* const target = GameModule::Resolve(GameAddress::kPlayerAdjustMove);
            std::string mismatch;

            if (target == nullptr ||
                !GameModule::VerifyBytes(target, kPlayerAdjustMovePrologue, sizeof(kPlayerAdjustMovePrologue), mismatch))
            {
                FORGE_LOG_ERROR("Player_AdjustMove does not open as expected (%s)", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_moveHook.Install(target, reinterpret_cast<void*>(&AdjustMoveDetour),
                                    kPlayerAdjustMovePrologue, sizeof(kPlayerAdjustMovePrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Player_AdjustMove: %s", error.c_str());
                return false;
            }

            g_gameAdjustMove = reinterpret_cast<AdjustMoveRoutine>(g_moveHook.Trampoline());
            return true;
        }

        int __cdecl ApiSetMoveHandler(AmodMod mod, AmodPlayerMoveHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (handler == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetMoveHandler was given no handler");
            }
            if (g_moving.owner != nullptr && g_moving.owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod already has what he is told to do");
            }
            if (!HoldMoveHook())
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "his moving cannot be taken in this build");
            }

            g_moving.owner = caller;
            g_moving.handler = handler;
            g_moving.user = user;
            return 1;
        }

        int __cdecl ApiClearMoveHandler(AmodMod mod)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (g_moving.owner == caller)
            {
                g_moving = Moving{};
            }
            return 1;
        }
    }

    bool PlayerApi::Install()
    {
        FrameApi::AddInternalTick(&Watch);
        FrameApi::AddInternalTick(&HoldSlot);

        g_table.structSize = sizeof(AmodPlayerApi);
        g_table.IsHere = &ApiIsHere;
        g_table.Get = &ApiGet;
        g_table.SetPosition = &ApiSetPosition;
        g_table.SetPositionOnly = &ApiSetPositionOnly;
        g_table.SetCollides = &ApiSetCollides;
        g_table.Collides = &ApiCollides;
        g_table.SetFacing = &ApiSetFacing;
        g_table.Kill = &ApiKill;
        g_table.Burn = &ApiBurn;
        g_table.Revive = &ApiRevive;
        g_table.GetLives = &ApiGetLives;
        g_table.SetLives = &ApiSetLives;
        g_table.GetScore = &ApiGetScore;
        g_table.AddScore = &ApiAddScore;
        g_table.SetScore = &ApiSetScore;
        g_table.AddEventHandler = &ApiAddEventHandler;
        g_table.RemoveEventHandler = &ApiRemoveEventHandler;
        g_table.Cancel = &ApiRemoveEventHandler;
        g_table.Drive = &ApiDrive;
        g_table.Release = &ApiRelease;
        g_table.Driver = &ApiDriver;
        g_table.Step = &ApiStep;
        g_table.GetPlace = &ApiGetPlace;
        g_table.Ask = &ApiAsk;
        g_table.SetStateHandler = &ApiSetStateHandler;
        g_table.ClearStateHandler = &ApiClearStateHandler;
        g_table.SetDeathHandler = &ApiSetDeathHandler;
        g_table.ClearDeathHandler = &ApiClearDeathHandler;
        g_table.SetMoveHandler = &ApiSetMoveHandler;
        g_table.ClearMoveHandler = &ApiClearMoveHandler;

        FORGE_LOG_INFO("Player interface installed");
        return true;
    }

    void PlayerApi::Uninstall()
    {
        ReleaseDriving(nullptr);
        ReleaseDying(nullptr);

        for (int state = 0; state < 4; ++state)
        {
            g_states[state] = StateClaim{};
            g_stateHooks[state].Uninstall();
            g_originalStates[state] = nullptr;
        }
        g_stateHooksTried = false;

        g_moving = Moving{};
        g_moveHook.Uninstall();
        g_gameAdjustMove = nullptr;
        g_moveHookTried = false;

        g_subscriptions.clear();
        g_table = AmodPlayerApi{};
    }

    void PlayerApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_subscriptions.begin(); it != g_subscriptions.end();)
        {
            it = it->owner == owner ? g_subscriptions.erase(it) : ++it;
        }

        if (g_driving.owner == owner)
        {
            ReleaseDriving(nullptr);
        }
        if (g_dying.owner == owner)
        {
            ReleaseDying(nullptr);
        }
        if (g_moving.owner == owner)
        {
            g_moving = Moving{};
        }

        for (int state = 0; state < 4; ++state)
        {
            if (g_states[state].owner == owner)
            {
                g_states[state] = StateClaim{};
            }
        }
    }

    const AmodPlayerApi* PlayerApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
