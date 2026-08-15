`TAB` walks between the room screen and the maze screen, `B` picks a brush, `M` a group, `C` empties
the room, `F3` loads `levels/temp.txt`. `F2` would save and run it, but SampleMod takes that key.

| Command | What it does |
| --- | --- |
| `edit` | the mod's own screen |
| `brush` | brushes and a group of the mod's own |
| `maze` | the grid of rooms |
| `part` | the game's editor's looks |
| `savelevel` | write the whole level out |

---

## `edit` — an editor of the mod's own

| Type this | And you get |
| --- | --- |
| `edit` | the mod's screen up, or down again |
| `edit world` | whether entities keep updating |
| `edit render` | whether the 3D world keeps being drawn |
| `edit input` | whether the game still acts on keys — Esc, the player |
| `edit game` | whether the game's own editor still draws and takes keys |

The four toggles are independent and each one can be left alone. Arrows move a marker while the
mod's screen is up.

`edit` down does not stop the backdrop, the key capture or anything under `part` — those are what
the game's editor *is*, not what stands in for it.

```
edit game
    edit: world stops, render stops, input stops, game editor shows
```

---

## `brush` — brushes of the mod's own

| Type this | And you get |
| --- | --- |
| `brush` | every group and brush listed, the held one arrowed, the mod's marked `(mine)` |
| `brush add` | a "Sample" group with three brushes |
| `brush remove` | all of it taken out again |

```
brush
    brush: group 0 'Geometry' paints grid 0, holds 14
    brush: group 1 'Entities' paints grid 1, holds 19
    brush: group 2 'Sample'   paints grid 0, holds 2
      ->  0  cell 'W'  key '1'  FFFFFFFF  Sample Wall       (mine)
```

The three brushes:

| Brush | Key | What it shows |
| --- | --- | --- |
| `Sample Wall` | `1` | the plain case — the handler returns non-zero and the loader writes the cell. Its `texture` is what a painted cell looks like |
| `Wall 3x3` | `2` | writes nine cells around the one it was handed, then returns zero |
| `Sample Pickup` | `3` | the entities grid: a `draw` handler of its own, and blocks written into `_bonuses.txt` and read back |

A group paints one of the two grids the room editor holds; which brushes are in it is the mod's.
`M` walks past the game's two into it.

---

## `maze` — the grid of rooms

| Type this | And you get |
| --- | --- |
| `maze` | the size, the cursor, and a map: `#` a room, `.` empty, `@` the cursor |
| `maze goto <x> <y>` | the cursor onto a cell that already exists |
| `maze new <x> <y>` | a room there, the maze grown to reach it, the cursor on it |
| `maze del <x> <y>` | the room freed and the cell emptied |
| `maze clear <x> <y>` | both grids of a room emptied and the wall border put back |
| `maze row <at>` | a row inserted before that index |
| `maze col <at>` | a column, likewise |
| `maze save <x> <y> [path]` | one room out to a constructor file |
| `maze load <x> <y> [path]` | one room in from one, over whatever was there |

The path defaults to `rooms/sample.txt`.

```
maze new 2 1
    maze: room 2,1 made and crossed
    maze: cursor on 2,1
maze
    maze: 3x2, cursor on 2,1
      ...
      ..@
```

Every grid and brush call answers for the cell the cursor is on, so `maze goto` is how a mod edits
a room it is not standing in. On a fresh maze start with `maze new 0 0`.

Saving writes the `.txt` and the `_ctrls`, `_monsters` and `_bonuses` files beside it.

---

## `savelevel` — write the level out

| Type this | And you get |
| --- | --- |
| `savelevel` | the level written to `levels/sample.txt` |
| `savelevel <path>` | the same, somewhere else |

Every room goes out beside it, compiled into the `.room` files the level points at. SampleMod fills
in what the game writes as constants: per-room textures and `$type` in the level file, and per-room
ambient light in each room file — the rooms come out light, dark, light on a checkerboard.

---

## `part` — the game's editor's looks

| Type this | And you get |
| --- | --- |
| `part` | all five pieces listed, and the four status lines |
| `part <piece> off` | that piece hidden |
| `part <piece> on` | it back |
| `part <piece> <x> <y> [size] [scale]` | it moved |
| `part say <line> <text>` | one of the four status lines written by the mod |
| `part say <line>` | that line given back to the game |
| `part name <group> <index> <text>` | a brush renamed, the game's own included |
| `part name <group> <index>` | its name back |
| `part colour <group> <index> <AARRGGBB>` | a brush recoloured |
| `part colour <group> <index> 0` | its colour back |
| `part icon 0 <path>` | the editor's mouse cursor |
| `part icon 1 <path>` | what an empty maze cell shows — maze screen only |
| `part icon <0\|1>` | the game's own sprite back |
| `part screen <0\|1>` | the room screen or the maze screen, the same as TAB |
| `part back [on\|off]` | a backdrop under the whole editor |

The five pieces, and what `size` and `scale` mean for each:

| Piece | x, y | `size` | `scale` |
| --- | --- | --- | --- |
| `grid` | its top left corner | one cell, 12 | — |
| `lines` | follows `grid` | — | — |
| `status` | the first of the four lines | the step to the next, 15 | the letters, 1 |
| `palette` | the top of the brush list | the step to the next row, 15 | the letters, 1 |
| `maze` | — | the maze screen's zoom, 0.1 to 2 | — |

y is measured from the top, as `Render` measures it.

```
part status off
part palette 530 20 24 1.6
part grid 100 60 8
part say 0 "Brush: %s"
part colour 0 1 FF4488FF
part back on
```

A `%s` in a status line is filled with the held brush on line 0 and the group on line 1. Everything
else is drawn as it stands.

---

## Every call it demonstrates

| The call | Where to see it |
| --- | --- |
| `Enter`, `Leave`, `IsActive`, `SetMode`, `IsGameEditorOn` | `edit` |
| `OnFrame`, `OnDraw`, `OnDraw(AMOD_EDITOR_DRAW_WORLD, …)` | the mod's screen |
| `AddGroup`, `AddBrush`, `GetBrush`, `GetCurrentBrush`, `RemoveBrush`, `RemoveGroup` | `brush` |
| `GetGridCell`, `SetGridCell`, `GridSize` | the paint and save handlers |
| `MazeSize`, `GetMazeCursor`, `SetMazeCursor`, `HasRoom` | `maze` |
| `CreateRoom`, `RemoveRoom`, `ClearRoom`, `InsertRow`, `InsertColumn` | `maze new`, `del`, `clear`, `row`, `col` |
| `LoadRoom`, `SaveRoom` | `maze save`, `maze load` |
| `OnRoomCreated`, `OnRoomLoaded` | `maze new`, `maze load` |
| `OnRoomWrite`, `OnLevelWrite`, `SaveLevel` | `savelevel` |
| `GetPart`, `SetPart` | `part` |
| `SetStatusLine`, `GetStatusLine` | `part say` |
| `SetBrushLabel`, `SetBrushColour` | `part name`, `part colour` |
| `SetIcon` | `part icon` |
| `GetScreen`, `SetScreen` | `part screen` |
| `OnDraw(AMOD_EDITOR_DRAW_BACKDROP, …)` | `part back` |
| `OnKey` | F2, which the mod takes |
