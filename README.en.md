# AI Open Hardware

[中文](README.md) | [English](README.en.md)

A component-oriented ESP32-S3 example project for the L-AIWFS300 board family. The workspace is preparing its first public candidate through factual, secret, binary-asset, and licensing gates.

> This is not an open-source release yet. The root license, third-party asset permissions, the delivery form of the self-developed `net_mgmt` libraries, and the public repository identity are pending owner decisions. The root `OPEN_REPOSITORY/` directory is only a temporary allowlist-generated review artifact from this single source-of-truth workspace; it is not a second maintained source tree and grants no license.

> In the current Windows maintainer environment, the existing script entry in `design.md` 4.1.2 completed a `1421/1421` clean build with a 200-second caller window. Those unchanged scripts contain machine-specific configuration and are excluded from the public candidate, so this evidence does not validate a clean-machine public entry. Portability and two-platform replay remain final-S5 work.

## Start here

- [Quick Start draft](docs/quick-start/README.en.md)
- [Source tree and build procedure](CODE/README.en.md)
- [Example index](CODE/examples/README.en.md)
- [Component index](CODE/components/README.en.md)
- [Developer documentation](docs/README.en.md)
- [OPEN development Skills](skills/README.en.md)
- [Release candidate and gates](docs/release/README.en.md)

The first example is `display_demo`. The current source fills the LCD with white, red, green, and blue in sequence, holding each color for two seconds. The current four-color source has clean-build evidence; complete four-color device replay is still pending.

## Support statement

| Item | Current statement |
| --- | --- |
| Target chip | ESP32-S3 |
| Target board | L-AIWFS300; `korvo2_mic_test` separately targets ESP32-S3-Korvo-2 V3 |
| ESP-IDF | v5.5.4 has project evidence; other versions are unsupported/unverified |
| Windows | In first-release scope; public-candidate build awaits clean-machine replay |
| macOS | In first-release scope; public-candidate build awaits clean-machine replay |
| Linux | Not promised in the first release |
| Build concurrency | The current mirror is shared; build Examples serially |

## Build principle

The validated entries are the existing `CODE/tools/build_example.sh`, `build_example.ps1`, and `build_example_macos.sh` scripts in the private source workspace. Documentation records those entries with repository-relative paths and no longer presents direct `idf.py` calls as a validated alternative.

The scripts still contain maintainer-specific paths or a historical fixed port, so they are excluded from the public candidate. A portable build/flash/monitor entry and clean-machine Windows/macOS replay remain final-S5 work. Until then, the Quick Start is explicitly a draft; see [CODE/README.en.md](CODE/README.en.md).

`skills/` now provides a dependency-aware orchestrator, 11 independently invocable child Skills, a shared status/redaction runtime, and behavior tests. The Skills preserve the same boundary: automated build/flash/monitor returns `S5_PORTABLE_ENTRY_PENDING` until final S5 instead of synthesizing a replacement command.

## Candidate status

`CODE/examples/examples.yml` is the machine-readable list for 35 formal entries and 33 current directories. `publish` and `publish-with-limitations` only permit inclusion in a local review candidate. `hold`, `archive`, and `unavailable` entries are not copied as buildable public Examples.

## Security and licensing

- Never commit real Wi-Fi credentials, tokens, certificates, device identifiers, internal addresses, or maintainer absolute paths.
- Every non-source asset needs a source, purpose, SHA-256, license, and regeneration note before inclusion.
- `net_mgmt` is self-developed, but its license, ABI, and source/binary delivery form are pending.
- Do not describe the candidate as open source until the root license is approved.
