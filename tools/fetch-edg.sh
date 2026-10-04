#!/usr/bin/env bash
# Fetches the EDG sources at the commit the submodule pins, without their test suite (large, and
# not needed for the build): one commit deep, with a sparse checkout.
set -euo pipefail

cd "$(dirname "$0")/.."
path=third_party/edgcpp
url=$(git config -f .gitmodules "submodule.$path.url")
commit=$(git ls-tree HEAD "$path" | awk '{print $3}')
if [[ -z "$commit" ]]; then
  echo "fetch-edg: $path is not a submodule of this checkout" >&2
  exit 1
fi
if [[ -e "$path/src/cfe.c" ]] && [[ "$(git -C "$path" rev-parse HEAD 2>/dev/null)" == "$commit" ]]; then
  echo "fetch-edg: $path is already at $commit"
  exit 0
fi

mkdir -p "$path"
git -C "$path" init -q
git -C "$path" remote remove origin 2>/dev/null || true
git -C "$path" remote add origin "$url"
git -C "$path" fetch -q --depth 1 --filter=blob:none origin "$commit"
git -C "$path" sparse-checkout set --no-cone '/*' '!/tests/'
git -C "$path" checkout -q --detach "$commit"
echo "fetch-edg: $path at $commit"
