# Third-Party Notices

## mevdschee/2048.c

- Source: <https://github.com/mevdschee/2048.c>
- Commit: `afc8898691f54d43309497f4c32682fe90bb5f57`
- License: MIT
- Copyright: Copyright (c) 2024 Maurits van der Schee
- Local use: reference for line compression, one-merge-per-tile behavior, score accumulation, and core test cases.
- Local changes: the terminal UI, `rand()`/time seeding, upstream data representation, and upstream I/O were not retained. This Demo uses an injectable 32-bit random source, unbiased rejection sampling, exponent cells, saturating `uint32_t` scores, atomic failure handling, and a hardware-independent API.

Full license: `licenses/mevdschee-2048.c-MIT.txt`.

## 100askTeam/lv_lib_100ask

- Source: <https://github.com/100askTeam/lv_lib_100ask/tree/v8.x/src/lv_100ask_2048>
- Commit: `b1cdbac458041a996948ff130305428a3baa5874`
- License: MIT
- Copyright: Copyright (c) 2022 深圳百问网科技有限公司(www.100ask.net)
- Local use: reference for an LVGL 8 board, touch entry points, status display, and restart interaction.
- Local changes: the upstream component was not imported. This Demo separates LVGL from the pure-C model, replaces `time(NULL)+rand()`, uses a fixed 320x240 layout, safe number formatting, a 30px contact-level gesture lock, one-step undo, NVS high score storage, and the local display flush path.

Full license: `licenses/100ask-lv_lib_100ask-MIT.txt`.
