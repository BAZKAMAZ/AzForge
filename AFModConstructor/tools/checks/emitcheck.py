"""Compiles every catalog entry of a package once, with its preset values in place.

The studio only emits what a sheet asks for, so a freshly written entry can sit in the catalog
for weeks before anyone finds out its C does not build. This puts all of them in one file and
hands it to the same 32-bit compiler the studio ships.

    python tools/checks/emitcheck.py render          one package
    python tools/checks/emitcheck.py                 every package
"""
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runtime import INC, build as build_runtime  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GCC = os.path.join(ROOT, 'compiler', 'bin', 'gcc.exe')
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')
OUT = os.path.join(ROOT, 'build', 'checks')

# What a generated mod starts with now: the header of the helper library and nothing else.
# The helpers themselves are compiled once into _as-cm10.a, which runtime.py builds.
PREAMBLE = '''/* Written by tools/checks/emitcheck.py: every catalog entry with its presets, so the C is compiled */
#include "_as-cm10.h"

static MOD_UNUSED const AmodFrame* frame;
static MOD_UNUSED AmodToken check_token;
static MOD_UNUSED float mylist[16];
'''

# The prologues no longer go into the file: they are the library. What was here is gone with them
BUILTINS = ''


def read(package, name):
    path = os.path.join(ROOT, 'packages', package, 'catalog', name)
    if not os.path.exists(path):
        return []
    return json.load(open(path, encoding='utf-8'))


def fill(text, params, extra=None, variables=None):
    values = {}
    for param in params:
        value = param.get('preset', '')
        # A list of the sheet is named the same way a variable is, and stands in for one here
        if param.get('label') in ('mod.p.listName', 'mod.p.matrixList', 'mod.p.vertexList'):
            value = value or 'sheet_list'
            if variables is not None:
                variables.add(value)
        if param.get('label') == 'mod.p.varName':
            # The sheet's own variable, which the generator declares by the kind the author
            # picked. An entry that copies or compares text needs the text one
            text_variable = 'mod_set_text(' in text or 'strcmp(' in text
            value = value or ('sheet_text' if text_variable else 'sheet_var')
            if variables is not None:
                variables.add(value)
        if value == '' and param.get('kind') == 'number':
            value = '0'
        # A value a person opened with "=" is code, in a number field and in a text one alike
        code = len(value) > 1 and value[0] == '=' and value[1] != '='
        values['%' + param['key'] + '%'] = (value[1:] if code else value, code)
    values.update({'%' + key + '%': (value, False) for key, value in (extra or {}).items()})

    def literal(body):
        """One "..." of the template with its marks filled in, the way ModCodegen::Literal does.

        A literal that is nothing but code stops being a literal: "%text%" holding a mod_text
        call comes out as that call, not as its text.
        """
        for mark, (value, code) in values.items():
            if body == mark:
                return value if code else '"' + escape(value) + '"'
        out = ''
        at = 0
        while at < len(body):
            if body[at] == '\\':
                out += body[at:at + 2]
                at += 2
                continue
            for mark, (value, _) in values.items():
                if body.startswith(mark, at):
                    out += escape(value)
                    at += len(mark)
                    break
            else:
                out += body[at]
                at += 1
        return '"' + out + '"'

    out = []
    at = 0
    while at < len(text):
        if text[at] == '"':
            end = at + 1
            while end < len(text) and text[end] != '"':
                end += 2 if text[end] == '\\' else 1
            out.append(literal(text[at + 1:end]))
            at = end + 1
            continue
        # Outside a literal the value stands where code stands and is never escaped
        for mark, (value, _) in values.items():
            if text.startswith(mark, at):
                out.append(value)
                at += len(mark)
                break
        else:
            out.append(text[at])
            at += 1
    return ''.join(out)


def escape(value):
    return value.replace('\\', '\\\\').replace('"', '\\"')


