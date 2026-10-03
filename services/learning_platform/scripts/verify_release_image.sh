#!/usr/bin/env bash
set -euo pipefail

image_name="${1:-jp-assist-learning-platform:test}"
host_port="${JP_ASSIST_RELEASE_TEST_PORT:-18766}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd -- "$script_dir/.." && pwd)"
container_name="jp-assist-release-test-$$"

cleanup() {
  docker rm --force "$container_name" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker run --rm \
  --volume "$service_root:/workspace:ro" \
  "$image_name" \
  python -m unittest discover -s /workspace/tests -v

docker run --detach \
  --name "$container_name" \
  --publish "127.0.0.1:${host_port}:8766" \
  --env JP_ASSIST_ENV=production \
  --env JP_ASSIST_COOKIE_SECURE=1 \
  --env JP_ASSIST_ALLOWED_HOSTS=127.0.0.1,localhost \
  --env JP_ASSIST_PUBLIC_BASE_URL=https://localhost \
  --env JP_ASSIST_MAIL_TRANSPORT=smtp \
  --env JP_ASSIST_SMTP_HOST=mail.invalid \
  "$image_name" >/dev/null

service_url="http://127.0.0.1:${host_port}"
ready=0
for attempt in $(seq 1 50); do
  if curl --fail --silent --show-error "$service_url/readyz" >/dev/null 2>&1; then
    ready=1
    break
  fi
  if ! docker inspect "$container_name" >/dev/null 2>&1; then
    echo "Release-image container exited before becoming ready" >&2
    docker logs "$container_name" >&2
    exit 1
  fi
  sleep 0.2
done

if [[ "$ready" -ne 1 ]]; then
  echo "Release-image verification timed out" >&2
  docker logs "$container_name" >&2
  exit 1
fi

python3 "$script_dir/deployment_smoke.py" --production "$service_url"
