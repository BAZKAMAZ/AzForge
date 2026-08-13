# The monsters workbench

One console command, `monster`, that drives every part of `MonstersApi`. Load the mod, start a
level, open the console and type. Typing `monster` on its own lists this room's placements and
prints the commands.

## Start here

```
monster live
monster grab 0
monster grab off 0
```

The first prints what is standing here. The second takes that one monster over — it rises and
stops walking, because the mod is driving it now. The third gives it back to the game.

## The two numbers, before anything else

The game counts monsters in two different orders and mixing them is the commonest way to act on
the wrong one:

- **live** — monsters standing in this room. `grab`, `speed`, `anim` and `drive` count in these.
- **placed** — the room's placement list, in room-file order. `clear` and `killfar` count in these.

The live list is built at its head, so the newest monster is **first** there while its placement
is **last** in the file. They frequently read backwards from each other. Both listings print both
numbers, so you never have to guess:

```
monster live      live 0  placed 1  spider   tag ''
monster list      placed 0  live 1  spider   tag ''
```

## Looking

| Type this | And you get |
| --- | --- |
| `monster list` | the room's placements, with the live number beside each |
| `monster live` | what is standing here, with the placement number beside each |
| `monster speed 0` | what monster 0 actually walks at, and what `hard` claims |

## Making them appear and disappear

| Type this | And you get |
| --- | --- |
| `monster mine` | a monster of the mod's own making at 20 14 |
| `monster mine 24 10` | the same, at your cell |
| `monster mine totem 22 14` | one you walk straight through |
| `monster mine quiet 22 14` | one that is met, says so, and does nothing else |
| `monster mine stiff 22 14` | one whose hit box does not stretch with its animation |
| `monster far 3` | a spider in room 3 — walk there and it is standing |
| `monster far 3 bat 24 10` | that species, that room, that cell |
| `monster clear` | every placement here spent — no deaths, no score, no tally |
| `monster clear 3` | the same for room 3, without going there |
| `monster killfar 3 0` | placement 0 of room 3 killed, and the level counts it killed |
| `monster none` | no monster may be built at all — walk into a room to find it empty |
| `monster none off` | allowed again |

## Driving them

| Type this | And you get |
| --- | --- |
| `monster drive spider` | every spider rises, **and still walks** — the game's tick runs too |
| `monster drive spider instead` | every spider rises and stops walking — the game's tick does not |
| `monster drive off` | all of them handed back |
| `monster grab 0` | that one monster alone, whatever its name |
| `monster grab off 0` | given back |

`grab` beats `drive`: what is said about one monster wins over what is said about its whole name.

## Speed and animation

| Type this | And you get |
| --- | --- |
| `monster speed 0` | its real speed, its walking range, and what `hard` says |
| `monster speed 0 0.6` | it walks at that — and still will after you leave the room |
| `monster anim 0 move` | plays that frameset on monster 0 |
| `monster anim 0 stay` | another one, if its animation has it |

## Using it from your own mod

A kind of your own, which the game knows nothing about:

```cpp
const int kind = Monsters::RegisterKind("mymod.watcher", [](const AmodMonsterKindInfo* info)
{
    if (info->event == AMOD_MONSTER_EVENT_BUILD)
    {
        info->body->actsLike = "spider";      // contact: kills the player, dies to the knife
        info->body->animatedLike = "spider";  // and its animation, model and skin
        return 1;
    }

    if (info->event == AMOD_MONSTER_EVENT_TOUCH)
    {
        return 0;   // 0 lets what actsLike named happen as well
    }

    Monsters::At(info->think->index)["facing"].Set(info->think->position[0] > 0.0f ? -1 : 1);
    return 0;
});

Monsters::PlaceKind(kind, 20, 14, "first");
```

Register kinds in `AmodInit`, not on a command: a room file naming one is read the moment the
player walks in, and by then it is too late to ask.

A model and animation of your own, rather than a species':

```cpp
info->body->modelId = Mmd::Load("models/firedragon.mmd");
info->body->animationId = Mmd::LoadAnimation("scripts/anim_firedragon.txt");
info->body->textureId = Textures::Load("textures/firedragon.jpg");
info->body->frameset = "move";
```

Both files go inside the `.amod` and the loader serves them to the game. The animation file is the
game's own format, and `$range` counts frames of **your** model:

```
$animation
{
    $frameset { $name "move"   $fps 17 $range 0 6  $loop 1 }
    $frameset { $name "attack" $fps 20 $range 7 13 $break 1 $follow "move" }
}
```

Driving one of the game's own monsters:

```cpp
Monsters::SetBehaviour("spider", [](const AmodMonsterThinkInfo* info)
{
    return AMOD_MONSTER_ALSO_GAME;   // your handler ran; now the game walks it as usual
});
```

Return `AMOD_MONSTER_INSTEAD_OF_GAME` and the game's own tick does not run at all. `Borrow` takes
one monster rather than every monster of a name, and outranks `SetBehaviour` on that one.

Refusing monsters before they exist, which is how a level is emptied for a conversion:

```cpp
Monsters::OnPlace([](const AmodMonsterPlaceInfo* info)
{
    return 0;   // this placement never becomes a monster, in any room
});
```

`DefineKind` and `DefineBehaviour` are the older, simpler pair: they teach the game's spawner a
name that moves like one of its own. Reach for them when that is all you want; reach for
`RegisterKind` and `SetBehaviour` when you need your own entity, or a say over the game's tick.
