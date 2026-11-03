#!/usr/bin/env bash
# Bring the local three-node ring up (pricing + trip + dht-a/b/c).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPOSE_FILE="$ROOT_DIR/docker/docker-compose.yml"
# MSYS_NO_PATHCONV stops /app/... being rewritten, but it also stops the
# compose file path from being converted, so hand docker a Windows path.
if command -v cygpath >/dev/null 2>&1; then COMPOSE_FILE="$(cygpath -w "$COMPOSE_FILE")"; fi
IMAGE="lab1b-dht-microservices:local"

echo "==> building image $IMAGE"
docker build -f "$ROOT_DIR/docker/Dockerfile" -t "$IMAGE" "$ROOT_DIR"

echo "==> starting stack"
docker compose -f "$COMPOSE_FILE" up -d

echo "==> containers"
docker compose -f "$COMPOSE_FILE" ps
