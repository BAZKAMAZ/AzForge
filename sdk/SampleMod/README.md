## Moving it by hand

| command | what it does |
| --- | --- |
| `cam eye <x> <y> <z>` | put the eye there this instant |
| `cam at <x> <y> <z>` | point it there |
| `cam up <x> <y> <z>` | which way is up; `0 1 0` is normal |
| `cam roll <radians>` | turn it about the view axis, as `$fx_rotate` does |
| `cam follow <x> <y> <z>` | give it a point to catch up to, at `$follow_speed` |
| `cam place <x> <y> <z>` | the same point, but arrive at once |
| `cam here` | put it where the player is |

Anything written this way lasts until the next frame, when the game's update drags it back. To make
it stick, stop the camera following:

```
cam lock eye 0           the eye stands still; the target still tracks him
cam lock at 0            neither end moves
cam lock eye 1           back to normal
```

These are the game's own two switches, not something the mod invented, so nothing is fighting.

---

## Effects

Three effects, one at a time, plus a shake that runs alongside any of them. The game fires one of
the three at random when a bonus is taken.

| command | what it does |
| --- | --- |
| `cam fx zoom` | pushes in over `$time_in` and drifts back out over `$time_out` |
| `cam fx rock` | swings about the view axis, `$angle` degrees, `$count` times |
| `cam fx rotate` | turns the world over, holds for `$time_pause`, turns back |
| `cam fx off` | whichever is running, stopped |
| `cam shake on` / `cam shake off` | `$event_rock`: the eye jitters about where it would be |

The shake has to be stopped on its own. Stopping the effect does not stop it.

---

## Driving it

`cam drive <who>` decides who moves the camera each frame.

| command | what it does |
| --- | --- |
| `cam drive orbit` | the mod's own camera: it circles the player |
| `cam drive chase` | the mod's own camera: it hangs behind and catches up by hand |
| `cam drive both` | the game moves it, then the mod moves it again |
| `cam drive nobody` | neither; it stays where it stands and the world slides past |
| `cam drive game` | given back |

`orbit` and `chase` are the same handler with two minds. Both ask the API where the player's focus
is rather than reading the entity, because the focus is not his position — it is his position
raised half his box, which is what the game itself looks at.

`chase` is the interesting one: it does its own catching-up, so the lag is the mod's arithmetic and
`$lag_dist` no longer means anything. That is what taking a domain over looks like.

`nobody` is worth trying while walking around. Nothing crashes, nothing snaps back — the camera is
simply not updated, which is the shape a cutscene wants.

---

## Drawing from somewhere else

`cam view` changes what the renderer is given, after everything above has had its say.

| command | what it does |
| --- | --- |
| `cam view flip` | up is negated: the world is drawn upside down |
| `cam view shoulder` | the eye is pushed aside, without the camera knowing |
| `cam view off` | the view is the game's again |

The handler returns a verdict. Zero means it only looked, and whatever it wrote is thrown away —
so a handler that gathers information cannot change the picture by accident.

`shoulder` shows the point of the seam: `cam` still reports the eye where the game put it, while
`drawn from` reports it a metre and a half to the side. The camera and the view are not the same
thing, and a mod can hold either one.

---

## The lens

The view says where the camera is. The lens says how much of the world fits in it, and it is not
part of the camera at all — the game builds it inline while rendering, from a field of its video
settings and three constants. `cam` prints the four numbers it was last built from.

| command | what it does |
| --- | --- |
| `cam lens fov <degrees>` | how wide it sees; the game's is what the options screen holds |
| `cam lens near <n>` | anything nearer is cut away |
| `cam lens far <n>` | anything further is cut away; the game's is **10** |
| `cam lens off` | all four back to the game's |

The far plane is the one to notice. Ten is close — the player is a third of a unit tall, so ten
units is about thirty of him. Fly the camera back with `cam drive orbit` and a bigger radius and
the room disappears into nothing; `cam lens far 60` brings it back.

The calls compose: each number above zero is taken and the rest are left alone, so `fov` then
`far` keeps both. Only the world is affected — a model a mod puts in front of the camera is drawn
through its own lens and is not disturbed.

---

## The snap

Every level and every room start puts the camera where the player is, at once. That happens
outside the update, so a mod driving the camera gets jerked without being told.

```
cam snap watch           print it when it happens, and let it happen
cam snap refuse          print it and refuse; the camera stays where it stands
```

Refusing is what a cutscene wants and what a normal game does not: refuse it before the first
room of a level and the camera begins wherever it happened to be. Told and allowed is the safe
setting, and the reason the handler returns a verdict rather than the mod simply being informed.

---

## Trying it

Any level will do — the camera is not a room's business. Start a game, open the console and:

```
cam                      what is going on
cam drive orbit          circle him
cam lens far 60          and now the room is still there behind him
cam view flip            circle him, upside down
cam snap refuse          walk into the next room and it does not jump
cam view off
cam lens off
cam drive game           everything back
```