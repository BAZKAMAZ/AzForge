"""A whole sheet, entry by entry, compiled: the checks above take one entry at a time, this one
puts them together the way a person would and sees whether the result is C.

The scenario is the one the canvas exists for: pixels are painted in a loop, turned into a
picture, given out under a game path, read back by the sprite font as a grid of letters and
finally drawn - without a single file on disk.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runtime import INC, build as build_runtime  # noqa: E402
from emitcheck import BUILTINS, PREAMBLE, fill, read  # noqa: E402

OUT = os.path.join(ROOT, 'build', 'checks')
GCC = os.path.join(ROOT, 'compiler', 'bin', 'gcc.exe')
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')

FONT = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'

# (package, kind, id, values). A condition with a loop wraps what follows it, one step deep.
STEPS = [
    ('textures', 'actions', 'texture.canvas', {'canvas': 'sheet', 'w': '208', 'h': '16'}),

    ('system', 'conditions', 'system.repeat', {'count': '26', 'var': 'letter'}),
    ('system', 'conditions', 'system.repeat', {'count': '16', 'var': 'row'}),
    ('textures', 'actions', 'texture.pixel',
     {'canvas': 'sheet', 'x': 'letter * 8 + row / 4', 'y': 'row',
      'r': '1', 'g': '1', 'b': '1', 'a': '1'}),
    (None, None, 'end', {}),
    (None, None, 'end', {}),

    ('textures', 'actions', 'texture.make', {'canvas': 'sheet', 'name': 'myfont'}),
    ('textures', 'actions', 'texture.override', {'path': 'mod/font.tga', 'name': 'myfont'}),

    ('sprite_font', 'actions', 'font.grid',
     {'font': 'mine', 'file': 'mod/font.tga', 'chars': FONT,
      'cellW': '8', 'cellH': '16', 'offsetX': '0'}),
    ('sprite_font', 'actions', 'font.lineHeight', {'font': 'mine', 'height': '16'}),
    ('sprite_font', 'actions', 'font.draw',
     {'font': 'mine', 'text': 'HELLO', 'x': '320', 'y': '240', 'scale': '1',
      'align': 'AMOD_FONT_CENTRE', 'r': '1', 'g': '1', 'b': '1', 'a': '1'}),

    ('system', 'actions', 'var.setText', {'name': 'caption', 'value': 'width {shown}'}),
]


def entry(package, kind, entry_id):
    for item in read(package, kind + '.json'):
        if item['id'] == entry_id:
            return item
    raise SystemExit('нет записи %s в %s' % (entry_id, package))


def typed(item, values):
    """The entry's parameters as this step fills them in, the rest left at their presets."""
    out = []
    for param in item.get('params', []):
        copy = dict(param)
        if param['key'] in values:
            copy['preset'] = values[param['key']]
        out.append(copy)
    return out


def main():
    prologues = []   # in the library now; the file only includes its header

    lines = []
    depth = 1
    for package, kind, entry_id, values in STEPS:
        pad = '    ' * depth
        if entry_id == 'end':
            depth -= 1
            lines.append('    ' * depth + '}')
            continue

        item = entry(package, kind, entry_id)
        params = typed(item, values)
        if item.get('loop'):
            lines.append(pad + fill(item['loop'], params, None, None) + ' {')
            depth += 1
            continue

        text = fill(item.get('emit', ''), params, None, None)
        lines.append('\n'.join(pad + l for l in text.split('\n')))

    # What the sheet would have declared for itself
    head = ('static MOD_UNUSED char caption[256] = "";\n'
            'static MOD_UNUSED int shown = 0;\n')

    source = os.path.join(OUT, 'scenario.c')
    os.makedirs(OUT, exist_ok=True)
    with open(source, 'w', encoding='utf-8', newline='\n') as f:
        f.write(PREAMBLE + '\n' + BUILTINS + '\n' + '\n'.join(prologues) + '\n' + head +
                '\nstatic MOD_UNUSED void font_without_files(void) {\n' + '\n'.join(lines) + '\n}\n')

    run = subprocess.run([GCC, '-c', '-O2', '-std=c99', '-Wall', '-Werror=int-conversion',
                          '-Werror=incompatible-pointer-types',
                          '-Werror=implicit-function-declaration', '-B', os.path.dirname(GCC),
                          '-I', SDK, '-I', INC, '-x', 'c', source, '-o', os.path.join(OUT, 'scenario.o')],
                         capture_output=True, text=True)
    noise = [l for l in run.stderr.split('\n') if l.strip()]
    print('шагов в листе:', len([s for s in STEPS if s[2] != 'end']))
    for line in noise[:15]:
        print('   ', line)
    print('чисто' if run.returncode == 0 and not noise else 'ОШИБКИ')
    return 0 if run.returncode == 0 and not noise else 1


if __name__ == '__main__':
    sys.exit(main())
