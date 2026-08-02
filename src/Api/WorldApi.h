#pragma once
 
#include <WorldApi.h>
 
#include <stdint.h>
 
namespace Forge
{
    struct ModContext;

    class WorldApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();
 
        static const AmodWorldApi*                                  Table();
 
        static void                                                 ForgetMod(ModContext* owner);
 
        static bool                                                 IsLevelLoaded();
        static uint8_t*                                             CurrentRoom();

        // Every room of the level, whether the player is in it or not
        static int                                                  RoomTotal();
        static uint8_t*                                             RoomAt(int index);
 
        // Live entity list of the current room. Null ends the walk.
        static uint8_t*                                             FirstLiveEntity();
        static uint8_t*                                             NextLiveEntity(const uint8_t* entity);

        // The same order the mod-facing GetEntity counts in. Below zero is the current room.
        static uint8_t*                                             LiveEntityAt(int roomIndex, int index);
 
        // Null before a game is running.
        static const uint8_t*                                       Player();
        static uint8_t*                                             Session();
    };
}
 