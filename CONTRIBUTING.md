# Contributing to Nyx

Nyx is a pre-1.0 C++26 graphics engine. Contributions are welcome when they preserve the project's architectural direction, module boundaries, and toolchain contract.

## Before contributing

Read:

- `README.md`
- `CHANGELOG.md`
- `LICENSE`
- `SECURITY.md` for vulnerability reports

Do not open a public issue for a security vulnerability.

## Toolchain

The authoritative reference baseline is recorded in:

```text
.github/reference-toolchain.env
```

Before submitting a change, reproduce it with that immutable toolchain unless the contribution is specifically intended to advance the reference baseline.

Activate the installed snapshot before configuring:

```bash
source /path/to/reference/share/clang-cxx26/activate.sh
```

Nyx source and CMake files must not reconstruct reference-compiler flags, libc++ paths, reflection switches, contract switches, or standard-module paths. Those belong to the selected external toolchain.

## Checkout

```bash
git clone --recurse-submodules https://github.com/spwn02/Nyx.git
cd Nyx
```

Keep submodule changes intentional. A Nyx commit that changes Miracle, Switch, or vcpkg must pin an exact tested commit.

## Required validation

For normal C++ or build-system changes:

```bash
cmake --preset development-tests --fresh
cmake --build --preset development-tests
ctest --preset development-tests

cmake --preset release --fresh
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake
```

The exact CI/release validation path can also be run with:

```bash
make ci
```

Do not make a release package depend on files from `build/`, `vcpkg_installed/`, or a developer-specific toolchain location.

## Code style

- Use modern C++26 language and library facilities where they improve the design.
- Prefer standard algorithms/ranges and explicit ownership.
- Preserve the Public/Internal/Private module visibility model.
- Keep public module dependencies minimal and intentional.
- Do not introduce preprocessor registration machinery where C++ language facilities can express the same model.
- Run the repository's clang-format configuration on touched C++ sources.
- Keep warnings and diagnostics actionable rather than suppressing them globally.

## Tests

Behavioral changes should include or update Switch-based tests where practical. Bug fixes should include a regression test when the failure can be represented without disproportionate infrastructure.

Tests must remain deterministic. Avoid making correctness depend on wall-clock timing, thread scheduling, machine-specific paths, or external network access.

## Changelog

Update `CHANGELOG.md` for user-visible behavior, build/release contract changes, or compatibility changes. Pure refactors and test-only changes normally do not need an entry.

## Pull requests

Keep a pull request focused enough that its architecture and regression surface can be reviewed coherently. Describe:

- what changed;
- why the change belongs in Nyx;
- the relevant module/dependency consequences;
- the validation commands you ran.

## Contribution license

Nyx is licensed under `LGPL-3.0-only`. By intentionally submitting a contribution for inclusion in Nyx, you certify that you have the right to submit it and agree that the contribution may be distributed as part of Nyx under `LGPL-3.0-only`.

Do not submit code whose license is incompatible with LGPLv3 or whose copyright/patent terms you do not have authority to contribute.
