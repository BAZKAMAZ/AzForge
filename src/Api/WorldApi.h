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

        // Changes on every load and every free, so what was gathered about one level can be told
        // apart from what belongs to the next.
        static uint32_t                                             LevelGeneration();

        // Set while a room's placement file is being read: there is no current room then, and no
        // table to look one up in, so a mod asking for "here" is answered with this.
        static void                                                 SetParsingRoom(uint8_t* room);
        static uint8_t*                                             ParsingRoom();

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
 