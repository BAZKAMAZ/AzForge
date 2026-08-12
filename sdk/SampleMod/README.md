# The bonuses workbench

One console command, `bonus`, that drives every part of `BonusesApi`. Load the mod, start a level,
open the console and type. Typing `bonus` on its own prints the list.

## Start here

```
bonus room
bonus borrow
```

The first lists what is lying in this room. The second takes all of it over: the bonuses start
pulsing and stop bobbing, and picking one up still scores it the game's way. `bonus borrow off`
gives them back exactly as they were.

## Looking

| Type this | And you get |
| --- | --- |
| `bonus room` | everything standing in this room, with its tag and cell |
| `bonus rooms` | how many bonuses each room of the level holds, without walking into them |
| `bonus carried` | what is in the five inventory slots, and how long each has left |
| `bonus prop 0` | everything placement 0 says about itself |
| `bonus watch` | reports every placement as a room stands it up - then walk into a room |
| `bonus watch off` | quiet again |

## Placing

| Type this | And you get |
| --- | --- |
| `bonus place` | a `key_a` at 20 14 |
| `bonus place key_e 22 12` | that type, that cell |
| `bonus place key_e 22 12 textures/console.jpg` | the same, wearing a texture of its own |
| `bonus mine` | defines `azforge.mine` from nothing - run it **outside** a level, then place it |
| `bonus mine carry` | defines `azforge.minekey` instead, which goes into a slot rather than scoring |
| `bonus kind` | a bonus of the mod's own making at 20 14 |
| `bonus kind 24 10` | the same, at your cell |
| `bonus far 3` | one in room 3 - walk there and it is standing |
| `bonus far 3 24 10` | room 3, your cell |

## Taking over what the level placed

| Type this | And you get |
| --- | --- |
| `bonus borrow` | every bonus in the room driven by the mod: pulsing, not bobbing |
| `bonus borrow bob` | the same, but the game's own tick still runs, so they go on bobbing |
| `bonus borrow off` | all of them handed back, colours and all |
| `bonus hah` | every bonus changes into another type's model and texture once a second |
| `bonus hah off` | back to what they came in |

Borrowing follows you: walk into another room and its bonuses are taken over too.

## Changing one bonus

`bonus prop <n>` reads, `bonus prop <n> <key> <value>` writes. `<n>` is the number `bonus room`
prints on the left.

| Type this | And you get |
| --- | --- |
| `bonus prop 0` | every key and what it says |
| `bonus prop 0 tag mine` | gives it a tag, so `bonus take mine` can find it |
| `bonus prop 0 decor 1` | stops the level counting it |
| `bonus prop 0 time 5.0` | its own timer, overriding the type's |
| `bonus prop 0 pos 1.2 0.4 0.1` | moves it |
| `bonus prop 0 is_live 0` | spends it, as though it had been picked up |

`name` is read-only and will refuse: it is what the level counts by.

## Removing

| Type this | And you get |
| --- | --- |
| `bonus take mine` | the bonus tagged `mine` goes |
| `bonus take 20 14` | whatever is standing on that cell goes |
| `bonus takeall key_a` | every `key_a` in the room goes at once |
| `bonus none` | no placement is allowed to appear at all - walk into a room to find it empty |
| `bonus none off` | allowed again |

## Inventory

| Type this | And you get |
| --- | --- |
| `bonus events` | a line whenever a slot empties, and whenever the scoring chain moves |
| `bonus events off` | quiet |
| `bonus hold 0` | slot 0 stops counting down |
| `bonus hold 0 off` | it counts down again |
| `bonus expire 0` | slot 0 has half a second left, so you can see it run out now |
| `bonus move 0 2` | slot 0's contents move to slot 2 |
| `bonus swap 0 2` | the two trade places |

## Things worth knowing before you decide something is broken

**A type can only be defined outside a level.** `bonus mine` inside one refuses and says so. Go to
the menu, run it, then start a level and place it.

**`bonus mine` looks wrong on purpose.** It wears a wall texture, because that is one the game
certainly has. It is meant to be visible, not pretty.

**`bonus kind` is not `bonus mine`.** `mine` teaches the game a new bonus type and lets the game
run it. `kind` is the mod's own thing from top to bottom - the game's bonus table never hears about
it, and the mod answers for what it looks like, what it does every frame and what taking it does.

**A kind can be written into a room file.** Put `$name "azforge.mineKind"` in a `$bonus` block and
it appears when you walk in - the same name the mod registered. `$tag` in that block works too.

**`bonus far` puts nothing on screen.** That is the point: the room has the placement, and the game
builds it when you arrive. `bonus rooms` will show it before you go.

**`bonus hah` always matches.** The model and the texture are taken as a pair off one of the game's
own bonus types, so the skin always belongs to the shape.

**Removing does not make things vanish that instant.** Taking a bonus marks it; the game sweeps the
room a frame or so later.

## Using it from your own mod

A type the game runs, defined from nothing:

```cpp
Bonuses::NewType("mymod.gem")
    .Model("models/bonus/key.mmd")
    .Texture("textures/bonus/key_purple.tga")
    .PointsOnTake(500)
    .Behaves(AMOD_BONUS_LIKE_SCORED)
    .Define();
```

A kind of your own, which the game knows nothing about:

```cpp
const int kind = Bonuses::RegisterKind("mymod.spinner", [](const AmodBonusKindInfo* info)
{
    if (info->event == AMOD_BONUS_EVENT_BUILD)
    {
        info->body->model = "models/bonus/key.mmd";
        info->body->texture = "textures/bonus/key_purple.tga";
        info->body->pointsOnTake = 250;
        return 1;
    }

    if (info->event == AMOD_BONUS_EVENT_UPDATE)
    {
        info->body->position[1] += info->deltaTime * 0.05f;
        return 0;
    }

    return AMOD_BONUS_TAKE_DEFAULT;
});

Bonuses::PlaceKind(kind, 20, 14, "first");
```

Register kinds in `AmodInit`, not on a command: a room file naming one is read the moment the
player walks in, and by then it is too late to ask.

Taking over a bonus the level placed, so your kind drives it instead of the game:

```cpp
Bonuses::BorrowPlacement(kind, Bonuses::IndexOfTagged("door_key"));
```

Return `0` from `UPDATE` and the game's own tick still runs after your handler; return anything
else and it does not. Return `AMOD_BONUS_TAKE_DEFAULT` from `TAKE` and the game scores, sounds and
counts it exactly as it always would.

Swapping the look every frame goes by handle, never by name - a name has to be looked up:

```cpp
const AmodTextureId skin = Textures::Load("textures/bonus/key_purple.tga");
info->body->textureId = skin;
```
