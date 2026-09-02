# Third-Party Notices

## esp_player

- Package: `espressif/esp_player`
- Version: `1.0.3`
- Upstream source reference: `3ae5408f2c900270c3c0dc6e4e84dc8760306ad3`
- License: Espressif Modified MIT License (`LicenseRef-Espressif-Modified-MIT`)
- Usage: MP3 playback, state, position, duration and seek APIs.
- Local changes: none; consumed through the ESP-IDF Component Manager.

The package license is distributed by the Component Manager with the resolved component and restricts use to Espressif products.

## esp_new_jpeg

- Package: `espressif/esp_new_jpeg`
- Version: `1.0.2`
- Upstream source reference: `daee964980d14d7fcf9da10821827399ef600577`
- License: Espressif MIT License for Espressif products.
- Usage: baseline JPEG cover decoding.
- Local changes: none; consumed through the ESP-IDF Component Manager.

## gmf_io

- Package: `espressif/gmf_io`
- Version: `1.0.0`
- License: Espressif Modified MIT License (`LicenseRef-Espressif-Modified-MIT`)
- Usage: `file://` input used by `esp_player`.
- Local changes: the Demo-local override adds paired file-lock callbacks so every file operation shares the SPI2 mutex used by LCD and TF access.

The complete license is retained at `components/gmf_io/LICENSE`. The exact local delta and compatibility boundary are documented in `components/gmf_io/LOCAL_CHANGES.md`.

## Noto Sans SC

- Asset: Noto Sans SC, converted to the Demo-local 16px, 2bpp binary font.
- License: SIL Open Font License 1.1.
- Usage: Chinese song titles, lyrics and song-list text.
- Local changes: glyph rasterization and binary packaging only; the font name and license are retained.

The complete license is retained at `fonts/OFL.txt`; the reproducible generator is `fonts/generate_mp3_font.py`.
