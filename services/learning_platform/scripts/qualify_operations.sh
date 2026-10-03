#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_dir="$(cd "$script_dir/.." && pwd)"
repository_root="$(cd "$service_dir/../.." && pwd)"
output_dir="$service_dir/var/qualification"
python_command="${JP_ASSIST_TEST_PYTHON:-}"

if [[ -z "$python_command" && -x "$repository_root/venv/learning-platform/bin/python" ]]; then
    python_command="$repository_root/venv/learning-platform/bin/python"
elif [[ -z "$python_command" ]]; then
    python_command="python3"
fi

mkdir -p "$output_dir"
export PYTHONPATH="$service_dir${PYTHONPATH:+:$PYTHONPATH}"

"$python_command" "$script_dir/load_qualification.py" \
    --output "$output_dir/load.json"
"$python_command" "$script_dir/migration_rehearsal.py" \
    --output "$output_dir/migration.json"

echo "Operational qualification passed. Reports: $output_dir"
