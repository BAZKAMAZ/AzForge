# AzForge Mod Tools

Writing a mod for Azangara from VS Code: a new project, a build that packs and installs it, and a
debugger that stops on breakpoints inside the running game.

The extension does not define a second way to build a mod. A mod project is the same `.vcxproj`
Visual Studio uses, importing the same `AzForge.Mod.props`, and every command here ends in MSBuild
running the same targets. A mod started in VS Code opens in Visual Studio with nothing changed, and
the other way round.

## What you need

* The C/C++ extension (`ms-vscode.cpptools`). It provides `cppvsdbg`, which is what attaches to the
  game, and it reads the include paths this extension writes.
* MSVC build tools. A mod is a 32-bit DLL loaded into `game.exe`, so it is built with `cl.exe` and
  nothing else will do. Visual Studio or the standalone Build Tools both work — MSBuild is found
  through `vswhere`.
* AzForge itself: the folder holding `sdk\` and `plugins\`, with `Release\AmodPack.exe` built.

## Commands

| Command | What it does |
| --- | --- |
| **AzForge: New Mod...** | Asks what the mod is called and where the game is, then writes the project, `Mod.cpp`, `resmod\` and the IntelliSense paths, and opens it |
| **AzForge: Build** | MSBuild → `mod.json` → `.amod` → installed into the game's `mods` folder |
| **AzForge: Rebuild** | The same, from scratch |
| **AzForge: Pack Release** | A Release build, and offers to show the `.amod` |
| **AzForge: Run the Game** | Builds, then starts `game.exe` under the debugger |
| **AzForge: Mod Settings** | The fields that become `mod.json`, edited in place in the `.vcxproj` |
| **AzForge: Refresh IntelliSense Paths** | Rewrites `.vscode/c_cpp_properties.json` from the mod's SDK folder |
| **AzForge: Open forge.log** | The loader's log, from the game folder |

Build is also a task: **Run Task → azforge**, so it can be a `preLaunchTask` or bound to
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd> like any other build.

## Where things are kept

Everything about the mod lives in the `.vcxproj`, because `mod.json` is written from it at every
build — a second place to keep the same facts is a second place for them to be wrong. **Mod
Settings** edits those properties in place rather than round-tripping the XML, so a project also
opened in Visual Studio is not reformatted underneath it.

The extension's own settings are only the defaults a *new* mod starts from, plus where MSBuild is:

* `azforge.azforgeRoot` — the folder holding `sdk\` and `plugins\`
* `azforge.gameDir` — where `game.exe` lives
* `azforge.msbuildPath` — left empty, `vswhere` is asked, then PATH
* `azforge.configuration` — which configuration Build and Run use

`Requires` is worth reading about: ticked subsystems are used as written, and ticking nothing lets
the build work the list out from what the sources actually use, on every build. The list of
subsystems that exist is read from `sdk\subsystems.txt`, which AzForge's own build writes.

## Debugging

**Run the Game** builds first, on purpose: the loader takes the DLL out of the `.amod` and copies it
into `mods\.cache`, so a stale build would be loaded without a word about it. Breakpoints bind when
the game loads the mod, which happens at the end of the loading screen.

There is no `launch.json`. The game folder is already in the `.vcxproj` and is read from there.

## Building the extension

```
npm install
npm run compile        # or: npm run watch
```

`compile` first copies `..\templates` into `templates\`. The mod template has one copy, shared with
the Visual Studio side, and neither IDE keeps its own — see `plugins\templates\README.md`. The copy
here is build output and is not committed.

<kbd>F5</kbd> opens an Extension Development Host with it loaded. To install it for real:

```
npx @vscode/vsce package
code --install-extension azforge-mod-tools-1.0.0.vsix
```
