<p align="center"><img src="brand/store-icon.png" width="120" alt="IZ2S Store icon"></p>

<h1 align="center">IZ2S Store</h1>

<p align="center">Browse, install, and update homebrew on Nintendo Switch 2.</p>

<p align="center"><a href="https://github.com/byizmysh/iz2s-store/releases/latest">Download</a> · <a href="#add-a-catalog">Add a catalog</a> · <a href="https://github.com/byizmysh/iz2s-store-catalog">Catalog repository</a></p>

## Pick your build

| Console environment | Download and extract to the microSD root | Apps this build installs |
| --- | --- | --- |
| [IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform) | `iz2s-store-iza-sd.zip` | **IZA** from SD and **NRO** from GitHub or SD |
| [kaiHEN](https://github.com/ouncebrew/kaihen) | `iz2s-store-nro-sd.zip` | **NRO** from GitHub or SD |

Both builds are for **Nintendo Switch 2**.

### IZ2S Platform · IZA

Use **IZ2S Platform 0.25 with the IZ2S Store 0.7.0 hosted service**. Restart IZ2S after updating the runtime.

- **From a microSD card:** Extract `iz2s-store-iza-sd.zip` to the card root, then open **IZ2S Store** from the IZ2S menu. For a manual copy, save `iz2s-store.iza` as `/iz2s/modules/iz2s-store/launcher.iza`.
- **From IZ2S Platform on PC:** With the console paired over PC Link, go to **App library → Add app** and select `iz2s-store-iza-sd.zip`. Open **Add App** on the console if the import is still queued, then launch the Store tile. You can also use **IZ2S SD on PC** in the App library to install to a mounted microSD card.

Apps installed by this build go to `/iz2s/modules/<id>/launcher.iza` or `/iz2s/modules/<id>/launcher.nro`.

### kaiHEN · NRO

Extract `iz2s-store-nro-sd.zip` to the microSD root, or copy `iz2s-store.nro` to `/switch/iz2s-store/iz2s-store.nro`. Launch **IZ2S Store** from the kaiHEN homebrew menu.

This build installs NRO apps to `/switch/<id>/<id>.nro`. IZA apps require IZ2S Platform.

## Add a catalog

The Store starts with an empty library. The catalog and thumbnails are supplied separately.

1. Open **Settings** with **−** and select **Catalog URL**.
2. Paste the public Raw URL of `catalog.tsv`. For the companion catalog, use:

   ```text
   https://raw.githubusercontent.com/byizmysh/iz2s-store-catalog/main/catalog.tsv
   ```

3. Confirm the URL. The Store downloads the list and looks for `artwork.bin` in the same directory. Use **Settings → Download catalog** to check for new entries and versions. The console downloads NROs directly from their publishers' GitHub Releases and checks file size and SHA-256 before installing. The cached list and thumbnails remain available offline.

**Use a catalog from microSD.** Put `catalog.tsv` and optional `artwork.bin` in `/iz2s/modules/iz2s-store/catalog/` for IZA, or `/switch/iz2s-store/catalog/` for NRO. Select **Settings → Catalog source → SD card** and choose **Reload SD catalog**. Entries with a GitHub link still need internet to download. For offline installs, the catalog must describe payload files already on the card. See the [catalog format](https://github.com/byizmysh/iz2s-store-catalog#catalog-format).

## Use the Store

Open an app card for its details, publisher link, and **Download** or **Update** action. Press **Y** to minimize a transfer or **B** to cancel from the download screen. **Installed** lists apps from the active catalog. Press **X** in app details to remove a Store-managed installation and keep its saved data.

<details>
<summary>Build from source</summary>

Use [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) with libounce, Node.js, Python 3, and Pillow. Set `DEVKITPRO` or `IZ_DEVKITPRO`.

```sh
IZ_STORE_FONT_DIR=/path/to/dejavu sh build.sh nro
IZ2S_PLATFORM_ROOT=/path/to/IZ2S-Platform sh build.sh iza
```

The font directory must contain `DejaVuSans.ttf` and `DejaVuSans-Bold.ttf`. IZA builds use the fonts from IZ2S Platform by default and require the SB4 headers beside devkitPro, plus `glslangValidator` and `spirv-val`. Use `both` to build both editions or set `IZ_STORE_OUT` to choose the output directory.

</details>

Made by [IZMYSH](https://github.com/byizmysh). [MIT License](LICENSE) · [Third-party notices](licenses)
