# SampleMod

Console commands. Open the console and type one with no arguments to see its forms and what it
currently reports.

**The game boots as itself. Nothing is taken over until you ask for it** — `stage take` is the
asking, and `stage give` hands everything back. Start there: `quads`, `models` and `azm` need the
mod to be holding the frame before they have anywhere to draw.

---

## stage

Who runs the frame.

| Type this | And you get |
| --- | --- |
| `stage` | who holds the frame, the input and the boot step, and what is being drawn |
| `stage take` | the mod runs the world and draws its own: one cube, one texture, both of its own making |
| `stage over` | the mod holds the frame and runs the game's world under it |
| `stage all` | the shell as well: no menus, no game states, nothing of the game per frame but the keyboard poll and Present |
| `stage give` | the game has its frame back |

```
stage take          a cube on a dark blue field
stage               how many frames the mod has run
stage give          back to whatever was there before
```

## window

The game's window, once a mod has it.

| Type this | And you get |
| --- | --- |
| `window` | how big it is and where |
| `window full` | borderless over the whole screen |
| `window windowed [w] [h]` | a frame again, at that size (800x600 by default) |
| `window cursor [1\|0\|-1]` | the system cursor shown, hidden, or the game's own again |
| `window sound <path.wav>` | that file loaded and played |

```
window full
window windowed 1280 720
window cursor 1
window sound sounds/hit.wav
```

The backbuffer keeps its size, so the picture stretches rather than being redrawn larger.

## quads

Many 2D quads in one draw.

| Type this | And you get |
| --- | --- |
| `quads` | how many the batch draws, and off which texture |
| `quads <count>` | that many turning quads, drawn in ONE call |
| `quads off` | none |

```
stage take
quads 2000          drawn as one call, off the mod's own texture
quads off
```

Without `stage take` there is no texture yet and they come out in plain colour.

## models

The same measurement in 3D.

| Type this | And you get |
| --- | --- |
| `models` | how many were drawn, and what a frame costs |
| `models <count>` | that many turning cubes, drawn as ONE sorted list |
| `models off` | none |

```
stage take
models 900          the count, the average frame and the worst one
models              read it again after it has settled
models off
```

The list is sorted, so a texture and a blend state are set once per run rather than once per model.
Every eighth cube is added rather than solid, to give the sort something to gather. Run
`stage take` first or the reading counts frames the mod never ran.

## fatal

| Type this | And you get |
| --- | --- |
| `fatal <text>` | that line as the game's own fatal error |

```
fatal the map has no start marker
```

A line in the console, a message box with the game's name on it, and the game is asked to close.
It is the game's own, not a box of the mod's making - and it RETURNS, so the
frame that called it finishes.

## azm

A model with parts, materials and colour in the vertex.

| Type this | And you get |
| --- | --- |
| `azm <path.azm>` | that model loaded, drawn, and everything it holds printed |
| `azm` | the same listing again |
| `azm tint <rrggbb>` | a colour multiplied over this draw of it |
| `azm tint` | and that taken off again |
| `azm paint <rrggbb>` | every paintable material recoloured, in the model itself |
| `azm many <n>` | that many copies of it, in one sorted batch, with the frame time so far |
| `azm off` | the model put down |

```
stage take
azm models/car.azm      parts, materials, points, boxes and bounds are printed
azm tint ff4020         red; whatever the file baked shows through it
azm tint                back to its own colours
azm many 200            two hundred of it in one batch; read the frame in the corner
azm paint 2060ff        the model's own vertices are rewritten
azm off
```

Anything the file calls a `wheel` turns on its own pivot. A tint belongs to one draw; paint belongs
to the model, so every copy of it wears the new colour.

Build a `.azm` with the tool beside the loader:

```
AzmPack.exe pack car.obj -o car.azm --sidecar car.json
AzmPack.exe dump car.azm
```

`sdk/samples/car` holds a car to try it on — a body, glass, four wheels, two lamps, a collision box
and two named points.

## shader

vs/ps 1.1 shaders. Needs `stage take` first.

| Type this | And you get |
| --- | --- |
| `shader` | a quad lit by ten lamps in one vertex shader, on or off |

```
stage take
shader              a soft ten-colour square, the lamps turning
shader              off
```

The shader is assembled from text — through `d3dx8.dll` if the machine has it, the loader's own
assembler otherwise; `forge.log` says which. Ten lamps summed in one shader: eight is the
fixed-function cap, not the shader's.

## skin

Bone deformation in a shader. Needs `stage take` first.

| Type this | And you get |
| --- | --- |
| `skin` | a bar bent by two bones, on or off |

```
stage take
skin                a bar that bends, its foot still and its tip swinging
skin                off
```

Every vertex carries a weight and blends two bone matrices in the vertex shader, so the bar bends
like a limb. Fixed function cannot deform a mesh by bones at all.

---

## What each command shows off

| The interface | Where to see it |
| --- | --- |
| Stage | `stage` |
| Render, the window | `window` |
| Render, batched | `quads` |
| Mmd, batched | `models` |
| Azm | `azm` |
| Fatal | `fatal` |
| Shader | `shader`, `skin` |
| Textures, Mmd | `stage take` builds both by hand |
| Sound | `window sound` |
| Input | `window cursor` |
