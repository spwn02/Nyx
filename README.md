# Nyx Engine

[![CI](https://github.com/spwn02/Nyx/actions/workflows/ci.yml/badge.svg)](https://github.com/spwn02/Nyx/actions/workflows/ci.yml)

Nyx is a modular C++26 3D graphics engine built around modern Vulkan, native C++ modules, and explicit visibility boundaries.

> **Status:** pre-1.0. Linux x86_64 is the currently validated platform and the engine is under active development.

## Architecture

Each directory under `Engine/Source/Runtime/` represents an engine module. Modules are split into three visibility layers:

- **Public** — exported interfaces available to dependent modules.
- **Internal** — implementation-facing module partitions and utilities that stay inside the module boundary.
- **Private** — implementation units.

The model is intentionally strict about dependency direction and visibility. It is influenced by Unreal Engine's module organization, but uses CMake's native C++ module support rather than macro-based registration.

Nyx consumes two standalone C++26 products as pinned Git submodules:

- [Miracle](https://github.com/spwn02/Miracle) — foundation and utility library.
- [Switch](https://github.com/spwn02/Switch) — reflection-driven testing framework.

External native dependencies are resolved through the pinned `vcpkg` submodule.

## Reference toolchain

Nyx does not reconstruct compiler, libc++, reflection, contracts, or module flags itself. The selected external C++26 toolchain owns those implementation details.

The current validated reference baseline is:

```text
repository: spwn02/clang-cxx26
branch:     cxx26
snapshot:   cxx26-2026.09.05
revision:   6c7ef6afbfd8456c964c7a2625b3ea2aaa7d613f
platform:   Linux x86_64
```

CI downloads that immutable snapshot, verifies its checksum and manifest, and uses its packaged CMake toolchain.

## Requirements

- Linux x86_64
- CMake 4.4 or newer
- Ninja
- Git with submodule support
- Vulkan-capable graphics stack
- the validated `clang-cxx26` reference toolchain, or another toolchain that satisfies Nyx's C++26 capability requirements

The current renderer requires Vulkan dynamic rendering and synchronization2 support.

## Clone

```bash
git clone --recurse-submodules https://github.com/spwn02/Nyx.git
cd Nyx
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build and test

Activate the reference toolchain before configuring Nyx:

```bash
source /path/to/clang-cxx26-2026.09.05/share/clang-cxx26/activate.sh
```

Development integration build:

```bash
cmake --preset development-tests --fresh
cmake --build --preset development-tests
ctest --preset development-tests
```

Production build:

```bash
cmake --preset release --fresh
cmake --build --preset release
```

The checked-in presets route both Nyx and vcpkg target builds through the same external C++26 toolchain.

## Packages and releases

Release archives are produced by CPack from the CMake install tree:

```bash
cpack --config build/release/CPackConfig.cmake
```

The canonical Linux artifact is:

```text
NyxEngine-<version>-linux-x86_64.tar.zst
```

The package includes the engine executable, the reference-toolchain runtime libraries needed by that executable, project documentation, the Nyx license, Miracle/Switch licenses, the LLVM license for bundled runtime components, and vcpkg dependency copyright notices.

GitHub release tags run the full test + Release + CPack validation path before publication. Published release assets are treated as immutable: a broken release is superseded by a new version rather than overwritten.

## Dependencies

The current vcpkg manifest includes:

- SDL3
- Vulkan headers/loader
- Vulkan Memory Allocator
- volk
- shaderc
- spdlog

Miracle and Switch are pinned source dependencies and retain their own licenses.

## Project policy

- [CHANGELOG.md](CHANGELOG.md) records user-visible changes.
- [CONTRIBUTING.md](CONTRIBUTING.md) defines build, review, and contribution requirements.
- [SECURITY.md](SECURITY.md) defines the private vulnerability-reporting path.

## License

Nyx-owned source is licensed under **GNU Lesser General Public License v3.0 only** (`LGPL-3.0-only`).

You may use, study, modify, fork, and redistribute Nyx under the LGPL terms. If you convey modified Nyx code, the LGPL copyleft requirements continue to apply to the covered Nyx portions. Applications and other independent works may use/link with Nyx under the conditions and additional permissions defined by LGPLv3.

The repository includes both the LGPLv3 text in [`LICENSE`](LICENSE) and the incorporated GPLv3 terms in [`licenses/GPL-3.0.txt`](licenses/GPL-3.0.txt).

Miracle, Switch, the reference toolchain, and vcpkg-managed dependencies remain under their respective licenses.
