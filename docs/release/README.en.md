# Local Public Candidate and Release Gates

[中文](README.md) | [English](README.en.md)

This stage generates the root `OPEN_REPOSITORY/` review candidate from the single local source-of-truth workspace through an allowlist. It is a reproducible temporary release artifact, not a second maintained source tree. This stage does not create a remote repository, commit, push, or clean the private source.

Include reviewed root docs, the `skills/` orchestrator, 11 child Skills, shared references, public state scripts and tests, CODE metadata/components, and only `publish` or `publish-with-limitations` Examples. Exclude held/archived/unavailable sources, historical board drafts, builds, generated configuration, caches, logs, firmware backups, private requirements/design/memory/TODO, migration snapshots, unapproved binary/media assets, and maintainer-specific scripts/configuration.

The candidate must pass internal-document, generated-file, secret/device/path/port, binary allowlist, directory/manifest, Markdown-link, bilingual-key-field, Skill-package/state/failure-branch, and `display_demo` build checks. Until final S5, automated build/flash/monitor retains `S5_PORTABLE_ENTRY_PENDING`. Unexecuted platforms remain explicitly unverified.

The root license, asset licenses, `net_mgmt` delivery contract, S9 CI, repository identity, and every Git action remain unapproved. The local candidate is not an open-source release.

See [OPEN development Skills](../../skills/README.en.md). Valid packages and logic tests do not replace Windows/macOS empty/existing-environment replay, which still depends on the final-S5 portable entry and access to both platforms.
