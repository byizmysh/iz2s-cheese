# IZ2S Store

A homebrew store for Nintendo Switch 2.

<p>
  <a href="screenshots/catalog.png"><img src="screenshots/catalog.png" width="49%" alt="IZ2S Store catalog with app tiles and categories"></a>
  <a href="screenshots/app-details.png"><img src="screenshots/app-details.png" width="49%" alt="App details with the download button and publisher link"></a>
</p>

## Choose your build

| Console environment | Supported apps |
| --- | --- |
| [IZ2S](https://github.com/byizmysh/IZ2S-Platform) | **.iza** and **.nro** |
| [kaiHEN](https://github.com/ouncebrew/kaihen) | **.nro** |

## Install on IZ2S

**IZ2S Platform 0.25 or later is required.** Restart the IZ2S session after updating the platform.

- **From microSD.** Extract [`iz2s-store-iza-sd.zip`](https://github.com/byizmysh/iz2s-store/releases/latest/download/iz2s-store-iza-sd.zip) on your PC. Copy the extracted `iz2s/` folder to the root of the card. To copy the executable manually, save `iz2s-store.iza` as `/iz2s/modules/iz2s-store/launcher.iza`. Open the **IZ2S Store** tile.
- **From IZ2S Platform on PC.** Connect the console via PC Link, open **App library → Add app**, and select `iz2s-store-iza-sd.zip` directly. The panel imports the ZIP. If the import is still queued, open **Add App** on the console.

The Store installs apps to `/iz2s/modules/<id>/launcher.iza` or `/iz2s/modules/<id>/launcher.nro`.

### Install on kaiHEN

Extract [`iz2s-store-nro-sd.zip`](https://github.com/byizmysh/iz2s-store/releases/latest/download/iz2s-store-nro-sd.zip) to the card root or copy `iz2s-store.nro` to `/switch/iz2s-store/iz2s-store.nro`. Launch **IZ2S Store** from the homebrew menu. The Store installs apps to `/switch/<id>/<id>.nro`. IZA apps require IZ2S Platform.

## Add a catalog

The list is empty on first launch. Press **−**, select **Catalog URL**, and paste the raw URL of `catalog.tsv`.

The Store looks for `artwork.bin` alongside `catalog.tsv`. It saves the list and thumbnails to the card for offline browsing. Use **Settings → Download catalog** to refresh both files. IZ2S can download IZA and NRO apps from the same catalog. On kaiHEN, IZA entries are marked as requiring IZ2S.

For a local catalog, place `catalog.tsv` and `artwork.bin` in `/iz2s/modules/iz2s-store/catalog/` for IZA or `/switch/iz2s-store/catalog/` for NRO. Select **Settings → Catalog source → SD card**, then **Reload SD catalog**.

## Publish your own catalog

Create a separate public GitHub repository. Put `catalog.tsv` and `artwork.bin` at the root of the `main` branch. Open `catalog.tsv`, click **Raw**, and copy the URL into IZ2S Store.

See the [catalog format](docs/catalog-format.md) to list IZA releases and their companion files.
