"""Compiles the shape the generator now writes for a subscription that keeps its token.

For every event whose subscribe template asks for %token% this builds a rule the way
ModCodegen does - a token of its own, a handler, the subscription inside AmodInit - and, where
the package has cancel actions, puts them in the handler so the token is read back as well.
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


def main():
    packages = sorted(p for p in os.listdir(os.path.join(ROOT, 'packages'))
                      if os.path.isdir(os.path.join(ROOT, 'packages', p)))

    parts = []
    subscribing = []
    index = 0
    for package in packages:
        cancels = [a for a in read(package, 'actions.json') if a.get('needs') == 'token']
        events = [e for e in read(package, 'events.json') if '%token%' in e.get('subscribe', '')]
        if not events:
            continue

        for event in events:
            name = 'rule_%d' % index
            token = name + '_token'
            index += 1

            parts.append('static AmodToken %s;' % token)
            parts.append(event['handler'].replace('%H%', name) + ' {')
            for action in cancels:
                parts.append('    ' + fill(action['emit'], action.get('params', []),
                                           {'token': token}))
            # The generator closes a handler with the event's own answer; a callback that
            # returns something has to say what
            parts.append('    ' + event.get('retDefault', 'return;'))
            parts.append('}')
            subscribe = fill(event['subscribe'].replace('%H%', name), event.get('filters', []),
                             {'token': token})
            subscribing.append('    ' + subscribe + '   /* %s */' % event['id'])

    parts.append('static MOD_UNUSED void check_init(void) {')
    parts += subscribing
    parts.append('}')

    os.makedirs(OUT, exist_ok=True)
    source = os.path.join(OUT, 'tokens.c')
    with open(source, 'w', encoding='utf-8', newline='\n') as f:
        f.write(PREAMBLE + '\n' + BUILTINS + '\n' + '\n'.join(parts) + '\n')

    run = subprocess.run([GCC, '-c', '-O2', '-std=c99', '-Wall', '-Werror=int-conversion',
                          '-Werror=incompatible-pointer-types',
                          '-Werror=implicit-function-declaration', '-B', os.path.dirname(GCC),
                          '-I', SDK, '-I', INC, '-x', 'c', source, '-o', os.path.join(OUT, 'tokens.o')],
                         capture_output=True, text=True)
    noise = [l for l in run.stderr.split('\n') if l.strip()]
    print('подписок с токеном:', len(subscribing))
    for line in noise[:20]:
        print('   ', line)
    print('чисто' if run.returncode == 0 and not noise else 'ОШИБКИ')
    return 0 if run.returncode == 0 and not noise else 1


if __name__ == '__main__':
    sys.exit(main())
