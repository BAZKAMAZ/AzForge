# SampleMod

The bench the ctrls API is tried on. It adds three things the game never had — a lever, a purple
door and a purple key — and a `ctrl` console command that reaches every call in the interface.

Everything here is a worked example: if a call is in `CtrlsApi.h`, something below uses it.

---

## What it adds

### The lever

The game reads tags all over itself — every note asks after one — but nothing lets the player
**turn one on**. A tag's state is written only by the thing that wears it, so half of what tags
could say goes unsaid. The lever is that missing half: a thing the player throws, which turns its
tag on and off.

It is a kind of the mod's own (`RegisterKind`), so the game knows nothing about it.

Written in a room's `_ctrl.txt`:

```
$lever
{
  $cell_pos 34 14          where it stands
  $tag "door_lever"        the tag it turns on and off
  $active 0                its state to begin with
  $needs "key_c"           optional: an item without which it will not move
  $once 1                  optional: once thrown it stays thrown
  $sound "sounds/res_plat_up.wav"
  $texture "textures/ctrls/restore_plat.tga"
}
```

The handle slides between its two positions and fades from red to green as it goes. The colour is
eight models rather than one recoloured: a colour belongs to a model, and models are shared, so
repainting one would repaint every lever in the level at once.

Its state lives in its block, not on the thing, so a lever left thrown is still thrown after the
room has been walked out of and back into.

### The purple door

A door of the mod's own, three cells or taller. It is **not** one of the game's five: those have
their key wired into them in three separate places — the table that names them, the touch that
asks for the key, and the routine that decides whether their box pushes at all — so a different
key cannot be given to one from outside. Made rather than borrowed, all three are ours.

It is placed **by the room's grid**, the way the game's doors are, on a vertical run of `Q`:

```
W WWWWWW
 Q
 Q          three cells or more, in one column
 Q
WWWWWWWW
```

`Q` was picked because the letters that matter are taken: `F` is fire's, and `A`..`E` are the
game's five doors. The mod reads `Q` itself; the game does not know the letter and leaves it alone.

**No block is needed.** The doors are made when the level loads, from the grids of every room.

### The purple key

`key_e` — an ordinary bonus, added to `data/scripts/bonus_res.txt` with its own purple texture.
Being a bonus, the game carries it, counts it, draws it in the belt and takes it away when it is
spent, exactly like its own four keys.

A mod cannot define a bonus type at runtime: `BonusesApi` reads them and places them, but making a
new one is a line in `bonus_res.txt`.

---

## What the tags do

Two tags drive the door, and both are read with `FindByTag` — the same walk a note does when it
asks after one.

| tag | worn by | what it does |
| --- | --- | --- |
| `door_lever` | a lever | thrown → the door opens; not thrown → it closes again |
| `e_bridge` | a lever | thrown → the door **shrinks** open; not thrown → it **rides up** |

`e_bridge` is asked only while the door is shut, so the way it opens cannot change halfway
through. The key opens the door for good whatever the levers say afterwards.

Shrinking is eight copies of the door model at eight heights, standing on the same foot, swapped
by `SetLook`. The entity's own scale (`SetScale`) is what the game's doors use, but on any other
kind of thing it comes out skewed — it belongs to the door's own drawing, not to a thing in
general.

Nothing writes into another thing's block: the door and the levers talk only through tags, which
is what the game has them for.

---

## The `ctrl` command

`ctrl` alone lists what stands in the room: index, id, name, cell, tag and its state.

### One thing

| command | what it does |
| --- | --- |
| `ctrl <n>` | where it is and every key of its block |
| `ctrl <n> <key> <value...>` | write one of those keys |
| `ctrl <n> box <min3> <max3>` | what it takes up |
| `ctrl <n> hide` / `show` | whether the room draws it |
| `ctrl <n> remove` | take it away for good, this room and the level |

### Taking it over

| command | what it does |
| --- | --- |
| `ctrl <n> drive` | the mod moves it, and the game's own update does not run |
| `ctrl <n> freeze` | nobody moves it |
| `ctrl <n> game` | hand it back |
| `ctrl <n> touch` | say what meets it, and let its own rule run |
| `ctrl <n> eat` | say what meets it, and put its own rule aside |
| `ctrl <n> deaf` | stop listening, keep driving |
| `ctrl <n> needs <item>` | it stands still until that item is carried |
| `ctrl <n> unlock` | undo that |

`freeze` on a lift is the clearest of these: it stops mid-travel while the room goes on living.

### Tags

| command | what it does |
| --- | --- |
| `ctrl tag <name>` | who wears it and whether it is on |
| `ctrl <n> tagas <name>` | give it that tag, in its block as well as on the thing |
| `ctrl <n> tagon 0\|1` | turn its tag on or off |

### Making things

| command | what it does |
| --- | --- |
| `ctrl new <col> <row>` | a `$restore_plat`, built from the same text a level would write |
| `ctrl new <col> <row> ladder` | a ladder — placed by cells, not by a position |
| `ctrl lever <col> <row> [tag]` | a lever of the mod's own kind |
| `ctrl borrow <col> <row>` | a plate of the **game's** kind, driven by the game, heard by the mod |
| `ctrl strip <col> <row>` | a ladder taken apart: its models out of the room, then its cells |

`borrow` is the one to read if a mod wants a mechanic that already works and only needs something
added to it.

### Rooms and ids

| command | what it does |
| --- | --- |
| `ctrl room <r>` | what room `r` was told to hold, whether or not the player has been there |
| `ctrl room <r> <at> <key> <value>` | change one of those blocks — a lift's speed before he arrives |
| `ctrl id <#>` | what an id means, and which room it is in |

An index is only where a thing stands this frame. An id is the thing itself, and it is the same
number in every run of the same level, so a mod may be written around one.

---

## Trying it

The editor's bench, `az_orginal/levels/rooms_temp`, is set up for this: a run of `Q`, two levers
and a purple key.

```
door_lever off, e_bridge off     the door rides up when opened
door_lever off, e_bridge on      the door shrinks instead
door_lever on                    it opens; off again and it closes the same way
key_e                            opens it for good, and is spent doing it
```