def body(package):
    """The C of one package: its prologue, then every entry that emits something."""
    out = []

    # An action may only make sense inside an event: "set the item colour" writes the rgba the
    # style callback hands over. The handler signatures say what those names are.
    context = {}
    for item in read(package, 'events.json'):
        inside = re.search(r'%H%\s*\((.*)\)', item.get('handler', ''))
        if not inside:
            continue
        for part in inside.group(1).split(','):
            part = part.strip()
            name = re.sub(r'\[.*\]$', '', part).split()[-1].lstrip('*')
            if not name or name in ('user', 'frame') or name in context:
                continue
            context[name] = part + (' = {0}' if part.endswith(']') else ' = 0') + ';'

    lines = ['    ' + declaration for declaration in context.values()]
    lines += ['    (void)%s;' % name for name in context]
    states = []
    variables = set()
    for at, item in enumerate(read(package, 'conditions.json')):
        if item.get('state'):
            states.append('static %s state_%d = %s;' % (item['state'], at, item.get('stateInit', '0')))
        text = item.get('emit', '')
        if not text or item.get('loop'):
            continue
        # A condition that asks for %state% without declaring one is the "only once" wrapper:
        # the generator folds it around the other conditions instead of emitting it
        if '%state%' in text and not item.get('state'):
            continue
        text = fill(text, item.get('params', []), {'state': 'state_%d' % at}, variables)
        lines.append('    if (%s) { (void)0; }   /* condition %s */' % (text, item['id']))

    for item in read(package, 'actions.json'):
        text = item.get('emit', '')
        if not text:
            continue
        # A "stop this" action reads the token of the rule it sits in; here one stands in for all
        text = fill(text, item.get('params', []), {'token': 'check_token'}, variables)
        lines.append('    /* action %s */' % item['id'])
        # do/while(0) so that "leave the loop" and "next turn" are legal here as well
        lines.append('    do {')
        lines.append('\n'.join('        ' + l for l in text.split('\n')))
        lines.append('    } while (0);')

    kinds = {'int': 'int', 'float': 'float', 'text': 'const char*'}
    for at, item in enumerate(read(package, 'expressions.json')):
        text = item.get('text', '')
        if not text:
            continue
        lines.append('    { %s value_%d = (%s); (void)value_%d; }   /* expression %s */'
                     % (kinds.get(item.get('kind', 'float'), 'float'), at,
                        text, at, item['id']))

    out += states
    for name in sorted(variables):
        if name.endswith('_text'):
            out.append('static MOD_UNUSED char %s[256];' % name)
        elif name.endswith('_list'):
            out.append('static MOD_UNUSED float %s[64];' % name)
        else:
            out.append('static MOD_UNUSED float %s;' % name)
    out.append('static MOD_UNUSED void check_%s(void) {' % re.sub(r'\W', '_', package))
    out += lines
    out.append('}')
    return '\n'.join(out)


def check(packages):
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    for package in packages:
        source = os.path.join(OUT, package + '.c')
        with open(source, 'w', encoding='utf-8', newline='\n') as f:
            f.write(PREAMBLE + '\n' + BUILTINS + '\n' + body(package) + '\n')

        run = subprocess.run(
            [GCC, '-c', '-O2', '-std=c99', '-Wall', '-Werror=int-conversion',
                          '-Werror=incompatible-pointer-types',
                          '-Werror=implicit-function-declaration', '-B', os.path.dirname(GCC),
             '-I', SDK, '-I', INC, '-x', 'c', source, '-o', os.path.join(OUT, package + '.o')],
            capture_output=True, text=True)
        noise = [l for l in run.stderr.split('\n') if l.strip()]
        if run.returncode != 0 or noise:
            bad += 1
            print('%-18s ОШИБКИ' % package)
            for line in noise[:12]:
                print('   ', line)
        else:
            print('%-18s чисто' % package)
    return bad


if __name__ == '__main__':
    names = sys.argv[1:] or sorted(
        p for p in os.listdir(os.path.join(ROOT, 'packages'))
        if os.path.isdir(os.path.join(ROOT, 'packages', p)))
    failed = check(names)
    print('пакетов с ошибками:', failed)
    sys.exit(1 if failed else 0)
