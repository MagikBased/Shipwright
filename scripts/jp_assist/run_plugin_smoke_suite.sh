#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
app_dir=${JPASSIST_APP_DIR:-"$repo_dir/build-cmake/soh"}
plugin_package=${JPASSIST_PLUGIN_PACKAGE:-"$repo_dir/build-cmake/mods/jp_assist_plugin/jp-assist.o2r"}
config_file="$app_dir/shipofharkinian.json"
mods_dir="$app_dir/mods"
installed_plugin="$mods_dir/jp-assist.o2r"

if [[ ! -f "$plugin_package" ]]; then
    echo "JP Assist plugin smoke runner: missing package: $plugin_package" >&2
    echo "Build it with: cmake --build build-cmake --target jpassist_plugin_package" >&2
    exit 2
fi
if [[ ! -f "$config_file" ]]; then
    echo "JP Assist plugin smoke runner: missing config: $config_file" >&2
    exit 2
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "JP Assist plugin smoke runner: jq is required to stage the temporary runtime settings" >&2
    exit 2
fi

staging_dir=$(mktemp -d "${TMPDIR:-/tmp}/jpassist-plugin-smoke.XXXXXX")
config_backup="$staging_dir/shipofharkinian.json"
plugin_backup="$staging_dir/jp-assist.o2r"
had_plugin=0

cleanup() {
    local status=$?
    cp -- "$config_backup" "$config_file"
    if [[ $had_plugin -eq 1 ]]; then
        cp -- "$plugin_backup" "$installed_plugin"
    else
        rm -f -- "$installed_plugin"
    fi
    rm -rf -- "$staging_dir"
    exit "$status"
}
trap cleanup EXIT INT TERM

mkdir -p -- "$mods_dir"
cp -- "$config_file" "$config_backup"
if [[ -f "$installed_plugin" ]]; then
    cp -- "$installed_plugin" "$plugin_backup"
    had_plugin=1
fi
cp -- "$plugin_package" "$installed_plugin"

temporary_config="$staging_dir/plugin-enabled.json"
jq '
    .CVars.gEnhancements.JPAssist.Enabled = 1
    | .CVars.gEnhancements.StudyMods["jp-assist"].Enabled = 1
' "$config_file" >"$temporary_config"
mv -- "$temporary_config" "$config_file"

echo "JP Assist plugin smoke runner: temporarily installed $plugin_package"
echo "Runtime settings and any previous installed package will be restored after the run."
JPASSIST_PLUGIN_SMOKE=1 "$script_dir/run_smoke_suite.sh"
