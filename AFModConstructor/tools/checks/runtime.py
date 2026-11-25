"""Builds _as-cm10 the way ModRuntime builds it, so the checks compile what the studio compiles.

Every helper of the catalog goes into one library beside the studio, and a mod only includes
the header and links it. The transformation is the same one the studio does: a file-scope
function of a prologue loses its "static" and its head becomes a line of the header.
"""
import json
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACKAGES = os.path.join(ROOT, 'packages')
OUT = os.path.join(ROOT, 'build', 'checks')
# Where the constructor keeps them: inside the toolchain it ships
LIB = os.path.join(ROOT, 'compiler', 'lib')
INC = os.path.join(ROOT, 'compiler', 'include')
GCC = os.path.join(ROOT, 'compiler', 'bin', 'gcc.exe')
AR = os.path.join(ROOT, 'compiler', 'bin', 'ar.exe')
SDK = os.path.join(os.path.dirname(ROOT), 'sdk', 'include')

BUILTINS = '''/* The table the loader hands over, and the seconds the last frame took. The generated mod
   fills them in: everything below reads them */
const AmodApi* g_api = 0;
float mod_delta = 0.0f;

AmodValue as_value(const char* text) {
    AmodValue v;
    char* end = 0;
    double number;
    memset(&v, 0, sizeof v);
    number = strtod(text, &end);
    if (text[0] != 0 && end != 0 && *end == 0) {
        v.kind = AMOD_VALUE_FLOAT;
        v.count = 1;
        v.numbers[0] = (float)number;
    } else {
        v.kind = AMOD_VALUE_STRING;
        v.text = text;
    }
    return v;
}

int ctrl_index(const char* name) {
    return (API != 0 && API->ctrls != 0) ? API->ctrls->Find(SELF, name, 0) : -1;
}

const char* mod_text(const char* format, ...) {
    static char buffers[4][512];
    static int turn = 0;
    char* const out = buffers[turn];
    va_list args;
    turn = (turn + 1) % 4;
    va_start(args, format);
    vsnprintf(out, sizeof buffers[0], format, args);
    va_end(args);
    return out;
}

void mod_set_text(char* slot, int size, const char* text) {
    if (slot == 0 || size <= 0) return;
    if (text == 0) text = "";
    strncpy(slot, text, (size_t)size - 1);
    slot[size - 1] = 0;
}
'''

DECLARATIONS = '''extern const AmodApi* g_api;
#define API g_api
#define SELF g_api->self

extern float mod_delta;

AmodValue as_value(const char* text);
int ctrl_index(const char* name);
void mod_set_text(char* slot, int size, const char* text);

#if defined(__GNUC__)
#define MOD_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define MOD_FORMAT
#endif

MOD_FORMAT const char* mod_text(const char* format, ...);
'''

HEAD = '''/* Written by the checks, the way AzangaraStudio writes it */
#ifndef AS_CM10_H
#define AS_CM10_H

#include <ForgeApi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>

#if defined(__GNUC__)
#define MOD_UNUSED __attribute__((unused))
#else
#define MOD_UNUSED
#endif

'''


def function_head(text, at):
    """The head of a file-scope function starting at `at`, or None for a variable or an array."""
    if not text.startswith('static ', at):
        return None
    brace = text.find('{', at)
    semicolon = text.find(';', at)
    if brace < 0 or (0 <= semicolon < brace):
        return None
    end = brace
    while end > at and text[end - 1] in ' \t\r\n':
        end -= 1
    if end == at or text[end - 1] != ')':
        return None
    return text[at:end]


def without_static(head):
    head = head[len('static '):]
    if head.startswith('MOD_UNUSED '):
        head = head[len('MOD_UNUSED '):]
    return head


def pieces():
    """One piece per subsystem: each becomes an object of its own inside the library, so a mod
    drags in only the subsystems it actually calls."""
    out = [('_builtins', BUILTINS)]
    for package in sorted(os.listdir(PACKAGES)):
        path = os.path.join(PACKAGES, package, 'catalog', 'subsystems.json')
        if not os.path.exists(path):
            continue
        for item in json.load(open(path, encoding='utf-8')):
            if item.get('prologue'):
                out.append((item['id'], item['prologue']))
    return out


def split(source):
    code, declarations = [], []
    at = 0
    while at < len(source):
        head = function_head(source, at)
        if head:
            declarations.append(without_static(head) + ';\n')
            code.append(without_static(head))
            at += len(head)
            continue
        end = source.find('\n', at)
        end = len(source) if end < 0 else end + 1
        code.append(source[at:end])
        at = end
    return ''.join(code), ''.join(declarations)


def build():
    """Writes the header and the library; answers where they are, or raises with the compiler's words."""
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(LIB, exist_ok=True)
    os.makedirs(INC, exist_ok=True)
    parts = pieces()

    header = os.path.join(INC, '_as-cm10.h')
    open(header, 'w', encoding='utf-8', newline='\n').write(
        HEAD + DECLARATIONS + '\n/* ---- the subsystems of the catalog ---- */\n' +
        ''.join(split(text)[1] for _, text in parts) + '\n#endif\n')

    objects = []
    for name, text in parts:
        csource = os.path.join(LIB, 'as-cm10', 'as_%s.c' % name)
        obj = os.path.join(LIB, 'as-cm10', 'as_%s.o' % name)
        os.makedirs(os.path.dirname(csource), exist_ok=True)
        open(csource, 'w', encoding='utf-8', newline='\n').write(
            '#include "_as-cm10.h"\n\n' + split(text)[0])
        run = subprocess.run([GCC, '-c', '-O2', '-std=c99', '-Wall', '-B', os.path.dirname(GCC),
                              '-I', SDK, '-I', INC, '-x', 'c', csource, '-o', obj],
                             capture_output=True, text=True)
        noise = [l for l in run.stderr.split('\n') if l.strip()]
        if run.returncode != 0 or noise:
            raise SystemExit('%s не собрался:\n   %s' % (name, '\n   '.join(noise[:12])))
        objects.append(obj)

    library = os.path.join(LIB, '_as-cm10.a')
    if os.path.exists(library):
        os.remove(library)
    run = subprocess.run([AR, 'rcs', library] + objects, capture_output=True, text=True)
    if run.returncode != 0:
        raise SystemExit('архив не собрался: ' + run.stderr.strip())

    return INC, library


if __name__ == '__main__':
    folder, library = build()
    print('заголовок: %s' % folder)
    print('библиотека: %s' % os.path.dirname(library))
    print('_as-cm10.a: %.1f КБ' % (os.path.getsize(library) / 1024))
    print('объявлений в заголовке:',
          sum(1 for l in open(os.path.join(folder, '_as-cm10.h'), encoding='utf-8')
              if l.rstrip().endswith(');')))
