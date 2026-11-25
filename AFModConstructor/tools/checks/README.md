# Checks

What the studio cannot tell you until something has already gone wrong. Every one of them is
plain Python and builds with the 32-bit compiler the studio ships (`cmake-build-debug/compiler`),
writing its scratch into `cmake-build-debug/checks`.

Run them from the repository root.

| | what it answers |
|---|---|
| `python tools/checks/runtime.py` | Does the helper library build at all - every prologue of the catalog compiled together, which is where a name two packages both took shows up? |
| `python tools/checks/packagecheck.py` | Does the studio actually take everything a package brings? An id outside the package's namespaces, a namespace two packages claim, a translation no entry names, a key the base table already says, two languages that hold different keys — each one makes the studio drop something silently. |
| `python tools/checks/emitcheck.py [package]` | Does the C of every catalog entry compile? The studio only emits what a sheet asks for, so a freshly written entry can sit broken for weeks. |
| `python tools/checks/tokencheck.py` | Does a subscription that keeps its token compile — the token, the handler and the cancel inside it? |
| `python tools/checks/substcheck.py` | Do values a person types survive substitution: an expression with strings of its own, a quote inside a text, text landing in a number field? |
| `python tools/checks/scenariocheck.py` | Do entries still fit together when several are used at once: pixels painted in a loop, turned into a picture, given out under a path, read back as a font? |
| `python tools/checks/sheetcheck.py <mod>` | Does a whole sheet of a real project hold up — every id and argument against the catalog, `{variables}` expanded the way the generator expands them — and does the file the studio itself wrote last build still compile? |
| `python tools/checks/apicover.py [subsystem]` | How much of the SDK the catalog actually calls, and which calls nobody reaches yet. |

`runtime.py` builds `_as-cm10` the way the studio builds it - every helper of the catalog into
`compiler/lib/_as-cm10.a`, its header into `compiler/include` - and the others compile against
that, so a check sees the same shape a mod does. Run it on its own to see what the library
holds. `emitcheck.py` also holds the substitution rules the others import.
