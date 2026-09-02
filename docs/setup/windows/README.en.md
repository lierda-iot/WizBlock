# Windows Setup

[中文](README.md) | [English](README.en.md)

1. Connect the board with a data-capable cable and confirm the CH340E serial device in Device Manager.
2. Install Git and verify Git Bash.
3. Install ESP-IDF v5.5.4 and activate its official environment.
4. Check `idf.py --version`, Python, CMake, and Ninja.
5. Use a short ASCII checkout path. If the private source path is non-ASCII, use the complete Git Bash mirror procedure in `CODE/README.en.md`.

Do not copy a maintainer drive, Python path, toolchain directory, certificate path, or COM port. Existing validated scripts remain unchanged and are excluded from the public candidate; the portable public entry remains final-S5 work.

In the current maintainer environment, first mirror the complete `./CODE` tree with Git Bash, then invoke `& "./CODE/tools/build_example.ps1" -Example <example> -Clean` from PowerShell at the workspace root. Direct `idf.py` calls are not presented as a validated public alternative. After any source change, repeat both the complete mirror and `-Clean` entry.
