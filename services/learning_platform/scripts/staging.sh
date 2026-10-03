#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_dir="$(cd "$script_dir/.." && pwd)"
env_file="$service_dir/.env.staging"
project_name="${JP_ASSIST_COMPOSE_PROJECT:-jp-assist-staging}"
podman_compat=false

if docker compose version >/dev/null 2>&1; then
    compose=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
    # Mint's packaged Compose v1 can be shadowed by incompatible user-site
    # requests/urllib3 packages. Podman's Docker compatibility socket is also
    # per-user rather than /var/run/docker.sock.
    compose=(env PYTHONNOUSERSITE=1)
    docker_version="$(docker --version 2>&1 || true)"
    if grep -qi podman <<<"$docker_version" && command -v podman >/dev/null 2>&1; then
        podman_socket="$(podman info --format '{{.Host.RemoteSocket.Path}}')"
        compose+=("DOCKER_HOST=unix://$podman_socket")
        podman_compat=true
    fi
    compose+=(docker-compose)
else
    echo "Docker Compose is required (docker compose or docker-compose)." >&2
    exit 1
fi

run_compose() {
    "${compose[@]}" \
        --project-name "$project_name" \
        --project-directory "$service_dir" \
        --env-file "$env_file" \
        -f "$service_dir/compose.yaml" "$@"
}

prepare_legacy_podman_network() {
    $podman_compat || return 0
    [[ "$(podman version --format '{{.Client.Version}}')" == 3.* ]] || return 0

    local network_name="${project_name}_default"
    local cni_file="${XDG_CONFIG_HOME:-$HOME/.config}/cni/net.d/${network_name}.conflist"
    if ! podman network exists "$network_name"; then
        podman network create \
            --label com.docker.compose.network=default \
            --label "com.docker.compose.project=$project_name" \
            --label com.docker.compose.version=1.29.2 \
            "$network_name" >/dev/null
    fi
    # Podman 3 can emit CNI 1.0 even when Ubuntu 22.04's firewall plugin only
    # implements 0.4. This changes only the network owned by this project.
    if [[ -f "$cni_file" ]] && grep -q '"cniVersion": "1.0.0"' "$cni_file"; then
        sed -i 's/"cniVersion": "1.0.0"/"cniVersion": "0.4.0"/' "$cni_file"
    fi
}

smoke() {
    python3 "$script_dir/deployment_smoke.py" \
        --production --insecure https://localhost:8443
}

wait_until_ready() {
    local attempt
    for attempt in $(seq 1 60); do
        if smoke >/dev/null 2>&1; then
            smoke
            return
        fi
        sleep 2
    done
    echo "Staging did not become ready within 120 seconds." >&2
    run_compose ps >&2 || true
    run_compose logs --tail=100 app caddy >&2 || true
    exit 1
}

case "${1:-up}" in
    up)
        run_compose config >/dev/null
        prepare_legacy_podman_network
        run_compose up -d --build
        wait_until_ready
        cat <<'EOF'
JP Assist staging is ready:
  Site:       https://localhost:8443
  Mail:       http://127.0.0.1:8025
  Metrics:    http://127.0.0.1:9090
  Dashboard:  http://127.0.0.1:3000

The local Caddy certificate may require a browser trust exception.
EOF
        ;;
    down)
        run_compose down
        ;;
    status)
        run_compose ps
        ;;
    logs)
        run_compose logs --tail=200 -f "${@:2}"
        ;;
    smoke)
        smoke
        ;;
    backup)
        run_compose exec -T app python -m learning_platform.manage backup-scheduled
        ;;
    restore-drill)
        run_compose exec -T app python -m learning_platform.manage restore-drill
        ;;
    config)
        run_compose config
        ;;
    *)
        echo "Usage: $0 {up|down|status|logs [service...]|smoke|backup|restore-drill|config}" >&2
        exit 2
        ;;
esac
