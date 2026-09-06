# Changelog

All notable user-visible changes to Nyx are documented here.

Nyx is currently pre-1.0. Until the first tagged release, current development is collected under `Unreleased`.

## Unreleased

### Added

- Reproducible GitHub Actions CI using the immutable `cxx26-2026.09.05` reference-toolchain snapshot.
- Full development-test, Release, and CPack validation on clean Linux x86_64 runners.
- Canonical `tar.zst` release archives with SHA-256 sidecars.
- Deterministic release provenance recording the exact Nyx source revision, reference-toolchain revision, Miracle/Switch gitlinks, vcpkg gitlink, and package digest.
- Repository contribution and security policies.
- Packaged project and third-party license notices.

### Changed

- Nyx now consumes compiler/runtime configuration exclusively through the external packaged C++26 toolchain instead of reconstructing Clang/libc++ flags locally.
- Miracle and Switch are consumed as pinned standalone source products.
- Release packaging now uses the CMake install tree and CPack rather than the previous debug ZIP path.
- Nyx-owned source is now source-available under the Nyx Source-Available License 1.0 instead of MIT for this and later revisions.

### Removed

- Project-owned `clang-p2996` compiler/libc++ toolchain reconstruction.
