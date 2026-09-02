# macOS Setup

[中文](README.md) | [English](README.en.md)

1. Connect the board and identify the new CH340E device under `/dev/cu.*`.
2. Install Xcode Command Line Tools and Git.
3. Install and activate ESP-IDF v5.5.4; verify `idf.py --version`.
4. Confirm `rsync`, enter `./CODE` from the workspace root, and invoke `bash ./tools/build_example_macos.sh <example> clean` in the current maintainer environment.
5. The script performs activation, complete mirroring, and the clean build. Direct `idf.py` calls are not presented as a validated alternative.

The unchanged script still contains maintainer-specific user, Python, certificate, and IDF path assumptions and is excluded from the public candidate. A portable entry remains final-S5 work. If a port is busy, close the owning monitor rather than repeatedly erasing the device.
