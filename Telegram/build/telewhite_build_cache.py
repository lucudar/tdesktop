'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
# The build cache keeps the out folder of the previous build. When the
# runner image brings another compiler, the old precompiled headers and
# objects can't be used with it (C1853, and /GL objects can't be linked),
# so the out folder is dropped once and the build starts from scratch.
#
#   python telewhite_build_cache.py [--root <repository>]

import os
import re
import shutil
import subprocess
import sys

STAMP = 'telewhite-compiler.txt'


def compiler_version():
    compiler = shutil.which('cl')
    if not compiler:
        return ''
    try:
        result = subprocess.run(
            [compiler],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
    except OSError:
        return ''
    banner = (result.stderr + result.stdout).decode('utf-8', 'replace')
    match = re.search(r'Version\s+([\d.]+)', banner)
    if not match:
        return ''
    return match.group(1) + ' ' + os.path.normcase(compiler)


def check_compiler(root):
    if sys.platform != 'win32':
        return
    version = compiler_version()
    if not version:
        return
    out = os.path.join(root, 'out')
    stamp = os.path.join(out, STAMP)
    old = ''
    if os.path.isfile(stamp):
        with open(stamp, 'r', encoding='utf-8') as f:
            old = f.read().strip()
    if os.path.isdir(out) and old != version:
        text = 'Compiler changed (' + (old or 'unknown') + ' -> ' \
            + version + '), the cached out folder is dropped.'
        print('::notice title=Build cache::' + text)
        subprocess.run(['cmd', '/c', 'rmdir', '/s', '/q', out])
        if os.path.isdir(out):
            shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    with open(stamp, 'w', encoding='utf-8') as f:
        f.write(version + '\n')


if __name__ == '__main__':
    arguments = sys.argv[1:]
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    if '--root' in arguments:
        root = arguments[arguments.index('--root') + 1]
    check_compiler(root)
