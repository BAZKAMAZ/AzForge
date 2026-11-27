"""Everything the studio silently drops when a package is written wrong.

ModCatalog refuses an entry outside the package's namespaces and a translation the package has
no entry for. Both show up as a warning in the panel and an empty subsystem in the tree, which is
easy to miss, so the same rules are checked here:

  - every catalog id belongs to one of the namespaces the manifest declares;
  - no two packages claim the same namespace or id;
  - a package translates only keys its own entries name, and never one the base table holds;
  - every key an entry names has a translation, in the package or in the base table;
  - the two languages of a file hold the same keys in the same order.

    python tools/checks/packagecheck.py
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACKAGES = os.path.join(ROOT, 'packages')
CATALOGS = ('subsystems.json', 'conditions.json', 'actions.json', 'events.json',
            'expressions.json')

# What AzForge installs and answers for, from ForgeBoot. A mod that asks in its manifest for
# anything else is refused before it is even mapped, so a package named otherwise has to say
# it is not one of these - the way the "system" package does, holding maths and loops
LOADER = {
    'azm', 'block_expression', 'bonuses', 'box_menu', 'camera', 'config', 'console', 'ctrls',
    'demo', 'files', 'frame', 'hud', 'input', 'interop', 'inventory', 'launcher', 'level_editor',
    'mmd', 'mod_screen', 'monsters', 'objects_menu', 'particles', 'player', 'render', 'sav',
    'scores', 'shader', 'sound', 'sprite_font', 'stage', 'textures', 'world',
}


def read(package, name):
    path = os.path.join(PACKAGES, package, 'catalog', name)
    return json.load(open(path, encoding='utf-8')) if os.path.exists(path) else []


def keys_of(path):
    if not os.path.exists(path):
        return []
    return [l.split('=', 1)[0].strip() for l in open(path, encoding='utf-8')
            if '=' in l and not l.lstrip().startswith('#')]


def manifest(package):
    path = os.path.join(PACKAGES, package, 'package.json')
    return json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}


def owns(spaces, entry_id):
    return any(entry_id == space or entry_id.startswith(space + '.') for space in spaces)


def wanted(package):
    """Translation keys the package's own entries name, exactly as ModCatalog collects them."""
    keys = set()
    for name in CATALOGS:
        line = name in ('conditions.json', 'actions.json', 'events.json')
        for item in read(package, name):
            label = item.get('label', '')
            if label:
                keys.add(label)
                if line:
                    keys.add(label + '.line')
            for field in ('section', 'hint'):
                if item.get(field):
                    keys.add(item[field])
            for param in item.get('params', []) + item.get('filters', []):
                if param.get('label'):
                    keys.add(param['label'])
                for lw in param.get('labelWhen', []) or []:
                    if lw.get('label'):
                        keys.add(lw['label'])
                for option in param.get('options', []) or []:
                    if option.get('label'):
                        keys.add(option['label'])
    return keys


def main():
    packages = sorted(p for p in os.listdir(PACKAGES) if os.path.isdir(os.path.join(PACKAGES, p)))
    base = set(keys_of(os.path.join(ROOT, 'assets', 'lang', 'en.txt')))
    complaints = []
    taken = {}

    for package in packages:
        man = manifest(package)
        spaces = man.get('namespaces') or [man.get('id', package)]

        if man.get('loader', True) and package not in LOADER:
            complaints.append('%s: the loader installs no subsystem of that name, so a mod asking '
                              'for it is refused; say "loader": false when it is the studio own'
                              % package)

        for space in spaces:
            if space in taken:
                complaints.append('%s: namespace "%s" is already %s' % (package, space, taken[space]))
            taken[space] = package

        seen = {}
        for name in CATALOGS:
            for item in read(package, name):
                entry = item.get('id', '')
                if not entry:
                    complaints.append('%s/%s: an entry without an id' % (package, name))
                    continue
                if not owns(spaces, entry):
                    complaints.append('%s: "%s" is outside %s, the studio will drop it'
                                      % (package, entry, spaces))
                if entry in seen and seen[entry] == name:
                    complaints.append('%s/%s: "%s" twice' % (package, name, entry))
                seen[entry] = name

        may = wanted(package)
        english = os.path.join(PACKAGES, package, 'lang', 'en.txt')
        russian = os.path.join(PACKAGES, package, 'lang', 'ru.txt')
        own = keys_of(english)

        if own != keys_of(russian):
            complaints.append('%s: en.txt and ru.txt do not hold the same keys' % package)

        for key in own:
            if key not in may:
                complaints.append('%s: translates "%s", which none of its entries names' % (package, key))
            if key in base:
                complaints.append('%s: "%s" is the base table\'s to say' % (package, key))

        for key in sorted(may):
            if key.endswith('.line'):
                continue   # the phrasing of a sheet line is optional; the label stands in for it
            if key.startswith(('mod.', 'common.')) and key not in own and key not in base:
                complaints.append('%s: "%s" has no translation anywhere' % (package, key))

    english = keys_of(os.path.join(ROOT, 'assets', 'lang', 'en.txt'))
    if english != keys_of(os.path.join(ROOT, 'assets', 'lang', 'ru.txt')):
        complaints.append('assets/lang/: en.txt and ru.txt do not hold the same keys')

    for line in complaints:
        print('  ', line)
    print('пакетов: %d, замечаний: %d' % (len(packages), len(complaints)))
    return 1 if complaints else 0


if __name__ == '__main__':
    sys.exit(main())
