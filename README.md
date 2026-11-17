# IZ2S Store

IZ2S Store is a homebrew app browser and installer for Nintendo Switch 2.
It supports an IZA build for IZlauncher and a standalone NRO build for the
Switch 2 homebrew menu. The interface is in English.

## Install

- **IZlauncher** — extract `iz2s-store-iza-sd.zip` to the SD root, or copy
  `iz2s-store.iza` as `/iz2s/modules/iz2s-store/launcher.iza`. This
  edition requires an IZ2S Platform 0.25 runtime with the IZ2S Store 0.7.0
  hosted service. After updating the runtime, restart IZlauncher.
- **Switch 2 homebrew menu** — copy `iz2s-store.nro` as
  `/switch/iz2s-store/iz2s-store.nro`. The standalone build
  needs a Switch 2 homebrew environment with libounce support.

## Add a catalog

1. Open **Settings** with **−** and select **Catalog URL** to use an online catalog.
2. Enter the raw HTTPS URL of an external GitHub `catalog.tsv`, or its HTTP URL
   on a private IPv4 LAN server.
3. Confirm the URL. The Store downloads the catalog and its thumbnails
   automatically. **Download catalog** refreshes both files. The cache is
   reused when the Store opens, including when offline. If you have a saved URL
   and are missing the catalog or its thumbnails, the Store tries to download
   the missing data when it opens.

For a local catalog, place `catalog.tsv` and `artwork.bin` together:
- IZA: `/iz2s/modules/iz2s-store/catalog/`
- NRO: `/switch/iz2s-store/catalog/`

Choose **SD card** in **Settings → Catalog source** and press **Y** to reload
after changing the files. The Store shows entries from the selected source.
Older SD catalogs in `/iz2s/iz2s-store/` are supported as a fallback.

The repository and release packages ship without a catalog or application
payloads. A fresh installation opens with an empty list and no configured URL.

When upgrading from an older Store installation, remove its old launcher
file to avoid duplicate tiles. The new Store moves its settings, cache,
artwork and local packages from the former data folder on first launch if
the new folder is absent.

## Use the store

Use the controller or touch screen to browse categories and app details.
Open the repository tile with **R** to see the publisher URL. Select
**Download** or **Update** to install a verified release. **B** cancels a
download and **Y** minimizes it while you continue browsing. **X** opens the
removal dialog for apps managed by the store. Saved data is kept. Installed
lists only entries in the active catalog. If its thumbnail is missing, the
Store looks for the installed app's thumbnail or uses its embedded icon.

Press **X** on the catalog screen for About and the author and project links.

The IZA edition installs to `/iz2s/modules/<id>/`. The standalone edition
installs NROs to `/switch/<id>/<id>.nro`. IZA entries require IZlauncher.

## Catalog format

Use UTF-8, tabs between fields and an `IZSTORE2` header on the first line.
For each online application, place these records together:

| Record | Fields after the record name |
| --- | --- |
| `app` | ID, `nro`, version, title, author, category, description |
| `repository` (optional) | HTTPS project URL |
| `file` | Exact byte count, lowercase SHA-256, `launcher.nro` |
| `github` | Direct HTTPS URL for a tagged GitHub Release NRO asset |

The list can contain up to 24 entries. App IDs must be unique. The store
checks byte count, SHA-256, and Switch 2 NRO compatibility before installation.
Downloads go directly from the console to the release server. A LAN server
hosting the catalog does not need to proxy the releases.

Publish `artwork.bin` beside the catalog. It is optional, so text-only feeds
still work. The image pack begins with the ASCII bytes `IZART1\n` and one
unsigned byte containing the number of images (1–24). Each record contains
a 48-byte NUL-padded app ID and 96 × 96 straight RGBA pixels (36,864 bytes)
in row order. Use the same IDs as the catalog. The Store caches the pack
on the SD card, keyed by the configured catalog URL.

## Build

Use a Switch 2 devkitPro installation with libounce and set `DEVKITPRO`
or `IZ_DEVKITPRO`. Install Node.js, Python and Pillow. Point
`IZ_STORE_FONT_DIR` at a folder containing `DejaVuSans.ttf` and
`DejaVuSans-Bold.ttf`, or use the default font folder in IZ2S Platform.

```sh
IZ_STORE_FONT_DIR=/path/to/dejavu sh build.sh nro
```

To build IZA, set `IZ2S_PLATFORM_ROOT` to a checkout of IZ2S Platform 0.25
with the Store 0.7.0 hosted service. Install the SB4 headers alongside the
devkitPro folder and put `glslangValidator` and `spirv-val` on PATH.

```sh
IZ2S_PLATFORM_ROOT=/path/to/IZ2S-Platform sh build.sh both
```

Use `nro`, `iza`, or `both`. Set `IZ_STORE_OUT` to change the output
directory. Use `IZ_STORE_CHECK=1` to run package and catalog checks with a
host C compiler and OpenSSL development headers.

The Store is MIT licensed. The GitHub mark is from Primer Octicons. The NRO icon decoder uses stb_image. Third-party notices are included with the build.
