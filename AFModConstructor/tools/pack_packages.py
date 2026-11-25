"""Packs packages/<id>/ folders into %USERPROFILE%/.bazkamaz/packages/<id>.zip.

Usage:
    python tools/pack_packages.py              pack all packages
    python tools/pack_packages.py console sav  pack selected packages
"""
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKAGES = os.path.join(ROOT, 'packages')
HOME = os.environ.get('USERPROFILE') or os.environ.get('HOME') or os.path.expanduser('~')
DEST = os.path.join(HOME, '.bazkamaz', 'packages')


def pack_one(name):
    src = os.path.join(PACKAGES, name)
    if not os.path.isdir(src):
        raise SystemExit('no package folder: %s' % src)
    os.makedirs(DEST, exist_ok=True)
    out = os.path.join(DEST, name + '.zip')
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
        for base, dirs, files in os.walk(src):
            dirs.sort()
            for fname in sorted(files):
                full = os.path.join(base, fname)
                rel = os.path.relpath(full, src).replace('\\', '/')
                zf.write(full, rel)
    return out


def main():
    names = sys.argv[1:] or sorted(
        d for d in os.listdir(PACKAGES)
        if os.path.isdir(os.path.join(PACKAGES, d))
    )
    for name in names:
        pack_one(name)
    print('packed %d package(s) -> %s' % (len(names), DEST))
    return 0


if __name__ == '__main__':
    sys.exit(main())
