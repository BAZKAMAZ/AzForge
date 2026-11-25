"""Values a person types, run through the rules ModCodegen::Substitute follows, then compiled.

The generator itself is C++ inside the studio; this walks the same three rules on the catalog's
own templates so a regression in them shows up as a broken line of C rather than in the game:

  - a value written with a leading "=" is code and goes in as it stands, quotes and all;
  - a value that lands outside a string literal is never escaped;
  - a number field that opens with a quote is text in the wrong place: it warns and reads 0.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runtime import INC, build as build_runtime  # noqa: E402
from emitcheck import BUILTINS, PREAMBLE, read  # noqa: E402

GCC = os.path.join(ROOT, 'compiler', 'bin', 'gcc.exe')
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')
OUT = os.path.join(ROOT, 'build', 'checks')

# package, action, values as a person typed them, and what the line has to end up saying
CASES = [
    ('objects_menu', 'menu.setPos',
     {'menu': 'IDM_MAIN_MENU', 'item': 'ID_NEW_GAME',
      'x': '=menu_float("IDM_MAIN_MENU", "ID_NEW_GAME", "pos", 0) + dir * 160.0f * mod_delta',
      'y': '=menu_float("IDM_MAIN_MENU", "ID_NEW_GAME", "pos", 1)'},
     ['menu_float("IDM_MAIN_MENU"', 'dir * 160.0f * mod_delta'], []),

    ('block_expression', 'block.storeSetNumber',
     {'path': 'data', 'key': 'count', 'value': '"Solider boy"'},
     ['(float)(0)'], ['Solider boy']),

    ('block_expression', 'block.storeSetText',
     {'path': 'data', 'key': 'count', 'text': 'Solider "boy"'},
     ['Solider \\"boy\\"'], []),

    ('sound', 'sound.playAt',
     {'file': 'sounds/water.wav', 'name': 'water', 'x': '=player_x()', 'y': '0', 'z': '0',
      'loop': '1', 'pitch': '1'},
     ['player_x()'], []),
]


def pieces(kind, text):
    """PiecesOf, for the cases a catalog value actually takes."""
    if len(text) > 1 and text[0] == '=' and text[1] != '=':
        return text[1:], True
    return text, False


def escape(value):
    out = ''
    previous = ''
    for c in value:
        if c in '\\"':
            out += '\\'
        if c == '?' and previous == '?':
            out += '\\'
        if c in '\n\r':
            continue
        out += c
        previous = c
    return out


def substitute(template, params, typed, complaints):
    values = {}
    for param in params:
        text = typed.get(param['key'], param.get('preset', ''))
        if text == '':
            text = '0' if param.get('kind') == 'number' else ''

        code, written = pieces(param.get('kind'), text)
        first = len(text) - len(text.lstrip(' \t'))
        if (param.get('kind') == 'number' and not written
                and text[first:first + 1] == '"'):
            complaints.append('text where a number belongs, %s: %s' % (param['key'], text))
            code = '0'
        values['%' + param['key'] + '%'] = code

    out = ''
    at = 0
    quoted = False
    while at < len(template):
        if template[at] == '\\' and quoted:
            out += template[at:at + 2]
            at += 2
            continue
        if template[at] == '"':
            quoted = not quoted
            out += template[at]
            at += 1
            continue
        for mark, value in values.items():
            if template.startswith(mark, at):
                out += escape(value) if quoted else value
                at += len(mark)
                break
        else:
            out += template[at]
            at += 1
    return out


def main():
    lines = []
    prologues = []   # in the library now; the file only includes its header
    bad = 0
    for package, action, typed, expected, forbidden in CASES:
        item = next((a for a in read(package, 'actions.json') if a['id'] == action), None)
        if item is None:
            print('нет такого действия:', action)
            bad += 1
            continue

        complaints = []
        line = substitute(item['emit'], item.get('params', []), typed, complaints)
        print('%-22s %s' % (action, line))
        for note in complaints:
            print('%-22s замечание: %s' % ('', note))

        for want in expected:
            if want not in line:
                print('%-22s ЖДАЛИ в строке: %s' % ('', want))
                bad += 1
        for never in forbidden:
            if never in line:
                print('%-22s НЕ ДОЛЖНО быть: %s' % ('', never))
                bad += 1
        lines.append('    ' + line)

    # What a sheet would have around it; menu_float comes from the package prologue itself
    helpers = ('static MOD_UNUSED float player_x(void) { return 0.0f; }\n'
               'static MOD_UNUSED float dir = 1.0f;\n')

    source = os.path.join(OUT, 'subst.c')
    os.makedirs(OUT, exist_ok=True)
    with open(source, 'w', encoding='utf-8', newline='\n') as f:
        f.write(PREAMBLE + '\n' + BUILTINS + '\n' + '\n'.join(prologues) + '\n' + helpers +
                '\nstatic MOD_UNUSED void check_substitution(void) {\n' + '\n'.join(lines) + '\n}\n')

    run = subprocess.run([GCC, '-c', '-O2', '-std=c99', '-Wall', '-Werror=int-conversion',
                          '-Werror=incompatible-pointer-types',
                          '-Werror=implicit-function-declaration', '-B', os.path.dirname(GCC),
                          '-I', SDK, '-I', INC, '-x', 'c', source, '-o', os.path.join(OUT, 'subst.o')],
                         capture_output=True, text=True)
    noise = [l for l in run.stderr.split('\n') if l.strip()]
    for line in noise[:12]:
        print('   ', line)
    if run.returncode != 0 or noise:
        bad += 1
    print('чисто' if bad == 0 else 'ОШИБКИ: %d' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
