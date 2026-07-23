# AzForge

A modding platform for **Azangara**, a 2.5D platformer by AxySoft.

AzForge loads into the original `game.exe` and lets mods replace its resources and drive
parts of it from code. The game is not modified: nothing is patched on disk, and removing
one file returns it to stock.

## What a mod can do

- **Replace any resource** — scripts, levels, rooms, textures, models, sounds. The loader
  inserts itself ahead of the game's own lookup, so the resolution order becomes
  mods → disk → `data.pak`.
- **Run code** — a mod is a DLL inside its container, given a table of functions at load.
- **Reshape the start-up launcher** — react to its buttons, move and retitle its controls,
  replace its logo.
- **Reshape the in-game menus** — find an element by the `$id` its script declares, move it,
  resize it, hide it, change its text.

Mods compose. Two mods that replace the same script file conflict and the last one installed
wins; two mods that move different menu elements simply both work.

## Layout

| Path | What it is |
|---|---|
| `WinmmProxy/` | `winmm.dll` proxy that gets AzForge into the process |
| `src/` | the loader itself, built as `AzForge.dll` |
| `AmodPack/` | `amodpack.exe`, the command line tool for `.amod` containers |
| `sdk/include/` | the headers a mod includes |
| `sdk/SampleMod/` | a complete mod, and the template a new one starts from |

## Building

Visual Studio, or MSBuild directly:

```
msbuild AzForge.slnx /p:Configuration=Release /p:Platform=x86
```

Everything is **Win32 by design** — `game.exe` is a 32-bit process, so an x64 build could not
be loaded at all. The static runtime is used throughout so that no redistributable is needed
on a player's machine.

Building produces `winmm.dll`, `AzForge.dll`, `amodpack.exe` and `SampleMod.amod`.

## Installing

Copy `winmm.dll` and `AzForge.dll` next to `game.exe`. Mods live in `mods/` beside them:

```
amodpack install MyMod.amod --game "C:\path\to\the\game"
amodpack list --game "C:\path\to\the\game"
```

`forge.log` appears next to the game and records what was found, what loaded and why anything
was refused.

## Writing a mod

A mod is a folder with a manifest, whatever resources it replaces, and optionally a DLL:

```
mod.json
files/scripts/local/dialog_eng.txt
bin/MyMod.dll
```

```json
{
  "id": "author.mymod",
  "name": "My Mod",
  "version": "1.0.0",
  "api_version": 3,
  "entry": "bin/MyMod.dll"
}
```

Paths under `files/` mirror the ones the game asks for, which are the same keys `data.pak`
uses: `scripts/…`, `levels/…`, `textures/…`, `models/…`, `sounds/…`, `music/…`.

Code mods include one header and link against nothing:

```cpp
#include <ForgeApi.h>

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
        return AMOD_ERROR_API_VERSION;

    Amod::Log(AMOD_LOG_INFO, "%s is up", Amod::ModId());
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodShutdown(void) { }
```

`sdk/SampleMod` builds this way and seals itself into a `.amod` as a post-build step, so a
mod can be written in plain Visual Studio with none of the tooling installed.

Mods must be **Win32** and use the **static runtime** (`/MT`), for the same reasons the
loader does.

## Notes on the interface

- Everything crosses the boundary as C, `__cdecl`, with no C++ objects and no exceptions.
- Tables only ever grow at the end, and each carries its `structSize`, so a mod built against
  an older version keeps working.
- Everything a mod registers is released when it is unloaded, in reverse order. A mod does
  not write teardown code that mirrors its setup.
- Anything the typed interface does not cover is reachable through a raw pointer, but only
  when the mod names the game layout it was written against.

## Compatibility

AzForge identifies the routines it needs by byte signature, checking the address it was
reversed from first. A signature that matches nowhere, or in more than one place, is refused
rather than guessed at, and the loader stands aside and leaves the game untouched.
