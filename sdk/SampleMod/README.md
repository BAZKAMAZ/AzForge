# The SampleMod console

Start a level, open the console and type. Most print something useful with no arguments at all —
`cellsweep` is the one exception, it writes.

## Start here

```
roomtex
panel
block
```

The first says which stone this room is made of. The second puts a panel on the screen — if you can
see it, the 2D calls work. The third prints the whole level file as a tree.

## The room's own textures

`roomtex [room]` — no number means the room you are standing in.

It prints the id, the path and the size of each:

```
roomtex
    roomtex: $wall_tex is <id> <texture>, <w>x<h>
    roomtex: $floor_tex is <id> <texture>, <w>x<h>
```

These are the objects the game is drawing with, not a second copy loaded by name. Use them when you
add geometry that has to match the room.

## Drawing in 2D

| Type this | And you get |
| --- | --- |
| `panel` | the panel appears — a rect, a bar, a line of text, two sprites |
| `panel` | again, and it is gone |

**Everything 2D is in 640x480**, whatever your window is. Not pixels. `Render::GetViewport` gives
the real size when you want pixels.

## Texture handles

`texhandle [n]` — walks texture `n` out to the game's numbering and back.

```
texhandle 12
    texhandle: 12 '<name>' is id <id>, the game calls it 0x<pointer>, and back is <id> - the same
```

The words to look for at the end are **the same**. Anything else means the round trip lost it.

You need this when a draw handler tells you which texture the game is using: it says so in the
game's numbering, and everything else in the API wants an `AmodTextureId`.

## Music

| Type this | And you get |
| --- | --- |
| `mus` | what is playing right now |
| `mus music/light3.ogg` | that track, faded in over whatever was playing |
| `mus music/light3.ogg cut` | the same, cut in with no fade |
| `mus stop` | silence |

Any `.ogg` under `music/` works, and so does one your mod serves for a path that does not exist on
disk — see `fileserve`.

## Reading game files

`fileread <path>` reads exactly what the game would read, out of `data.pak` or off the disk.

```
fileread levels/orange/002.room
    fileread: 'levels/orange/002.room' is 336807 byte(s), starting 4d 52 05 00 a7 23 05 00
```

`block [path]` reads a file and prints it as a tree. With no path it takes the level you are in.

Nested blocks are indented and braced, so what comes back reads like the file it came from:

```
block
    block: levels/level_orange.txt
    $header
    {
      $next_level "levels/bonus_orange.txt"
      $music "music/light3.ogg"
      ...
    }
    $rooms
    {
      $room
      {
        $room_file "levels/orange/002.room"
        $floor_tex "textures/rooms/orange/01_floor.bmp"
        ...
```

```
block levels/orange/002_ctrl.txt
block scripts/menu_cfg.txt
```


## Answering for a file

`fileserve` puts bytes in front of the game's own copy, in memory only

| Type this | And you get |
| --- | --- |
| `fileserve` | every path this mod is currently answering for |
| `fileserve some/path.txt hello` | that path now reads as `hello`, for the game too |
| `fileserve some/path.txt` | the game's own copy back |

```
fileserve levels/mine.txt hello
    fileserve: 'levels/mine.txt' now answers with 5 byte(s), and reads back as 5

fileread levels/mine.txt
    fileread: 'levels/mine.txt' is 5 byte(s), starting 68 65 6c 6c 6f ...
```

`fileread` there is going through the same door the game uses, and getting the mod's answer rather
than a file — there is no `levels/mine.txt` anywhere on disk. That is the whole point: a mod can
replace any room, script or texture the game asks for without touching `data.pak`.

## The five spare bytes in every cell

A cell of the room grid is eight bytes and the game reads one of them. Three come out of the file;
**the five from `AMOD_CELL_FREE_FIRST` are yours**, they already ride inside the `.room`, and
nothing in the game will touch them.

| Type this | And you get |
| --- | --- |
| `cellbyte 20 14` | all eight bytes of that cell, the game's three shown apart from your five |
| `cellbyte 20 14 1 10` | free byte 1 of that cell set to 10, and read straight back |
| `cellsweep 7` | free byte 0 of every cell in the room set to 7 |
| `cellcheck 7` | how many cells still hold it |

The pair that matters:

```
cellsweep 7
    cellsweep: 1160 cell(s) marked with 7, 0 did not hold it
```

now walk out of the room and back, then

```
cellcheck 7
    cellcheck: 1160 of 1160 cell(s) still hold 7
```

If the second number holds after a room change, the grid is a place your mod can keep things.

## What each one is really testing

| Command | The call behind it |
| --- | --- |
| `roomtex` | `World::WallTexture`, `World::FloorTexture` |
| `texhandle` | `Textures::ToGame`, `Textures::FromGame` |
| `panel` | `Render::Rect`, `Render::Sprite`, `Render::Text` with an align |
| `mus` | `Sound::PlayMusic`, `StopMusic`, `CurrentMusic` |
| `block` | `Data::ParseFile` and the `Data::Block` walk |
| `fileread`, `fileserve` | `Files::Read`, `Files::Serve`, `Files::Clear` |
| `cellbyte`, `cellsweep`, `cellcheck` | `World::CellRecord`, `CellByte`, `SetCellByte` |

## One trap worth knowing

`argv` points into the **game's own console scratch**. Anything you ask the game for may write over
it. Copy what you need before the first call out — every command here that takes a path does:

```cpp
char path[260] = {};
Keep(path, sizeof(path), argv[1]);
```
