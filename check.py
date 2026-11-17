import os
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile

module = Path(__file__).resolve().parent
output = Path(sys.argv[1])
check_iza, check_nro = (value == '1' for value in sys.argv[2:])

if check_iza:
    entry = 'iz2s/modules/iz2s-store/launcher.iza'
    with ZipFile(output / 'iz2s-store-iza-sd.zip') as archive:
        assert set(archive.namelist()) == {
            entry, 'iz2s/modules/iz2s-store/sd-bundle.json'
        }
        iza = archive.read(entry)
    assert iza == (output / 'iz2s-store.iza').read_bytes()
    assert iza.startswith(b'IZSDAPP1')
    artwork = iza.index(b'IZSTOREA')
    assert iza[artwork + 8] == 0
    assert b'IZSTORE2\napp\t' not in iza

if check_nro:
    entry = 'switch/iz2s-store/iz2s-store.nro'
    with ZipFile(output / 'iz2s-store-nro-sd.zip') as archive:
        assert archive.namelist() == [entry]
        nro = archive.read(entry)
    assert nro == (output / 'iz2s-store.nro').read_bytes()
    assert nro[16:20] == b'NRO0'
    mod = struct.unpack_from('<I', nro, 4)[0]
    assert nro[mod:mod+4] == b'MOD0' and b'OUN0\x02\0\0\0\x01\0\0\0' in nro[mod:mod+128]
    aset = struct.unpack_from('<I', nro, 24)[0]
    assert nro[aset:aset+4] == b'ASET'
    fs, size = struct.unpack_from('<2Q', nro, aset+40)
    romfs = nro[aset+fs:aset+fs+size]
    assert size and b'font.bin' in romfs
    assert all(name not in romfs for name in (b'catalog.tsv', b'sources.json', b'payloads', b'IZSTORE2'))

with TemporaryDirectory(prefix='iz-store-check-') as directory:
    work = Path(directory)
    mock = work / 'catalog-download.c'
    mock.write_text(r'''
#include "download.h"
#include <stdio.h>
#include <stdlib.h>

bool __wrap_catalog_download(const char *url, uint64_t limit, DownloadSink sink,
                             void *context, char out[DOWNLOAD_MESSAGE]) {
    (void)url;
    FILE *file = fopen(getenv("IZ_STORE_TEST_CATALOG"), "rb");
    if (!file) return false;
    unsigned char data[1024];
    size_t count;
    bool ok = true;
    while (ok && (count = fread(data, 1, sizeof data, file)) != 0) {
        ok = count <= limit && sink(context, data, count);
        limit -= ok ? count : 0;
    }
    ok = ok && !ferror(file);
    fclose(file);
    if (!ok) snprintf(out, DOWNLOAD_MESSAGE, "Test source read failed.");
    return ok;
}
''')
    url = 'https://raw.githubusercontent.com/example/store-feed/main/catalog.tsv'
    source = work / 'feed.tsv'
    external = os.environ.get('IZ_STORE_CHECK_CATALOG')
    initial = Path(external).read_text() if external else (
        'IZSTORE2\napp\tsample\tnro\t1.0\tSample app\tExample\tTools\tSample application.\n'
        'file\t123\t' + '0' * 64 + '\tlauncher.nro\n'
        'github\thttps://github.com/example/sample/releases/download/v1.0/sample.nro\n'
    )
    app_ids = [line.split('\t')[1] for line in initial.splitlines() if line.startswith('app\t')]
    assert app_ids
    for standalone in (False, True):
        binary = work / 'host-check'
        sd = work / ('sd-nro' if standalone else 'sd-iza')
        sd.mkdir()
        subprocess.run([
            os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            *(['-DIZ_STORE_STANDALONE'] if standalone else []),
            '-I' + str(module / 'switch'), module / 'host-check.c', mock,
            *(module / 'switch' / name for name in ('store.c', 'ui.c', 'download.c')),
            '-Wl,--wrap=catalog_download', '-lssl', '-lcrypto', '-o', binary,
        ], check=True)

        def run(*args, code=0):
            result = subprocess.run([binary, sd, *args], capture_output=True, text=True,
                                    env={**os.environ, 'IZ_STORE_TEST_CATALOG': str(source)})
            assert result.returncode == code, (args, result.stdout, result.stderr)
            return result.stdout

        source.write_text(initial)
        assert not run('list')
        assert not (sd / 'iz2s').exists()
        assert f'{len(app_ids)} online entries' in run('fetch-list', url)
        cache = sd / 'iz2s/iz2s-store/remote.tsv'
        assert cache.read_text() == url + '\n' + initial
        assert [line.split('\t')[0] for line in run('list').splitlines()] == app_ids
        source.write_text('IZSTORE2\ninvalid\n')
        assert 'Invalid list' in run('fetch-list', url, code=1)
        assert cache.read_text() == url + '\n' + initial
        assert len(run('list').splitlines()) == len(app_ids)
        assert 'Enter a catalog URL' in run('fetch-list', '', code=1)
        assert not run('list')
        (cache.parent / 'catalog.tsv').write_text(initial)
        assert len(run('list').splitlines()) == len(app_ids)
        legacy = sd / 'iz2s/store'
        cache.parent.rename(legacy)
        assert len(run('list').splitlines()) == len(app_ids)
        assert cache.read_text() == url + '\n' + initial and not legacy.exists()
        legacy.mkdir()
        (legacy / 'catalog.tsv').write_text('Do not overwrite the active catalog.')
        assert len(run('list').splitlines()) == len(app_ids)
        assert (legacy / 'catalog.tsv').is_file()

print('Verified catalog-free packages, external catalogs, cache recovery and SD data migration.')
