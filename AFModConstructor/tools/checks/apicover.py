"""What the SDK offers against what the catalog actually calls.

Reads the AmodApi table to learn which struct belongs to which subsystem, collects every
function pointer of those structs, then looks through the packages for "->Name(" calls,
prologues included. Prints coverage per subsystem and names what nobody calls.

    python tools/checks/apicover.py            every subsystem
    python tools/checks/apicover.py sound      one of them
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')

# Attach and IsAvailable are C++ helpers outside the struct; Revive stays in ABI only
PLUMBING = {'Revive'}


def struct_members(text, name):
    """Function pointers of `typedef struct <name> { ... } <name>;`"""
    block = re.search(r'typedef\s+struct\s+' + name + r'\b(.*?)\}\s*' + name + r'\s*;', text, re.S)
    if not block:
        return []
    return [m for m in re.findall(r'\(\s*__cdecl\s*\*\s*(\w+)\s*\)', block.group(1))
            if m not in PLUMBING]


def api_table():
    """member of AmodApi -> (struct name, header file)"""
    text = open(os.path.join(SDK, 'ForgeApi.h'), encoding='utf-8', errors='ignore').read()
    block = re.search(r'typedef\s+struct\s+AmodApi\b(.*?)\}\s*AmodApi\s*;', text, re.S)
    out = {}
    for kind, member in re.findall(r'const\s+(Amod\w+Api)\s*\*\s*(\w+)\s*;', block.group(1)):
        for name in os.listdir(SDK):
            if not name.endswith('.h'):
                continue
            body = open(os.path.join(SDK, name), encoding='utf-8', errors='ignore').read()
            if re.search(r'typedef\s+struct\s+' + kind + r'\b', body):
                out[member] = (kind, name)
                break
    return out


def catalog_text(package):
    """Every piece of C the package carries: prologues, emits, expressions, subscriptions."""
    pieces = []
    folder = os.path.join(ROOT, 'packages', package, 'catalog')
    if not os.path.isdir(folder):
        return ''
    for name in sorted(os.listdir(folder)):
        if not name.endswith('.json'):
            continue
        for item in json.load(open(os.path.join(folder, name), encoding='utf-8')):
            for field in ('prologue', 'emit', 'text', 'subscribe', 'handler', 'frameTest', 'call'):
                if item.get(field):
                    pieces.append(item[field])
    return '\n'.join(pieces)


def main():
    table = api_table()
    wanted = sys.argv[1:]

    # Which packages speak to which API member
    speaks = {}
    for package in sorted(os.listdir(os.path.join(ROOT, 'packages'))):
        if not os.path.isdir(os.path.join(ROOT, 'packages', package)):
            continue
        text = catalog_text(package)
        for member in set(re.findall(r'API->(\w+)->', text)):
            speaks.setdefault(member, set()).add(package)

    rows = []
    for member, (kind, header) in sorted(table.items()):
        body = open(os.path.join(SDK, header), encoding='utf-8', errors='ignore').read()
        functions = struct_members(body, kind)
        if not functions:
            continue
        packages = sorted(speaks.get(member, []))
        if wanted and member not in wanted and not (set(wanted) & set(packages)):
            continue
        text = '\n'.join(catalog_text(p) for p in packages)
        called = {f for f in functions if ('->' + f + '(') in text}
        rows.append((member, header, packages, functions, called))

    rows.sort(key=lambda r: len(r[4]) / len(r[3]))
    for member, header, packages, functions, called in rows:
        missing = [f for f in functions if f not in called]
        share = '%d/%d' % (len(called), len(functions))
        print('%-16s %-22s %-7s %s' % (member, ', '.join(packages) or '- нет пакета -', share, header))
        if missing:
            print('      нет в каталоге:', ', '.join(missing))
    covered = sum(len(r[4]) for r in rows)
    total = sum(len(r[3]) for r in rows)
    print('\nвсего вызовов покрыто: %d из %d' % (covered, total))


if __name__ == '__main__':
    main()
