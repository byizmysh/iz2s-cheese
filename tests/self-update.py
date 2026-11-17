#!/usr/bin/env python3
from pathlib import Path
import hashlib, os, subprocess, sys, tempfile
from zipfile import ZipFile
module=Path(__file__).resolve().parent.parent
release=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='store-self-update-') as name:
    work=Path(name)
    for kind in ['iza','nro']:
        sd=work/kind
        folder=sd/('iz2s/modules/iz2s-store' if kind=='iza' else 'switch/iz2s-store')
        folder.mkdir(parents=True)
        active=folder/('launcher.iza' if kind=='iza' else 'iz2s-store.nro')
        previous=b'previous executable';active.write_bytes(previous)
        catalog=sd/'iz2s/iz2s-store/catalog.tsv';catalog.parent.mkdir(parents=True)
        program=release/f'iz2s-store.{kind}'
        files=[('launcher.'+kind, program)]
        if kind=='iza':
            with ZipFile(release/'iz2s-store-iza-sd.zip') as z:
                for p in z.namelist():
                    if p.endswith('.izdata'):files.append((Path(p).name,release/Path(p).name))
        rows=['IZSTORE2',f'app\tiz2s-store\t{kind}\t0.8.0\tIZ2S Store\tIZMYSH\tTools\tHomebrew store.']
        rows += [f'file\t{p.stat().st_size}\t{hashlib.sha256(p.read_bytes()).hexdigest()}\t{n}' for n,p in files]
        rows += [f'github\thttps://github.com/byizmysh/iz2s-store/releases/download/v0.8.0/iz2s-store.{kind}']
        catalog.write_text('\n'.join(rows)+'\n')
        binary=work/('test-'+kind)
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
            *(['-DIZ_STORE_STANDALONE'] if kind=='nro' else []),'-I'+str(module/'switch'),
            module/'tests/self-update.c',*(module/'switch'/f for f in ['store.c','ui.c','download.c','installed-artwork.c']),
            '-Wl,--wrap=github_download','-lssl','-lcrypto','-o',binary],check=True)
        subprocess.run([binary,sd],env={**os.environ,'STORE_TEST_RELEASE':str(release)},check=True)
        assert active.read_bytes()==program.read_bytes()
        assert (folder/f'.store-previous.{kind}.bak').read_bytes()==previous
        for n,p in files[1:]:assert (folder/n).read_bytes()==p.read_bytes()
