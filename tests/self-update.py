#!/usr/bin/env python3
from pathlib import Path
import hashlib, os, subprocess, sys, tempfile
module=Path(__file__).resolve().parent.parent
release=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='store-self-update-') as name:
    work=Path(name)
    for kind in ['iza','nro']:
        sd=work/kind
        folder=sd/('iz2s/modules/iz2s-cheese' if kind=='iza' else 'switch/iz2s-cheese')
        folder.mkdir(parents=True)
        active=folder/('launcher.iza' if kind=='iza' else 'iz2s-cheese.nro')
        previous=b'previous executable';active.write_bytes(previous)
        catalog=sd/'iz2s/iz2s-cheese/catalog.tsv';catalog.parent.mkdir(parents=True)
        program=release/f'iz2s-cheese.{kind}'
        rows=['IZSTORE3']
        for variant in ['iza','nro']:
            payload=(release/f'iz2s-cheese.{variant}').read_bytes()
            rows.extend([
                f'app\tiz2s-cheese\t{variant}\t0.8.7\tIZ2S Cheese\tIZMYSH\tTools\tHomebrew store.',
                f'file\t{len(payload)}\t{hashlib.sha256(payload).hexdigest()}\tlauncher.{variant}',
                f'github\thttps://github.com/byizmysh/iz2s-cheese/releases/download/v0.8.7/iz2s-cheese.{variant}',
            ])
        catalog.write_text('\n'.join(rows)+'\n')
        binary=work/('test-'+kind)
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
            *(['-DIZ_STORE_STANDALONE'] if kind=='nro' else []),'-I'+str(module/'switch'),
            module/'tests/self-update.c',*(module/'switch'/f for f in ['store.c','ui.c','download.c','installed-artwork.c']),
            '-Wl,--wrap=github_download','-lssl','-lcrypto','-o',binary],check=True)
        subprocess.run([binary,sd],env={**os.environ,'STORE_TEST_RELEASE':str(release)},check=True)
        assert active.read_bytes()==program.read_bytes()
        assert (folder/f'.store-previous.{kind}.bak').read_bytes()==previous
        assert not list(folder.glob('*.izdata')) and not list(folder.glob('sd-bundle*.json'))
