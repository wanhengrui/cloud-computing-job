#!/usr/bin/env bash
# Run this ON a CVM (Ubuntu 24.04). It starts exactly one ring node with
# --network host so the peers can reach 19001 directly, and feeds it the same
# node inventory that every other host uses.
#
#   bash scripts/deploy-node.sh node-b
#
# Prerequisites on the host:
#   - docker installed
#   - the image exists:  docker images | grep lab1b-dht-microservices
#     (build it with `bash scripts/up.sh` once, or `docker load` the tar)
#   - security group allows 19001 from the other two private IPs only
set -euo pipefail

SELF_NAME="${1:-}"
case "$SELF_NAME" in
  node-a | node-b | node-c) ;;
  *)
    echo "usage: bash scripts/deploy-node.sh <node-a|node-b|node-c>" >&2
    exit 2
    ;;
esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLOUD_DIR="$ROOT_DIR/config/cloud"
INVENTORY="$CLOUD_DIR/node-inventory.env"
IMAGE="${IMAGE:-lab1b-dht-microservices:local}"
LETTER="${SELF_NAME#node-}"

if [ ! -f "$INVENTORY" ]; then
  echo "==> no cloud config yet, generating it"
  bash "$ROOT_DIR/scripts/gen-cloud-config.sh"
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "image $IMAGE not found on this host." >&2
  echo "build it here:   docker build -f docker/Dockerfile -t $IMAGE ." >&2
  echo "or import it:    docker load -i lab1b-dht-microservices.tar.gz" >&2
  exit 2
fi

# Every host must agree on the ring, so refuse to start if this host's port is
# not the one the inventory advertises.
echo "==> inventory as seen by this host:"
grep -E '^NODE_[ABC]_' "$INVENTORY" | sed 's/^/    /'

echo "==> starting $SELF_NAME (config dht-node-$LETTER.yaml)"
docker rm -f dht >/dev/null 2>&1 || true
# shellcheck disable=SC2046
docker run -d --name dht --restart unless-stopped --network host \
  $(grep -E '^NODE_[ABC]_' "$INVENTORY" | sed 's/^/-e /') \
  -e SELF="$SELF_NAME" \
  -v "$CLOUD_DIR:/app/config/cloud:ro" \
  "$IMAGE" /app/dht_server --config="/app/config/cloud/dht-node-$LETTER.yaml"

sleep 2
echo "==> recent log:"
docker logs --tail 20 dht 2>&1 | sed 's/^/    /' || true

cat <<EOF

next checks on this host:
    docker ps --filter name=dht
    ss -ltnp | grep 19001
    docker exec dht /app/dht_cli --op=put --key=smoke:$(hostname) --value=ok --entry=$SELF_NAME \\
        --client_config=/app/config/cloud/dht-client.yaml
EOF
