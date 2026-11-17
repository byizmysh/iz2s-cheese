#!/bin/sh
set -eu

module_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
platform_dir=${IZ2S_PLATFORM_ROOT:-$(CDPATH='' cd -- "$module_dir/../.." && pwd)}
font_dir=${IZ_STORE_FONT_DIR:-"$platform_dir/language/fonts"}
DEVKITPRO=${IZ_DEVKITPRO:-${DEVKITPRO:?Set DEVKITPRO or IZ_DEVKITPRO to the installed Switch 2 SDK}}
export DEVKITPRO
cross=${IZ_AARCH64_PREFIX:-"$DEVKITPRO/devkitA64/bin/aarch64-none-elf-"}
include="$DEVKITPRO/../sb4/include"
if [ -f "$platform_dir/core/archive.cjs" ]; then
    output=${IZ_STORE_OUT:-"$platform_dir/dist/iz-homebrew-store"}
else
    output=${IZ_STORE_OUT:-"$module_dir/.build"}
fi
case "${1:-both}" in
    both) build_nro=1; build_iza=1 ;;
    nro) build_nro=1; build_iza=0 ;;
    iza) build_nro=0; build_iza=1 ;;
    *) echo "Usage: sh build.sh [both|nro|iza]" >&2; exit 2 ;;
esac
if [ "$build_iza" = 1 ] && [ ! -f "$platform_dir/core/archive.cjs" ]; then
    echo "Set IZ2S_PLATFORM_ROOT to a checkout of IZ2S Platform to build IZA." >&2
    exit 2
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/sd" "$work/romfs" "$output"
python3 -B "$module_dir/assets.py" "$font_dir" "$work/romfs" "$work/icon.jpg"
cp "$module_dir/licenses/DejaVu.txt" "$work/romfs/font-license.txt"
if [ "$build_nro" = 1 ]; then
    cp "$DEVKITPRO"/libounce/licenses/*.txt "$work/romfs/"
    "${cross}gcc" -std=gnu11 -O2 -Wall -Wextra -Werror -mcpu=cortex-a78c+crypto -mtp=soft \
        -fPIE -ffunction-sections -fdata-sections -D__SWITCH__ -D__SWITCH2__ -DIZ_STORE_STANDALONE \
        -isystem "$DEVKITPRO/libounce/include" -specs="$DEVKITPRO/libounce/switch.specs" \
        -L"$DEVKITPRO/libounce/lib" "$module_dir/switch/main.c" "$module_dir/switch/store.c" \
        "$module_dir/switch/ui.c" "$module_dir/switch/download.c" -lounce -o "$work/standalone.elf"
    version=$(node -p 'require(process.argv[1]).version' "$module_dir/module.json")
    tools="$DEVKITPRO/tools/bin"
    "$tools/nacptool" --create 'IZ2S Store' IZMYSH "$version" "$work/standalone.nacp"
    "$tools/elf2nro" "$work/standalone.elf" "$output/IZ2S-Store-Standalone.nro" \
        --nacp="$work/standalone.nacp" --icon="$work/icon.jpg" --romfsdir="$work/romfs"
    python3 - "$output/IZ2S-Store-Standalone.nro" "$output/IZ2S-Store-Standalone-SD.zip" <<'PY'
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
import os
import sys

source, target = map(Path, sys.argv[1:])
stage = target.with_suffix('.tmp')
with ZipFile(stage, 'w', ZIP_DEFLATED) as pack:
    pack.write(source, 'switch/iz-homebrew-store/iz-homebrew-store.nro')
os.replace(stage, target)
print(target)
PY
fi

if [ "$build_iza" = 1 ]; then
mkdir -p "$module_dir/switch/iza/assets"
cp "$module_dir/brand/store-icon.png" "$module_dir/switch/iza/assets/icon.png"
python3 -B "$module_dir/switch/iza/assets.py" "$work/romfs" "$module_dir/switch/iza/assets/data.bin"
python3 - "$module_dir/switch/iza/assets/state.bin" <<'STATE'
from pathlib import Path
import sys
Path(sys.argv[1]).write_bytes(bytes(1024))
STATE
for stage in vert frag comp; do
    glslangValidator -V -Os -g0 --target-env vulkan1.0 "$module_dir/switch/iza/app.$stage" -o "$module_dir/switch/iza/app.$stage.spv"
    spirv-val --target-env vulkan1.0 "$module_dir/switch/iza/app.$stage.spv"
done
"${cross}gcc" -std=c11 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -fPIC -fvisibility=hidden -fno-stack-protector -fno-jump-tables -mbranch-protection=bti \
    -I "$include" -c "$module_dir/switch/iza/entry.c" -o "$work/entry.o"
"${cross}ld" --no-undefined --fatal-warnings -T "$module_dir/switch/iza/entry.ld" "$work/entry.o" -o "$work/entry.elf"
"${cross}objcopy" -O binary -j .text "$work/entry.elf" "$module_dir/switch/iza/native.bin"
node - "$platform_dir" "$module_dir" "$work" <<'JS'
const fs = require('node:fs/promises');
const path = require('node:path');
const [root, moduleDir, work] = process.argv.slice(2);
const archive = require(path.join(root, 'core/archive.cjs'));
const {prepare} = require(path.join(root, 'engines/iz2s-core/tools/prepare-sd-app.cjs'));
(async () => {
  const packaged = path.join(moduleDir, 'iz-homebrew-store.izm');
  await fs.writeFile(packaged, await archive.pack(moduleDir));
  const result = await prepare(packaged, path.join(work, 'prepared'));
  if (result.abi !== 7 || result.id !== 'iz-homebrew-store') throw Error('Unexpected IZA layout');
  const folder = path.join(work, 'sd', 'iz2s', 'modules', result.id);
  await fs.cp(result.folder, folder, {recursive: true});
  console.log(packaged);
})().catch(error => { console.error(error.stack); process.exitCode = 1; });
JS

python3 - "$work/sd" "$output/IZ-Homebrew-Store-SD.zip" <<'PY'
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
import os
import sys
root, target = map(Path, sys.argv[1:])
stage = target.with_suffix('.tmp')
with ZipFile(stage, 'w', ZIP_DEFLATED) as pack:
    for source in sorted(root.rglob('*')):
        if source.is_file():
            pack.write(source, source.relative_to(root).as_posix())
os.replace(stage, target)
print(target)
PY

cp "$work/sd/iz2s/modules/iz-homebrew-store/launcher.iza" "$output/IZ2S-Store.iza"
fi

if [ "${IZ_STORE_CHECK:-0}" = 1 ]; then
    python3 -B "$module_dir/check.py" "$output" "$build_iza" "$build_nro"
fi
