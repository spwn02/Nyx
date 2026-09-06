#!/usr/bin/env bash
set -euo pipefail

: "${CXX26_CMAKE_TOOLCHAIN_FILE:?Activate the C++26 reference toolchain first}"

root_dir="$(git rev-parse --show-toplevel)"
cd "$root_dir"

if [[ ! -x vcpkg/vcpkg ]]; then
  ./vcpkg/bootstrap-vcpkg.sh -disableMetrics
fi

project_version="$({
  sed -n \
    's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' \
    CMakeLists.txt || true
} | head -n1)"

manifest_version="$(
  python3 - <<'PY'
import json
from pathlib import Path

print(json.loads(Path("vcpkg.json").read_text(encoding="utf-8"))["version-string"])
PY
)"

if [[ -z "$project_version" || "$manifest_version" != "$project_version" ]]; then
  echo "::error::CMake project version '$project_version' and vcpkg manifest version '$manifest_version' differ"
  exit 1
fi

cmake --preset development-tests --fresh
cmake --build --preset development-tests
ctest --preset development-tests

release_configure=(cmake --preset release --fresh)
if [[ -n "${NYX_PACKAGE_VERSION:-}" ]]; then
  release_configure+=("-DNYX_PACKAGE_VERSION=${NYX_PACKAGE_VERSION}")
fi

"${release_configure[@]}"
cmake --build --preset release

rm -rf build/release/package
cpack --config build/release/CPackConfig.cmake

mapfile -t packages < <(
  find build/release/package \
    -maxdepth 1 \
    -type f \
    -name 'NyxEngine-*-linux-x86_64.tar.zst' \
    -print |
    sort
)

if [[ "${#packages[@]}" -ne 1 ]]; then
  printf 'Expected exactly one Nyx release archive, found %d:\n' "${#packages[@]}" >&2
  printf '  %s\n' "${packages[@]:-<none>}" >&2
  exit 1
fi

package="${packages[0]}"
checksum="${package}.sha256"

test -s "$package"
test -s "$checksum"

(
  cd "$(dirname "$package")"
  sha256sum --check "$(basename "$checksum")"
)

package_stem="$(basename "$package" .tar.zst)"
manifest_file="$(mktemp)"
extract_dir="$(mktemp -d)"
ldd_log="$(mktemp)"

cleanup() {
  rm -f "$manifest_file" "$ldd_log"
  rm -rf "$extract_dir"
}
trap cleanup EXIT

tar --use-compress-program=unzstd -tf "$package" > "$manifest_file"

required_entries=(
  "$package_stem/bin/NyxEngine"
  "$package_stem/share/doc/NyxEngine/README.md"
  "$package_stem/share/doc/NyxEngine/CHANGELOG.md"
  "$package_stem/share/doc/NyxEngine/CONTRIBUTING.md"
  "$package_stem/share/doc/NyxEngine/SECURITY.md"
  "$package_stem/share/licenses/NyxEngine/LICENSE"
  "$package_stem/share/licenses/NyxEngine/GPL-3.0.txt"
  "$package_stem/share/licenses/NyxEngine/third-party/clang-cxx26/LLVM-LICENSE.txt"
  "$package_stem/share/licenses/NyxEngine/third-party/Miracle/LICENSE"
  "$package_stem/share/licenses/NyxEngine/third-party/Switch/LICENSE"
)

for entry in "${required_entries[@]}"; do
  if ! grep -Fxq "$entry" "$manifest_file"; then
    echo "::error::release package is missing '$entry'"
    exit 1
  fi
done

if ! grep -Eq \
  "^${package_stem}/share/licenses/NyxEngine/third-party/vcpkg/.+/copyright$" \
  "$manifest_file"; then
  echo "::error::release package contains no vcpkg third-party copyright notices"
  exit 1
fi

if ! grep -Eq \
  "^${package_stem}/lib/libc\\+\\+\\.so([.0-9]*)?$" \
  "$manifest_file"; then
  echo "::error::release package does not contain the reference libc++ runtime"
  exit 1
fi

tar \
  --use-compress-program=unzstd \
  -xf "$package" \
  -C "$extract_dir"

engine="$extract_dir/$package_stem/bin/NyxEngine"
test -x "$engine"

if env -u LD_LIBRARY_PATH ldd "$engine" > "$ldd_log" 2>&1; then
  if grep -Fq "not found" "$ldd_log"; then
    cat "$ldd_log"
    echo "::error::release binary has unresolved dynamic dependencies"
    exit 1
  fi

  forbidden_paths=(
    "$root_dir/build"
    "$root_dir/vcpkg_installed"
    "${GITHUB_WORKSPACE:-}"
    "${RUNNER_TEMP:-}"
  )

  for forbidden in "${forbidden_paths[@]}"; do
    [[ -z "$forbidden" ]] && continue

    if grep -Fq "$forbidden" "$ldd_log"; then
      cat "$ldd_log"
      echo "::error::release binary resolves a dependency from '$forbidden'"
      exit 1
    fi
  done
else
  if ! grep -Fq "not a dynamic executable" "$ldd_log"; then
    cat "$ldd_log"
    echo "::error::unable to inspect release binary dependencies with ldd"
    exit 1
  fi
fi

printf 'Validated release package: %s\n' "$package"
printf 'Checksum sidecar: %s\n' "$checksum"
