#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_name="jp-assist-rehearsal-$$"
export JP_ASSIST_COMPOSE_PROJECT="$project_name"
export JP_ASSIST_CONFIRM_DESTROY="$project_name"

cleanup() {
    "$script_dir/staging.sh" destroy >/dev/null 2>&1 || true
}
trap cleanup EXIT

"$script_dir/staging.sh" up
"$script_dir/staging.sh" backup
"$script_dir/staging.sh" restore-drill
"$script_dir/staging.sh" smoke
"$script_dir/staging.sh" destroy

remaining_containers="$(docker ps --all --quiet --filter "label=com.docker.compose.project=$project_name")"
remaining_networks="$(docker network ls --quiet --filter "label=com.docker.compose.project=$project_name")"
remaining_volumes="$(docker volume ls --quiet --filter "label=com.docker.compose.project=$project_name")"
if [[ -n "$remaining_containers$remaining_networks$remaining_volumes" ]]; then
    echo "Disposable staging resources remain after cleanup." >&2
    exit 1
fi

trap - EXIT
echo "Clean staging rehearsal passed and removed its disposable runtime state."
