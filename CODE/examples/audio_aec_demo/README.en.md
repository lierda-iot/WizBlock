<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# audio_aec_demo (EX-010)

Audio loop-through diagnostic.

## Status

- Public candidate status: `publish-with-limitations`
- Current verification: 当前回归未通过
- Evidence date: 2026-08-07
- Target: ESP32-S3 / ESP-IDF v5.5.4
- Main limitation: Current source disables active AEC and must not be presented as an AEC pass

## Build

Use the relative-path, per-Example build procedure in [CODE/README.en.md](../../README.en.md). The current local build scripts are not modified in this stage.

## Hardware and behavior

See the Chinese README and the public developer documentation for the detailed hardware setup and observable behavior. Unknown or unverified behavior must not be treated as passed.

## License

The repository license and any asset-specific permissions are pending owner approval. Candidate files must not be treated as a released open-source package yet.
