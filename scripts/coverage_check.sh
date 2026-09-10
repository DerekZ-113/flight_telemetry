#!/usr/bin/env bash
# Enforce the branch coverage thresholds in docs/test_plan.md section 6.3
# (REQ-TEST-003). Reads an lcov .info file that has already been filtered
# (no _deps, no tests, no main.cpp) and checks each scope separately.
#
# Usage: scripts/coverage_check.sh coverage/lcov.info
#
# Exit 1 if any scope with files falls below its threshold. A scope with
# no files yet (timing, logging, transport, replay before they exist)
# passes with a note; an empty directory is not a coverage failure.
set -euo pipefail

info="${1:?usage: coverage_check.sh <lcov.info>}"
# Same strictness relaxations as the CI capture step: lcov 2.x otherwise
# aborts --extract and --summary on consistency checks that do not affect
# the percentages (see .github/workflows/ci.yml).
rc="--rc branch_coverage=1 --ignore-errors inconsistent,unused,empty,mismatch,format,unsupported"

# scope pattern | threshold | label
scopes=(
  "*/src/processing/*|90|processing"
  "*/src/timing/*|80|timing"
  "*/src/logging/*|80|logging"
  "*/src/transport/*|80|transport"
  "*/src/replay/*|80|replay"
  "*/src/drivers/*|70|drivers"
)

branch_pct() {
  # lcov --summary prints "branches......: 92.3% (24 of 26 branches)";
  # print just the number, or "none" when the subset has no branch data.
  local file="$1"
  # `|| true` inside the pipeline: an extract with no matching files
  # leaves an empty .info that lcov refuses to summarize. With pipefail
  # and set -e that non-zero status would abort the whole script instead
  # of reporting "none" for the scope.
  { lcov $rc --summary "$file" 2>/dev/null || true; } \
    | awk '/branches/ { gsub("%","",$2); if ($2 ~ /^[0-9.]+$/) { print $2; found=1 } } END { if (!found) print "none" }'
}

fail=0
printf '%-12s %8s %10s  %s\n' "scope" "branch%" "threshold" "result"
for entry in "${scopes[@]}"; do
  IFS='|' read -r pattern threshold label <<< "$entry"
  tmp="$(mktemp)"
  lcov $rc --extract "$info" "$pattern" -o "$tmp" >/dev/null 2>&1 || true
  pct="$(branch_pct "$tmp")"
  rm -f "$tmp"
  if [[ "$pct" == "none" ]]; then
    printf '%-12s %8s %9s%%  %s\n' "$label" "-" "$threshold" "no files"
    continue
  fi
  if awk -v p="$pct" -v t="$threshold" 'BEGIN { exit !(p+0 >= t+0) }'; then
    printf '%-12s %7s%% %9s%%  %s\n' "$label" "$pct" "$threshold" "ok"
  else
    printf '%-12s %7s%% %9s%%  %s\n' "$label" "$pct" "$threshold" "BELOW THRESHOLD"
    fail=1
  fi
done

overall="$(branch_pct "$info")"
if awk -v p="$overall" 'BEGIN { exit !(p+0 >= 80) }'; then
  printf '%-12s %7s%% %9s%%  %s\n' "overall" "$overall" "80" "ok"
else
  printf '%-12s %7s%% %9s%%  %s\n' "overall" "$overall" "80" "BELOW THRESHOLD"
  fail=1
fi

exit $fail
