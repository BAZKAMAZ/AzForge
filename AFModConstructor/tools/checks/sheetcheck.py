"""A sheet of a real project, read the way the studio reads it, and compiled.

Not the generator itself - that lives in the studio and builds handlers and subscriptions too -
but everything a sheet can get wrong on its own: an entry that is not in the catalog, a
parameter nobody asked for, a value that is not the C it claims to be. The bodies of the rules
are written out in order, with loops and conditions around them, and handed to the compiler.

    python tools/checks/sheetcheck.py pixelart
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runtime import INC, build as build_runtime  # noqa: E402
from emitcheck import BUILTINS, PREAMBLE, fill, read  # noqa: E402

OUT = os.path.join(ROOT, 'build', 'checks')
GCC = os.path.join(ROOT, 'compiler', 'bin', 'gcc.exe')
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')
# A mod lives either in the examples folder or in build/projects
PLACES = (os.path.join(ROOT, 'examples'), os.path.join(ROOT, 'build', 'projects'))


def project(name):
    for place in PLACES:
        if os.path.isdir(os.path.join(place, name, 'logic')):
            return os.path.join(place, name)
    return os.path.join(PLACES[0], name)

KINDS = ('conditions', 'actions', 'events', 'expressions')


def handler_names(head):
    """The words a handler is handed, so an unused one does not become a warning."""
    inside = re.search(r'\((.*)\)', head)
    if not inside:
        return []
    out = []
    for part in inside.group(1).split(','):
        part = part.strip()
        if part and part != 'void':
            out.append(re.sub(r'\[.*\]$', '', part).split()[-1].lstrip('*'))
    return out


def catalog():
    """Every entry of every package, by id, with the package it came from."""
    out = {}
    for package in sorted(os.listdir(os.path.join(ROOT, 'packages'))):
        if not os.path.isdir(os.path.join(ROOT, 'packages', package)):
            continue
        for kind in KINDS:
            for item in read(package, kind + '.json'):
                out[(kind, item['id'])] = (package, item)
    return out


def prologue_of(packages):
    """Nothing: the helpers of a package live in the library the mod links, not in its file."""
    return []


def interpolate(text, kinds, complaints, where):
    """A text with braces in it, the way ModCodegen writes it: "score {n}" is a mod_text call.

    {name} is a variable of the sheet, {#code} a whole number, {~code} a fraction and {$code}
    a text. Braces around a block of a game script hold another brace and are left alone.
    """
    out, args, at, wrote = '', [], 0, False
    while at < len(text):
        open_at = text.find('{', at)
        close_at = text.find('}', open_at + 1) if open_at >= 0 else -1
        if open_at < 0 or close_at < 0:
            out += text[at:]
            break
        inner = text.find('{', open_at + 1)
        if 0 <= inner < close_at:          # a script block, not a value
            out += text[at:inner]
            at = inner
            continue

        name = text[open_at + 1:close_at]
        out += text[at:open_at].replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%')
        if name[:1] in ('#', '~', '$'):
            spec = {'#': '%d', '~': '%g', '$': '%s'}[name[0]]
            code = {'#': '(int)(%s)', '~': '(double)(%s)', '$': '%s'}[name[0]] % name[1:]
        elif name in kinds:
            spec = {'float': '%g', 'text': '%s'}.get(kinds[name], '%d')
            code = name
        elif name.replace('_', 'a').isalnum() and not name[:1].isdigit():
            complaints.append('%s: "%s" is not a variable of this sheet' % (where, name))
            spec, code = '%s', '""'
        else:
            out += '{' + name.replace('%', '%%') + '}'   # a script block, not a value
            at = close_at + 1
            continue
        out += spec
        args.append(code)
        wrote = True
        at = close_at + 1

    if not wrote:
        return None
    return '=mod_text("' + out + '"' + ''.join(', ' + a for a in args) + ')'


def typed(item, args, complaints, where, kinds=None):
    """Parameters filled in as the sheet filled them; an argument nobody asked for is a mistake."""
    keys = {p['key'] for p in item.get('params', [])}
    for key in args:
        if key not in keys:
            complaints.append('%s: "%s" takes no argument "%s"' % (where, item['id'], key))
    out = []
    for param in item.get('params', []):
        copy = dict(param)
        if param['key'] in args:
            copy['preset'] = args[param['key']]
        value = copy.get('preset', '')
        if (kinds is not None and copy.get('kind') != 'number' and '{' in value
                and not value.startswith('=')):
            written = interpolate(value, kinds, complaints, where)
            if written:
                copy['preset'] = written
        out.append(copy)
    return out


def declare(variables):
    lines = []
    for var in variables:
        kind = var.get('type', 'int')
        name = var['name']
        value = var.get('value', '0')
        if kind == 'list':
            length = int(value) if value.isdigit() and int(value) > 0 else 16
            lines.append('static MOD_UNUSED float %s[%d] = {0};' % (name, length))
        elif kind == 'text':
            lines.append('static MOD_UNUSED char %s[256] = "%s";' % (name, value))
        elif kind == 'bool':
            lines.append('static MOD_UNUSED int %s = %s;' % (name, 1 if value in ('1', 'true') else 0))
        elif kind == 'float':
            lines.append('static MOD_UNUSED float %s = %sf;' % (name, value if '.' in value else value + '.0'))
        else:
            lines.append('static MOD_UNUSED int %s = %s;' % (name, value or '0'))
    return lines


def body(rule, book, used, complaints, where, depth, states, kinds):
    """One rule as C: its loops, then its test, then its actions and the rules inside it."""
    lines = []
    pad = '    ' * depth
    loops, tests = [], []
    once = False

    for at, condition in enumerate(rule.get('conditions', [])):
        if not condition.get('enabled', True):
            continue
        found = book.get(('conditions', condition['id']))
        if not found:
            complaints.append('%s: no condition "%s" in the catalog' % (where, condition['id']))
            continue
        package, item = found
        used.add(package)
        params = typed(item, condition.get('args', {}), complaints, where, kinds)

        if item.get('loop'):
            loops.append(fill(item['loop'], params, None, None))
            continue

        # "only once" is not an ordinary condition: it wraps the rest of them, because it has
        # to remember whether they held on the previous frame
        if '%state%' in item.get('emit', '') and not item.get('state'):
            once = True
            continue

        extra = {}
        if item.get('state'):
            name = 'state_%d_%d' % (depth, len(states))
            states.append('static %s %s = %s;' % (item['state'], name, item.get('stateInit', '0')))
            extra['state'] = name
        test = '(' + fill(item.get('emit', '1'), params, extra, None) + ')'
        tests.append('!' + test if condition.get('inverted') else test)

    if once:
        name = 'once_%d_%d' % (depth, len(states))
        states.append('static int %s = 0;' % name)
        tests = ['trigger_once(&%s, %s)' % (name, ' && '.join(tests) if tests else '1')]

    if tests:
        lines.append(pad + 'if (' + ' && '.join(tests) + ') {')
        pad += '    '
        depth += 1
    for loop in loops:
        lines.append(pad + loop + ' {')
        pad += '    '
        depth += 1

    for act in rule.get('actions', []):
        if not act.get('enabled', True):
            continue
        found = book.get(('actions', act['id']))
        if not found:
            complaints.append('%s: no action "%s" in the catalog' % (where, act['id']))
            continue
        package, item = found
        used.add(package)
        params = typed(item, act.get('args', {}), complaints, where, kinds)
        text = fill(item.get('emit', ''), params, {'token': 'check_token'}, None)
        lines.append('\n'.join(pad + l for l in text.split('\n')))

    for kid in rule.get('children', []):
        lines += body(kid, book, used, complaints, where, depth, states, kinds)

    for _ in loops:
        depth -= 1
        lines.append('    ' * depth + '}')
    if tests:
        depth -= 1
        lines.append('    ' * depth + '}')
    return lines


def generated(name):
    """The file the studio itself wrote on its last build, compiled as the mod build compiles it.

    The rest of this script is a model of the generator; this is the generator's own answer, so
    a place where the two part ways shows up here and nowhere else.
    """
    source = os.path.join(project(name), 'src', 'mod_generated.c')
    if not os.path.exists(source):
        print('студия ещё не собирала этот мод, её вывод не проверен')
        return 0

    run = subprocess.run([GCC, '-c', '-O2', '-std=c99', '-Wall', '-Werror=int-conversion',
                          '-Werror=incompatible-pointer-types',
                          '-Werror=implicit-function-declaration', '-B', os.path.dirname(GCC),
                          '-I', SDK, '-I', INC, '-x', 'c', source,
                          '-o', os.path.join(OUT, 'generated.o')],
                         capture_output=True, text=True)
    noise = [l for l in run.stderr.split('\n') if l.strip()]
    print('вывод студии (%s):' % os.path.basename(source), 'чисто' if not noise else 'ОШИБКИ')
    for line in noise[:10]:
        print('   ', line)
    return 0 if run.returncode == 0 and not noise else 1


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'pixelart'
    folder = os.path.join(project(name), 'logic')
    if not os.path.isdir(folder):
        print('нет листов в', folder)
        return 1

    book = catalog()
    complaints, used, lines, states, variables, handlers = [], set(), [], [], [], []
    kinds = {}

    for file in sorted(os.listdir(folder)):
        if not file.endswith('.json'):
            continue
        sheet = json.load(open(os.path.join(folder, file), encoding='utf-8'))
        variables += sheet.get('vars', [])
        for var in sheet.get('vars', []):
            kinds[var['name']] = var.get('type', 'int')
        for at, rule in enumerate(sheet.get('rules', [])):
            where = '%s / %d' % (sheet.get('name', file), at + 1)
            event = rule.get('event', '')
            if event and ('events', event) not in book:
                complaints.append('%s: no event "%s" in the catalog' % (where, event))
                continue

            inside = body(rule, book, used, complaints, where, 1, states, kinds)
            if not event:
                lines += inside
                continue

            # A rule with an event becomes a handler of its own, the way the generator writes
            # it: what the event hands over is only in scope there
            package, item = book[('events', event)]
            used.add(package)
            name_of = 'rule_%d_%s' % (at, event.replace('.', '_'))
            head = item.get('handler', '').replace('%H%', name_of)
            if not head:
                lines += inside      # a frame rule, which has no handler of its own
                continue
            # Nothing subscribes them here, so they are marked as the generator marks helpers
            handlers.append(head.replace('static ', 'static MOD_UNUSED ', 1) + ' {')
            handlers += ['    (void)%s;' % word for word in handler_names(head)]
            handlers += inside
            handlers.append('    ' + item.get('retDefault', 'return;'))
            handlers.append('}')

    source = os.path.join(OUT, 'sheet_%s.c' % name)
    os.makedirs(OUT, exist_ok=True)
    with open(source, 'w', encoding='utf-8', newline='\n') as f:
        f.write(PREAMBLE + '\nstatic MOD_UNUSED AmodToken check_token;\n' + BUILTINS + '\n' +
                '\n'.join(prologue_of(used)) + '\n' + '\n'.join(declare(variables)) + '\n' +
                '\n'.join(states) + '\n' + '\n'.join(handlers) +
                '\nstatic MOD_UNUSED void sheet(void) {\n' + '\n'.join(lines) + '\n}\n')

    run = subprocess.run([GCC, '-c', '-O2', '-std=c99', '-Wall', '-Werror=int-conversion',
                          '-Werror=incompatible-pointer-types',
                          '-Werror=implicit-function-declaration', '-B', os.path.dirname(GCC),
                          '-I', SDK, '-I', INC, '-x', 'c', source, '-o', os.path.join(OUT, 'sheet.o')],
                         capture_output=True, text=True)
    noise = [l for l in run.stderr.split('\n') if l.strip()]

    print('лист "%s": подсистем %d, строк кода %d, переменных %d'
          % (name, len(used), len(lines), len(variables)))
    for line in complaints + noise[:15]:
        print('   ', line)
    ok = not complaints and run.returncode == 0 and not noise
    print('чисто' if ok else 'ОШИБКИ')
    return 0 if ok and generated(name) == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
