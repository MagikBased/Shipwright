#!/usr/bin/env bash
set -euo pipefail

version="${1:-}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+-rc\.[0-9]+$ ]]; then
    echo "Usage: $0 MAJOR.MINOR.PATCH-rc.NUMBER" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_dir="$(cd "$script_dir/.." && pwd)"
repository_root="$(cd "$service_dir/../.." && pwd)"
output_dir="$service_dir/var/releases/$version"
image="jp-assist-learning-platform:$version"
syft_command="${JP_ASSIST_SYFT:-$(command -v syft || true)}"
grype_command="${JP_ASSIST_GRYPE:-$(command -v grype || true)}"

if [[ -n "$(git -C "$repository_root" status --short)" ]]; then
    echo "Release artifacts must be built from a clean worktree." >&2
    exit 1
fi
if [[ -e "$output_dir" ]]; then
    echo "Release output already exists: $output_dir" >&2
    exit 1
fi
if [[ -z "$syft_command" || -z "$grype_command" ]]; then
    echo "Syft and Grype are required; set JP_ASSIST_SYFT and JP_ASSIST_GRYPE if needed." >&2
    exit 1
fi

mkdir -p "$output_dir"
docker build \
    --file "$service_dir/Dockerfile" \
    --tag "$image" \
    "$repository_root"
"$script_dir/verify_release_image.sh" "$image"
docker save --output "$output_dir/jp-assist-learning-platform-$version.tar" "$image"
git -C "$repository_root" archive \
    --format=tar.gz \
    --prefix="OoT-JP-Assist-$version/" \
    --output="$output_dir/OoT-JP-Assist-$version.tar.gz" \
    HEAD
"$syft_command" "$image" \
    -o "spdx-json=$output_dir/jp-assist-learning-platform-$version.spdx.json"
"$grype_command" \
    "sbom:$output_dir/jp-assist-learning-platform-$version.spdx.json" \
    --vex "$service_dir/security/openvex.json" \
    --fail-on high \
    --output "json=$output_dir/jp-assist-learning-platform-$version.vulnerabilities.json" \
    --output table
cp "$repository_root/docs/releases/$version.md" "$output_dir/RELEASE_NOTES.md"

commit="$(git -C "$repository_root" rev-parse HEAD)"
image_id="$(docker image inspect "$image" --format '{{.Id}}')"
generated_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
python3 - "$output_dir/manifest.json" "$version" "$commit" "$image_id" "$generated_at" <<'PY'
import json
import sys
from pathlib import Path

path, version, commit, image_id, generated_at = sys.argv[1:]
Path(path).write_text(json.dumps({
    "version": version,
    "commit": commit,
    "image": "jp-assist-learning-platform",
    "imageId": image_id,
    "apiVersion": "v1",
    "databaseSchema": 9,
    "generatedAt": generated_at,
}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY

(
    cd "$output_dir"
    sha256sum \
        "OoT-JP-Assist-$version.tar.gz" \
        "jp-assist-learning-platform-$version.tar" \
        "jp-assist-learning-platform-$version.spdx.json" \
        "jp-assist-learning-platform-$version.vulnerabilities.json" \
        RELEASE_NOTES.md manifest.json > SHA256SUMS
    sha256sum --check --strict SHA256SUMS
)

echo "Release candidate artifacts are ready: $output_dir"
