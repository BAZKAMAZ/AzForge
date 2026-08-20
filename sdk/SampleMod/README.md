# Commands

Type a command with no arguments to see its forms
and the state it reports on.

Key names are the game's own: `space`, `escape`, `numpad0`, `f1`, `a`, `d`, and `joy_left`,
`joy_right`, `joy_down`, `joy_up`, `joy1`..`joy4`.

---

## keys

| Type this | And you get |
| --- | --- |
| `keys` | every bound key, and the totals |
| `keys all` | every key the game has a name for |
| `keys <name>` | one key: its binding, held flag and parameters |
| `keys bind <name> <command> [held] [parameters...]` | that key runs a console command |
| `keys cvar <name> <cvar> [held]` | that key sets a variable |
| `keys unbind <name>` | the key does nothing |
| `keys keep <name> [0\|1] [seal]` | the binding comes back next run |
| `keys press <name>` | one press, as though the player made it |
| `keys hold <name> [frames] [alone]` | held for `frames` (60 by default) |

`held` and the trailing flags are `0` or `1`.

**Nothing happens while the console is open.** The game ignores the keyboard then, and a forced key
is ignored with it — press or hold, then close the console and watch.

```
keys hold d 200        close the console: the player runs right for 200 frames
keys hold d 200 1      and `a` cannot push back
keys hold d 0          let go now
keys press space       one jump
```

## freeze

| Type this | And you get |
| --- | --- |
| `freeze` | the world stops, or starts again |
| `freeze 0\|1` | stopped, or running |

The game starts the world again on the next key press — that is its own rule, not the mod's.

## cfg

| Type this | And you get |
| --- | --- |
| `cfg <name>` | the value, as a number |
| `cfg <name> <value> [save] [seal]` | the variable is set |
| `cfg keep <name> [0\|1] [seal]` | the value comes back next run |

```
cfg r_wireframe 1
cfg s_volume 0.5 1        kept for next time
cfg s_volume 0.5 1 1      kept, and sealed
```

## score

| Type this | And you get |
| --- | --- |
| `score` | the whole table, and how many castles it spans |
| `score add <level> <name> <time> <points> [save] [seal]` | one entry added |
| `score keep <index> [0\|1] [seal]` | that entry comes back next run |
| `score remove <index>` | the entry goes |

## pack

| Type this | And you get |
| --- | --- |
| `pack` | which levels are open |
| `pack open <level> [save] [seal]` | the level opens |
| `pack close <level> [save] [seal]` | the level closes |
| `pack keep <level> [0\|1] [seal]` | its state comes back next run |

## mtl

Materials cut one model into parts, each with its own texture. **A level has to be loaded** — with
no models in memory every form answers with nothing.

| Type this | And you get |
| --- | --- |
| `mtl` | how many models are loaded |
| `mtl <model>` | that model's materials |
| `mtl split <model> <texA> [texB]` | the triangles cut in two, a texture each |
| `mtl add <model> <firstIndex> <triangles> <texture>` | one material by hand |
| `mtl clear <model>` | the game draws it whole again |
| `mtl save <model> <path>` | written as .mmd, materials and all |
| `mtl load <path>` | read one back |

```
mtl door
mtl split door metall dver
mtl save door mods/door.mmd
```

## font

The mod builds its own font: Windows draws the letters, so there is no art file anywhere.

| Type this | And you get |
| --- | --- |
| `font` | every font the game draws with |
| `font show <text>` | that line drawn at the top, in the mod's font |
| `font hide` | it stops being drawn |
| `font measure <text> [gamefont]` | how wide it is, and the line height |
| `font swap <gamefont>` | the game draws that font in the mod's instead |
| `font restore <gamefont>` | the game's own font is back |
| `font save <path.png>` | the atlas is written to a file |
| `font file <path> [cell]` | a font built from an atlas on disk |

```
font show AZFORGE
font swap bigitem        the menu changes with it
font restore bigitem
font save fonts/sample.png
font file fonts/sample.png 31
```

`font swap` changes what the whole game draws, menu items included. It goes back on `restore`, and
on unloading the mod.

---

## seal, save and keep

`keep` writes a setting into `mods/state/<mod id>.forge` so it comes back next run — never into
the game's own files. `save` on a form that changes something does the same in one step.

The last flag, `seal`, hides that record's values and ties them to this machine. Every record is
tagged either way: one edited by hand is refused on the way back in, with a line in `forge.log`.

## What each command shows off

| The interface | Where to see it |
| --- | --- |
| Input | `keys` |
| World | `freeze` |
| Config | `cfg` |
| Scores | `score` |
| Sav | `pack` |
| Mmd, Textures | `mtl`, `font save` |
| SpriteFont, Render | `font` |
