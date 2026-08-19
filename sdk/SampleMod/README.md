# Demo level

A level plays itself from the scripts it names with `$demo`: `data/levels/<castle>/demo_NNN.txt`.
Each one is a list of `$move_cmd` blocks that drive the player, with `$cam_cmd` and `$view_cmd`
blocks driving the camera and `$time_cmd` bending game time.

One command, `demo`, reads all of that, writes to it, drives it and watches it.

| Type this | And you get |
| --- | --- |
| `demo` | every script of this level, the running one arrowed |
| `demo <n>` | one script written out whole — every move and every list on it |
| `demo play <n>` | that script from its first command |
| `demo stop` | the demo ended, whatever `$break_time` says |
| `demo next` | on to the level's next script |
| `demo watch` | starts, ends, breaks and every move as it is reached |
| `demo drive` | the mod drives the player instead of the script |
| `demo add <path>` | a script of the mod's own, read by the game's own parser |
| `demo remove <n>` | one the mod added, taken out |
| `demo suppress [0\|1]` | the level's own scripts dropped as the level loads |
| `demo break <n> <secs>` | `$break_time` |
| `demo set <n> <move> pause\|horz\|vert\|jump\|end <value>` | one field of one `$move_cmd` |
| `demo cam\|view <n> <move> <i> <x> <y> <z> [time] [auto]` | one `$cam_cmd` or `$view_cmd` |
| `demo time <n> <move> <i> <pause> <factor> <secs>` | one `$time_cmd` |
| `demo start <n> <x> <y> <z> <vx> <vy> <vz>` | `$camera_start` |
| `demo make` | a whole script built out of nothing, around where the player stands |
| `demo addmove <n> <pause> [horz] [vert] [jump] [end]` | a `$move_cmd` appended |
| `demo addcam\|addview <n> <move> <x> <y> <z> [time] [auto]` | a `$cam_cmd` or `$view_cmd` appended |
| `demo addtime <n> <move> <pause> <factor> <secs>` | a `$time_cmd` appended |

---

## Reading

```
demo
    demo: 1 script(s), playing
      -> 0  22 move(s)  break 0.02  no $player_place
    demo: on move 7 of script 0, 18.34s in

demo 0
    demo: script 0 starts the camera at -1.40 -0.40 1.10 looking -1.60 -0.60 0.00
       0  pause 8.00
            cam  -1.40 -0.40 1.10  0.01s
            cam  -0.30 -0.40 1.10  3.00s
            cam  -1.35 -0.30 2.80  3.00s  auto
            view -1.60 -0.60 0.00  0.01s
       1  horz 1 vert 0 jump 0 to -0.95
       2  horz 1 vert 0 jump 1 to -0.30
```

A move is a pause **or** a move, never both: the game reads `$pause` first and a non-zero one is
the whole command. A move runs until `$end_coord` passes the player's own coordinate.

## Driving

`demo drive` registers an input handler and returns non-zero from it, so what the script wanted is
thrown away and the player walks right and jumps twice a second instead. `demo drive` again gives
the script its player back.

That handler is the whole seam between a demo and a mod: it is handed the player's position and
hands back horizontal, vertical and jump — the same three the keyboard produces.

## Building one

`set`, `cam`, `view` and `time` write over a record that is already there, and a move the level
never gave a `$cam_cmd` has nothing to write over — `demo 0` says how many each move holds. The
`add` half makes them:

```
demo make               a script of three moves, a camera, a view and a time slice
demo addmove 1 0 1 0 1 0.5    walk right, jumping, until x passes 0.5
demo addcam 1 0 0 -1 3 2 1    a camera on move 0, two seconds, handed back at the end
```

A `$cam_cmd` with a time of zero is what the game's own parser throws away, so `addcam` refuses one
too.

Scripts and records made this way live until the level changes — nothing recreates them, because
there is no file to read them back from. A script from `demo add` does come back, being a file.

## A script of the mod's own

`demo add rooms/mine.txt` parses one with the game's own parser and appends it to the level's list.
It comes back for every level after that, so a mod that wants one level's demo removes it again on
the next. `demo suppress` drops what the level named, and the two together replace a level's demos
outright.

---

## Every call it demonstrates

| The call | Where to see it |
| --- | --- |
| `IsPlaying`, `Count`, `Current`, `Elapsed`, `MoveIndex`, `MoveCount` | `demo` |
| `GetMove`, `GetCamera`, `GetView`, `GetTime` | `demo <n>` |
| `GetCameraStart`, `GetPlace`, `GetBreakTime` | `demo`, `demo <n>` |
| `SetMove` | `demo set` |
| `SetCamera`, `SetView` | `demo cam`, `demo view` |
| `SetTime` | `demo time` |
| `SetCameraStart`, `SetBreakTime` | `demo start`, `demo break` |
| `Play`, `Stop`, `Next` | `demo play`, `demo stop`, `demo next` |
| `Add`, `Remove`, `SuppressLevelDemos` | `demo add`, `demo remove`, `demo suppress` |
| `OnDemo`, `OnMove` | `demo watch` |
| `OnInput` | `demo drive` |
| `Cancel` | both of those, switched off again |
| `Create`, `AddMove`, `AddCamera`, `AddView`, `AddTime` | `demo make`, and `demo addmove`, `addcam`, `addview`, `addtime` one at a time |
