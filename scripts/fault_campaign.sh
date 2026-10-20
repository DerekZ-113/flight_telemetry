#!/usr/bin/env bash
# The fault-injection campaign of record (REQ-TEST-005, REQ-TEST-006; FTS-FI-001 Section 10,
# FTS-TP-001 TC-036): on a clean tree, build, run every cell at its stated count into
# build/campaign/<commit>/, then rewrite the report's generated blocks and figures from that record.
#
#   scripts/fault_campaign.sh [--preview] [--config <file>]
#
# --preview runs on a tree with changes and records the tree that ran instead of a commit: HEAD
# plus the tree's hash, which `git rev-parse <commit>^{tree}` prints once the changes are
# committed. A campaign of record never uses it. The Python side runs as $PYTHON, by default
# .venv/bin/python.
set -euo pipefail

cd "$(dirname "$0")/.."
preview=0
config=config/telemetry_config.yaml
while [[ $# -gt 0 ]]; do
  case "$1" in
    --preview) preview=1; shift ;;
    --config) config="$2"; shift 2 ;;
    *) echo "usage: scripts/fault_campaign.sh [--preview] [--config <file>]" >&2; exit 1 ;;
  esac
done

if [[ -z "$(git status --porcelain)" ]]; then
  commit=$(git rev-parse HEAD)
  label=$commit
elif [[ $preview -eq 1 ]]; then
  # The tree as git will name it once committed: every file, tracked or new, staged into a
  # temporary index, so the real index is never touched.
  scratch=$(mktemp -d)
  tree=$(GIT_INDEX_FILE="$scratch/index" sh -c 'git read-tree HEAD && git add -A && git write-tree')
  rm -f "$scratch/index"
  rmdir "$scratch"
  commit="$(git rev-parse --short HEAD) + changes (tree $tree)"
  label=tree-$tree
else
  echo "error: the working tree has changes; the campaign of record runs on a commit (--preview runs anyway)" >&2
  git status --short >&2
  exit 1
fi

case "$(uname -s)" in
  Darwin)
    os="$(sw_vers -productName) $(sw_vers -productVersion)"
    cpu=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "CPU unknown") ;;
  Linux)
    os=$( (. /etc/os-release && echo "$PRETTY_NAME") 2>/dev/null || echo Linux)
    cpu=$( (grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //') 2>/dev/null || true)
    cpu=${cpu:-CPU unknown} ;;
  *)
    os=$(uname -s)
    cpu="CPU unknown" ;;
esac
platform="$os ($(uname -s) $(uname -r), $(uname -m)), $cpu"

cmake -S . -B build > /dev/null
cmake --build build --parallel --target fault_campaign
out=build/campaign/$label
mkdir -p "$out"
build/fault_campaign --out "$out" --config "$config" --commit "$commit" --platform "$platform"
"${PYTHON:-.venv/bin/python}" -m analysis.fault_campaign "$out" --report docs/fault_injection_report.md \
  --figures docs/img
echo "Record: $out"
