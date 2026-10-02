#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
app_dir=${JPASSIST_APP_DIR:-"$repo_dir/build-cmake/soh"}
timeout_seconds=${JPASSIST_SMOKE_TIMEOUT:-120}
report="$app_dir/jp_assist_smoke_results.json"
console_log="$app_dir/jp_assist_smoke_console.log"

if [[ ! -x "$app_dir/soh.elf" ]]; then
    echo "JP Assist smoke runner: missing executable: $app_dir/soh.elf" >&2
    exit 2
fi

# The manifest is source data rather than a compiled asset. Keep the app copy
# in sync so a smoke run always exercises the scenarios from this checkout.
mkdir -p -- "$app_dir/jp_assist"
cp -- "$script_dir/test_scenarios.json" "$app_dir/jp_assist/test_scenarios.json"

echo "JP Assist smoke runner: launching $app_dir/soh.elf"
echo "The game window will close automatically when the suite finishes."

# Never display a previous run's lastSuite when this process fails before it
# can write a new summary.
rm -f -- "$report"

cd -- "$app_dir"
set +e
timeout --foreground "${timeout_seconds}s" env JPASSIST_AUTORUN_SMOKE=1 SHIP_DISABLE_CRASH_DIALOG=1 ./soh.elf >"$console_log" 2>&1
status=$?
set -e

if [[ $status -eq 124 ]]; then
    echo "JP Assist smoke runner: timed out after ${timeout_seconds}s" >&2
    exit 124
fi

if [[ -f "$report" ]]; then
    if command -v jq >/dev/null 2>&1; then
        jq '.lastSuite' "$report"
    else
        echo "Report: $report"
    fi
fi

echo "Console log: $console_log"

if [[ $status -eq 0 ]]; then
    echo "JP Assist smoke runner: PASS"
else
    echo "JP Assist smoke runner: FAIL (exit $status)" >&2
fi

exit "$status"
