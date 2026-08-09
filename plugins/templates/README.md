# The mod template

One copy, used by both IDEs. Visual Studio zips it into the VSIX; VS Code copies it into the
extension. Neither keeps its own — two copies of a project template drift, and the one nobody is
looking at is the one that rots.

Both sides fill the same tokens:

| Token | What goes in |
| --- | --- |
| `$safeprojectname$` | The project name, with anything that is not a letter or digit taken out |
| `$guid1$` | A fresh GUID for `ProjectGuid` |
| `$modid$` `$modname$` `$modauthor$` `$moddescription$` | What the mod calls itself |
| `$gamedir$` | The folder holding `game.exe` |
| `$azforgedir$` | The AzForge folder — the one holding `sdk\` and `plugins\` |
| `$propspath$` | The full path to `AzForge.Mod.props` |

`$propspath$` is passed in rather than built from `$azforgedir$` because `AzForge.Mod.props` is
plain MSBuild and is not tied to either IDE. Both sides look for it, so moving it out of
`plugins\vs2026\` breaks nothing here.

Anything added to this folder has to be listed on both sides: in `AzForgeMod.vstemplate` for
Visual Studio, and written by `newMod.ts` for VS Code.
