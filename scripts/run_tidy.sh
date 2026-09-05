#!/usr/bin/env bash
# Run clang-tidy on an explicit list of files against the project compile DB.
#
# Usage:
#   scripts/run_tidy.sh <file> [more files...]
#   scripts/run_tidy.sh --build-dir build-release <file> ...
#
# clang-tidy is resolved like CMake does: $LLVM_PREFIX, then the Homebrew LLVM
# prefixes, then PATH. The compile DB lives in the given build dir (default
# build/, matching the debug preset).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${CORUNDUM_BUILD_DIR:-build}"

if [[ $# -eq 0 ]]; then
  echo "usage: scripts/run_tidy.sh [--build-dir <dir>] <file> [more files...]" >&2
  exit 1
fi

if [[ "$1" == "--build-dir" ]]; then
  [[ $# -ge 2 ]] || { echo "error: --build-dir needs an argument" >&2; exit 1; }
  build_dir="$2"
  shift 2
fi

resolve_clang_tidy() {
  local candidates=()
  [[ -n "${LLVM_PREFIX:-}" ]] && candidates+=("$LLVM_PREFIX/bin/clang-tidy")
  candidates+=("/opt/homebrew/opt/llvm/bin/clang-tidy" "/usr/local/opt/llvm/bin/clang-tidy")
  for candidate in "${candidates[@]}"; do
    [[ -x "$candidate" ]] && { echo "$candidate"; return; }
  done
  command -v clang-tidy
}

clang_tidy="$(resolve_clang_tidy || true)"
if [[ -z "$clang_tidy" ]]; then
  echo "error: clang-tidy not found (install Homebrew LLVM: brew install llvm)" >&2
  exit 1
fi

compile_db="$repo_root/$build_dir/compile_commands.json"
if [[ ! -f "$compile_db" ]]; then
  echo "error: no compile database at $compile_db (configure the '$build_dir' preset first)" >&2
  exit 1
fi

cd "$repo_root"
exec "$clang_tidy" -p "$build_dir" --config-file="$repo_root/.clang-tidy" "$@"