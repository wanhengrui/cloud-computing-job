#!/usr/bin/env bash
# Stop the local stack (keeps the image).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPOSE_FILE="$ROOT_DIR/docker/docker-compose.yml"
# MSYS_NO_PATHCONV would keep the compose path in POSIX form, which docker.exe
# cannot open; always hand compose a native path.
if command -v cygpath >/dev/null 2>&1; then COMPOSE_FILE="$(cygpath -w "$COMPOSE_FILE")"; fi

docker compose -f "$COMPOSE_FILE" down
