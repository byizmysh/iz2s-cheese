#!/usr/bin/env python3
"""Link the Cheese executable into the launcher's position-independent native section."""
import argparse
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
PAGE_RELATIVE = {
    'R_AARCH64_ADR_PREL_LO21', 'R_AARCH64_ADR_PREL_PG_HI21', 'R_AARCH64_ADR_PREL_PG_HI21_NC',
    'R_AARCH64_ADD_ABS_LO12_NC', 'R_AARCH64_LD_PREL_LO19', 'R_AARCH64_PREL32',
    'R_AARCH64_CALL26', 'R_AARCH64_JUMP26', 'R_AARCH64_CONDBR19', 'R_AARCH64_TSTBR14',
    *(f'R_AARCH64_LDST{width}_ABS_LO12_NC' for width in (8, 16, 32, 64, 128)),
}


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def build(args):
    if 'libounce 9382b1ec48ac' not in (args.devkitpro / 'ounce-devkit-version.txt').read_text().splitlines():
        raise ValueError('Native services v1 requires libounce 9382b1ec48ac')
    work = args.work
    work.mkdir(parents=True, exist_ok=True)
    names = json.loads((HERE / 'imports.json').read_text())
    if len(names) != len(set(names)) or any(not n.isidentifier() for n in names):
        raise ValueError('Invalid import manifest')
    width = max(map(len, names)) + 1
    header = ('#ifndef IZ_STORE_IMPORTS_H\n#define IZ_STORE_IMPORTS_H\n'
              f'enum {{ STORE_IMPORT_COUNT = {len(names)} }};\n'
              f'static const char store_import_names[STORE_IMPORT_COUNT][{width}] = {{\n'
              + ''.join(f'    {json.dumps(name)},\n' for name in names) + '};\n#endif\n')
    (work / 'store-imports.h').write_text(header)
    asm = '.arch armv8-a\n'
    for i, name in enumerate(names):
        # Native and worker entries set x28 to the SDK allocation and preserve it on return.
        asm += (f'.section .text.{name},"ax",%progbits\n.global {name}\n.hidden {name}\n'
                f'.type {name},%function\n{name}:\n bti c\n ldr x16, [x28, #{8*i}]\n br x16\n'
                f'.size {name}, .-{name}\n')
    (work / 'imports.S').write_text(asm)
    flags = ['-std=gnu11', '-Os', '-Wall', '-Wextra', '-Werror', '-fpie', '-ffixed-x28',
             '-fvisibility=hidden', '-fno-builtin', '-fno-stack-protector', '-fno-jump-tables',
             '-ffunction-sections', '-fdata-sections', '-mbranch-protection=bti', '-mno-outline-atomics',
             '-D__SWITCH__', '-D__SWITCH2__', '-DIZ_STORE_MODULE', '-DSTBI_NO_THREAD_LOCALS',
             f'-DIZ_STORE_VERSION="{args.version}"', '-I', work, '-I', args.include,
             '-isystem', args.devkitpro / 'libounce/include']
    sources = [HERE / 'entry.c', HERE / 'entry.S', work / 'imports.S',
               *(HERE.parent / name for name in ('main.c', 'store.c', 'ui.c', 'download.c', 'installed-artwork.c'))]
    objects = []
    for source in sources:
        obj = work / (source.name + '.o')
        # stb_image declares functions for disabled formats even with static linkage.
        warning_flags = ['-Wno-unused-function'] if source.name == 'installed-artwork.c' else []
        run(args.cross + 'gcc', *flags, *warning_flags, '-c', source, '-o', obj)
        objects.append(obj)
    elf = work / 'cheese.elf'
    run(args.cross + 'ld', '--no-undefined', '--fatal-warnings', '--gc-sections', '--emit-relocs',
        '-T', HERE / 'entry.ld', *objects, '-o', elf)
    relocations = subprocess.check_output([args.cross + 'readelf', '-Wr', elf], text=True)
    unsupported = set(re.findall(r'R_AARCH64_\w+', relocations)) - PAGE_RELATIVE
    if unsupported:
        raise ValueError(f'The native section must run at any page address: {sorted(unsupported)}')
    run(args.cross + 'objcopy', '-O', 'binary', '-j', '.text', elf, args.output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--devkitpro', type=Path, required=True)
    parser.add_argument('--include', type=Path, required=True)
    parser.add_argument('--cross', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args())
