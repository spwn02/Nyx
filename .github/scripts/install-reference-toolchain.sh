#!/usr/bin/env bash
set -euo pipefail

: "${RUNNER_TEMP:?RUNNER_TEMP must be set by GitHub Actions}"
: "${GITHUB_ENV:?GITHUB_ENV must be set by GitHub Actions}"
: "${GITHUB_PATH:?GITHUB_PATH must be set by GitHub Actions}"
: "${GH_TOKEN:?GH_TOKEN is required to download the reference release}"

root_dir="$(git rev-parse --show-toplevel)"
# shellcheck disable=SC1091
source "$root_dir/.github/reference-toolchain.env"

download_dir="$RUNNER_TEMP/cxx26-download"
install_dir="$RUNNER_TEMP/cxx26-install"

rm -rf "$download_dir" "$install_dir"
mkdir -p "$download_dir" "$install_dir"

gh release download "$CXX26_SNAPSHOT" \
  --repo "$CXX26_REPOSITORY" \
  --pattern "${CXX26_ASSET}.tar.zst" \
  --pattern "${CXX26_ASSET}.manifest.json" \
  --pattern "${CXX26_ASSET}.SHA256SUMS" \
  --dir "$download_dir"

(
  cd "$download_dir"
  sha256sum --check "${CXX26_ASSET}.SHA256SUMS"
)

python3 - \
  "$download_dir/${CXX26_ASSET}.manifest.json" \
  "$CXX26_SNAPSHOT" \
  "$CXX26_REVISION" \
  "$CXX26_BRANCH" <<'PY'
import json
from pathlib import Path
import sys

manifest_path = Path(sys.argv[1])
expected_snapshot = sys.argv[2]
expected_revision = sys.argv[3]
expected_branch = sys.argv[4]

manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

assert manifest["snapshot"] == expected_snapshot
assert manifest["source"]["revision"] == expected_revision
assert manifest["source"]["repository"] == "https://github.com/spwn02/clang-cxx26"
assert manifest["source"]["branch"] == expected_branch
assert manifest["compiler"]["name"] == "clang-cxx26"
assert manifest["platform"]["os"] == "linux"
assert manifest["platform"]["architecture"] == "x86_64"
assert manifest["cmake"]["minimumConsumerVersion"] == "4.4"
assert manifest["cmake"]["toolchainFile"] == "share/clang-cxx26/toolchain.cmake"
PY

tar \
  --use-compress-program=unzstd \
  -xf "$download_dir/${CXX26_ASSET}.tar.zst" \
  -C "$install_dir"

toolchain_root="$install_dir/clang-${CXX26_SNAPSHOT}"

test -x "$toolchain_root/bin/clang"
test -x "$toolchain_root/bin/clang++"
test -x "$toolchain_root/bin/clangd"
test -f "$toolchain_root/share/clang-cxx26/toolchain.cmake"
test -f "$toolchain_root/share/clang-cxx26/activate.sh"

cmp \
  "$download_dir/${CXX26_ASSET}.manifest.json" \
  "$toolchain_root/share/clang-cxx26/manifest.json"

# shellcheck disable=SC1090
source "$toolchain_root/share/clang-cxx26/activate.sh"

{
  echo "CXX26_REPOSITORY=$CXX26_REPOSITORY"
  echo "CXX26_BRANCH=$CXX26_BRANCH"
  echo "CXX26_SNAPSHOT=$CXX26_SNAPSHOT"
  echo "CXX26_REVISION=$CXX26_REVISION"
  echo "CXX26_ASSET=$CXX26_ASSET"
  echo "CXX26_TOOLCHAIN_ROOT=$CXX26_TOOLCHAIN_ROOT"
  echo "CXX26_CMAKE_TOOLCHAIN_FILE=$CXX26_CMAKE_TOOLCHAIN_FILE"
  echo "CC=$CC"
  echo "CXX=$CXX"
  echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
} >> "$GITHUB_ENV"

echo "$CXX26_TOOLCHAIN_ROOT/bin" >> "$GITHUB_PATH"
