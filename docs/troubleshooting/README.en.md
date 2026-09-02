# Troubleshooting

[中文](README.md) | [English](README.en.md)

Handle the first failing layer. Do not change the script, toolchain, port, and source simultaneously.

- Missing `idf.py`: activate the same ESP-IDF v5.5.4 environment.
- Missing Example: check `examples.yml`; held, archived, and unavailable entries are not build targets.
- Stale source: repeat the complete mirror and clean build.
- Dependency failure: fix the first Component Manager/network error without changing IDF versions.
- Windows path issue: use the Git Bash complete mirror into a short ASCII path.
- Confirmed GCC ICE: keep the same entry and reduce build parallelism to two.
- Missing/busy serial port: identify the device by unplug/replug or close the owning monitor; do not erase to fix ownership.
- Successful flash but no boot: inspect ROM mode and the first application error before rewriting flash.
- Reset: look for panic/assert/watchdog/brownout/reset evidence and do not guess a root cause.

When `design.md` records a mature entry, keep using it and stop at the first relevant failure rather than trying another runner.

