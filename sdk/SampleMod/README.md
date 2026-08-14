# The SampleMod console

Nine commands over the config file, the saves and what a level remembers. Open the console and type.
Every one of them prints its own usage with no arguments.

Most want a level being played. `cfg` and `slot` are the two that answer from the menu as well.

## Start here

```
cfg
scores
prog
slot
```

Four counts: how many config keys, how many scores and castles, how many palaces are open, and which
save slots hold a game.

## The config file

| Type this | And you get |
| --- | --- |
| `cfg` | how many keys there are |
| `cfg list` | every one, with its value |
| `cfg ed_enable` | that key: value, number, default, flags |
| `cfg ed_enable 1` | set it — and the in-game level editor comes up |
| `cfg mymod_speed 2.5` | a key the game never had; it is added and will be saved |
| `cfgsave` | write `Azangara.cfg` now instead of at quit |

```
cfg mymod_speed 2.5
    cfg: 'mymod_speed' is new, added as "2.5" and will be saved
cfgsave
    cfgsave: written - look in Azangara.cfg
```

Open the file and the line is there. Calling `Add` on every start is safe: a key already present
keeps whatever the player set.

## The leaderboard

`scores.dat`. Entries are grouped by castle, so the index is a walk across all of them.

| Type this | And you get |
| --- | --- |
| `scores` | the count, and which level each castle is for |
| `scores list` | every entry, and whether the next save keeps it |
| `scores add levels/level_t.txt Bob 25000` | a new one |
| `scores test` | writes name and points, reads them back, puts them back |
| `scores remove 0` | drops one |
| `scores save` | write `scores.dat` now |

**The level is a path, not a name.** `scores` with no arguments prints the ones the table knows —
copy one from there. `levels/level_t.txt`, not `level_t`.

## Palaces

`packs.sav` — the levels the player has opened, by menu id.

| Type this | And you get |
| --- | --- |
| `prog` | how many are open, and which pack |
| `prog list` | their ids |
| `prog open ID_CTRLY_GREEN` | opened |
| `prog close ID_CTRLY_GREEN` | closed, and blocked from reopening |
| `prog test` | Add, GetId, SetId, Remove, each read back — on ids of its own |
| `prog save` | write `packs.sav` now |

The button on the CASTLES screen is set when that screen is built, so **leave and come back** to see
a change.

Closing needs one more thing to stick across a restart: the screen reopens everything listed in
`$enabled_castles` in `scripts/menu_std_new.txt` every time it is built. Take the id out of that list,
or have your mod call `Progress::Block` in `AmodInit` — the block itself does not survive a restart.

## Saved games

One file a slot, `saves/saveNNN.sav`.

| Type this | And you get |
| --- | --- |
| `slot` | the slots that hold a game, with their name and level |
| `slot save 1 test` | this game written to slot 1 |
| `slot load 1` | that game back |
| `slot drop 1` | the file deleted |

**`slot save` needs a level being played.** From the menu there is nothing to save, and the command
says so rather than leaving a half-written file.

## A mod's own state in a save

`quest` keeps a step and a note, and they ride inside the game's own save file.

```
quest found the key
    quest: step 1, "found the key"
slot save 1 test
[restart the game]
slot load 1
    quest: slot 1 gave back step 1, "found the key"
```

A mod's bytes go after everything the game wrote, where the game never looks — so a save with them
still loads in a game with no loader at all.

## What a level remembers

`chg` is the same idea by name and value, and AzForge writes it into the slot for you.

| Type this | And you get |
| --- | --- |
| `chg` | every remembered change, with the level it belongs to |
| `chg door_7` | what that one stands at |
| `chg door_7 1` | set it on the level being played |

It is kept per slot and handed back before the level is drawn. Applying it is your mod's — only your
mod knows that `door_7` is a particular ctrl in a particular room.

`thing` is the other half: the records the game itself saves.

| Type this | And you get |
| --- | --- |
| `thing` | this room's records, its own line, and the bytes the whole level takes |
| `thing 3 1` | record 3 set — save the slot and it comes back changed |

```
thing
    thing: room 8 keeps 16, and takes 18 byte(s) in a save
       3  tag 9    1 byte(s)  1
      15  tag 10   1 byte(s)  1
      room line: saved, state 1, extra yes (116)
    thing: the level section takes 343 byte(s) of things and lines, over 16 room(s)
```

Tags 2, 9, 10 and 11 are saved; anything else is not, and shows `0 byte(s)`.

## The level statistic

The eight pairs the end-of-level screen turns into points.

| Type this | And you get |
| --- | --- |
| `stats` | all eight, got and total |
| `stats monsters 3` | that counter set |

`crystals`, `bonuses`, `super`, `monsters`, `light`, `dark`, `secret`, `eighth`. The first seven are
the game's own names, out of `$percent_str` in a statistic script. The eighth is scored and named
nowhere.

Set one and both the end-of-level screen and the next save follow it.

## What each one is really testing

| Command | The call behind it |
| --- | --- |
| `cfg`, `cfgsave` | `Config::Has`, `Get`, `Set`, `Add`, `Save` |
| `scores` | `Scores::Get`, `Add`, `SetName`, `SetPoint`, `Remove`, `Save` |
| `prog` | `Progress::Open`, `Close`, `SetId`, `Remove`, `Block`, `Save` |
| `slot` | `Progress::SlotExists`, `SlotName`, `SlotLevel`, `SaveSlot`, `LoadSlot`, `DeleteSlot` |
| `quest` | `Progress::OnSlotSave`, `OnSlotLoad`, `WriteSlotData` |
| `chg` | `Progress::SetLevelChange`, `GetLevelChange`, `LevelChangeAt` |
| `thing` | `World::GetRoomThing`, `SetRoomThing`, `GetRoomMemory`, `RoomSavedSize` |
| `stats` | `World::GetLevelStats`, `SetLevelStats` |
