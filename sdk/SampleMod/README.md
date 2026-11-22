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

## Starting up

A mod that reads files in `AmodInit` blocks the game while it does, and nothing is drawn in the
meantime: the loader repaints the loading screen BETWEEN mods, and inside one there is only
whatever was on the screen last. At startup that is nothing at all, which is the white window.

Three calls, cheapest first. Off the loading screen all of them do nothing and say so, so the same
lines are safe to leave where they are.

| Call | What it does |
| --- | --- |
| `Stage::Loading()` | whether there is a loading screen to paint at all |
| `Stage::LoadingSay(text)` | a line on the game's own screen, painted there and then |
| `Stage::LoadingStep(percent, text)` | the same, and moves the bar |
| `Stage::LoadingFrame(draw, user)` | a whole frame of the mod's own |

`LoadingFrame` hands the screen over: the loader opens the frame, calls back, and shows it, and
inside the callback everything a mod draws with normally works — rects, text, sprites, models.
So the screen a mod puts up while it loads can be the screen it wants, not a line on somebody
else's.

```c
void __cdecl MyLoadingScreen(const AmodFrame*, void* user)
{
    const int done = *(const int*)user;

    Stage::Clear(0xFF101018);
    Render::Rect(0.0f, 0.0f, AMOD_MENU_WIDE * done / 100.0f, 8.0f, 0xFFE0A040);
}

for (int at = 0; at < many; ++at)
{
    LoadOneThing(at);

    const int done = (at + 1) * 100 / many;

    Stage::LoadingFrame(&MyLoadingScreen, &done);
}
```

The bar a mod moves with `LoadingStep` is put back where the game had it before the next mod runs,
so one mod's progress is never mistaken for another's.

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
| `azm shader` | the model drawn by the mod's own vertex shader, on or off |
| `azm skin <path> [material]` | a texture from anywhere put on one of its materials |
| `azm off` | the model put down |

```
stage take
azm models/car.azm      parts, materials, points, boxes and bounds are printed
azm tint ff4020         red; whatever the file baked shows through it
azm tint                back to its own colours
azm many 200            two hundred of it in one batch; read the frame in the corner
azm paint 2060ff        the model's own vertices are rewritten
azm shader              twelve lights on it, summed in one vertex shader
azm off
```

Anything the file calls a `wheel` turns on its own pivot. A tint belongs to one draw; paint belongs
to the model, so every copy of it wears the new colour.

A material's texture is found on load: beside the model first, then by the path exactly as the
`.mtl` writes it. So `map_Kd textures/paint.png` reaches `resmod/textures/paint.png` and nothing
needs copying next to the model. `Azm::SetMaterialTexture` is for the other case — a texture the
game makes up as it runs, from a path of its own, from pixels it built, from anywhere at all.

```
azm models/testcar.azm
azm skin textures/dirt.png 0     the paint wears something else
azm skin                          and takes it off again
```

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

### A model through a shader

`Shader::DrawTriangles` draws loose vertices. To put a whole model through your own shader use
`Azm::DrawShaded` for a `.azm`, or `Shader::DrawModel` for one engine mesh — both issue the
draw themselves, so the game's per-draw FVF never overwrites what you Set.

| Call | What it draws |
| --- | --- |
| `Azm::Queue` then `Azm::FlushShaded()` | the whole queue at once, sorted; what a street wants |
| `Azm::DrawShaded(model, matrix)` | every visible part, posed, one draw a material |
| `Shader::DrawModels(draws, count)` | many engine meshes at once, sorted the same way |
| `Shader::DrawModel(model, matrix, texture)` | one mesh; texture 0 keeps the mesh's own |
| `Shader::SetTransformRegisters(wvp, world)` | moves those two matrices, -1 for neither |
| `Shader::SetTintRegister(reg)` | where a draw's tint lands, -1 for nowhere |

The loader fills the matrices before each draw:

| Register | What is in it |
| --- | --- |
| `c0..c3` | world * view * projection, transposed — `dp4 oPos.x, v0, c0` |
| `c4..c7` | the world matrix, transposed — `dp3 r1.x, v1, c4` for the normal |

A draw's tint reaches the shader only if you ask for it: `Shader::SetTintRegister(40)` puts red,
green, blue and alpha in `c40` before each part, and `Azm::SetPartTint` then works through your
own shader the way it does through the game's. Without that call nothing is written and a tint is
simply ignored.

One draw at a time says every state again for every model. A batch says it once for a run: sorted
by blend, then model, then texture, then tint, so a street of two hundred cells on one model binds
its stream once. Only the matrices are written per draw, because only they differ. `azm many 40`
with `azm shader` on goes through that path.

Everything from `c8` up is yours. The vertex is the game's own: `v0` position, `v1` normal,
`v2` diffuse, `v3` texture corner. A material with no texture of its own gets one white pixel bound
to `t0`, so `tex t0 ; mul r0, t0, v0` is right whether the model is textured or vertex-coloured.

```
stage take
azm models/car.azm
azm shader          twelve lamps turning around it, its own colours underneath
azm many 40         forty of them, each one its own draw
azm shader          the game's path again
```

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
| Shader | `shader`, `skin`, `azm shader` |
| Textures, Mmd | `stage take` builds both by hand |
| Sound | `window sound` |
| Input | `window cursor` |
