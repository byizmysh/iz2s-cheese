# Build IZ2S Cheese

Set `IZ_DEVKITPRO` (or `DEVKITPRO`) to ounce-devkit 20261114 with libounce `9382b1ec48ac`. Install Node.js, Python, Pillow, `glslangValidator` and `spirv-val`. Set `IZ_STORE_FONT_DIR` to a directory containing DejaVu Sans regular and bold fonts.

```sh
sh build.sh nro
IZ2S_PLATFORM_ROOT=/path/to/IZ2S-Platform sh build.sh both
```

For IZA, install the updated libsb4 headers alongside the devkitPro directory. `IZ_STORE_OUT` overrides the release directory. `IZ_STORE_CHECK=1` enables package and catalog tests, which also require a host C compiler and OpenSSL development headers.

Publish `iz2s-cheese.iza` and `iz2s-cheese.nro`. The built-in updater uses the same files as a fresh installation. License texts are in the README.

## Native module

`switch/iza/build-native.py` links the application into the IZA native section. It verifies that the loader can map the code at any page-aligned address. The SDK allocation holds the import table and a pointer to the session. The native entry and background worker establish that context before calling application code. The launcher must grant application I/O to the `iz2s-cheese` module ID.

The image resolves the explicit imports in `switch/iza/imports.json` before running application code. Native services v1 uses the pinned libounce C ABI. Missing services fail before a worker starts, with `startup-error.txt` or `missing-sdk-import.txt` in the module folder. TLS certificate and hostname verification remain enabled. Shared launcher sockets are borrowed and never closed by IZ2S Cheese.

The existing launcher policy grants this installer process-level SD I/O. This does not grant general modules whole-SD access or change the separate scoped storage API. SDK network, TLS, keyboard, input and thread functions are reusable by other applications; they contain no application logic.

STOP cancels an active download and returns `STOP_PENDING` while the worker is alive. The module joins the worker and releases the session before the launcher unloads its code. The updater keeps the previous executable.

## Checks

```sh
python3 check.py /path/to/release 1 1
python3 tests/self-update.py /path/to/release
```

For the ARM64 lifecycle test, set `IZ_LINUX_SYSROOT`, `IZ_SB4_INCLUDE`, and `IZ_QEMU_AARCH64`, then run `sh tests/iza-lifecycle.sh /path/to/release`. It runs the packaged native section at a mapped address with simulated device services. It covers import resolution, repeated startup, keyboard calls, DNS failure, TLS and socket cleanup, and worker shutdown. Physical network and keyboard behavior still requires a console check.

`python3 tests/catalog-size.py` exercises dynamic catalogs and both thumbnail formats with ASan/UBSan. On hosts where LeakSanitizer is unavailable under process tracing, use `ASAN_OPTIONS=detect_leaks=0`.
