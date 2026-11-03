#!/usr/bin/env bash
# Start pricing-service and trip-service on the host that faces the client
# (in the two-person plan that is node-b, the machine 万恒睿 opened).
# The ring itself is started separately on each CVM with deploy-node.sh.
#
#   bash scripts/deploy-trip-host.sh
#
# Precondition: config/cloud/ exists (bash scripts/gen-cloud-config.sh).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLOUD_DIR="$ROOT_DIR/config/cloud"
IMAGE="${IMAGE:-lab1b-dht-microservices:local}"

if [ ! -f "$CLOUD_DIR/trip-service.yaml" ]; then
  echo "missing $CLOUD_DIR/trip-service.yaml - run scripts/gen-cloud-config.sh first" >&2
  echo "(it needs env/node-inventory.env with the three private IPs)" >&2
  exit 2
fi

echo "==> trip-service will reach the ring at:"
grep -A3 'name: trpc.dht' "$CLOUD_DIR/trip-service.yaml" | grep target | sed 's/^/    /'

for name in pricing trip; do
  docker rm -f "$name" >/dev/null 2>&1 || true
done

echo "==> starting pricing-service (config/pricing-service.yaml, port 8000)"
docker run -d --name pricing --restart unless-stopped --network host \
  -v "$ROOT_DIR/config:/app/config:ro" \
  "$IMAGE" /app/pricing_server --config=/app/config/pricing-service.yaml

echo "==> starting trip-service (config/cloud/trip-service.yaml, port 8000)"
docker run -d --name trip --restart unless-stopped --network host \
  -e DHT_ENTRY="${DHT_ENTRY:-node-a}" \
  -v "$CLOUD_DIR:/app/config/cloud:ro" \
  "$IMAGE" /app/trip_server --config=/app/config/cloud/trip-service.yaml

sleep 2
docker ps --filter name=pricing --filter name=trip

cat <<EOF

now prove the client contract still behaves (1A acceptance):
    docker exec trip /app/ride_client --op=create \\
        --client_config=/app/config/cloud/ride-client.yaml
    docker exec trip /app/ride_client --op=get \\
        --client_config=/app/config/cloud/ride-client.yaml --trip_id=trip-000001

expect: fare 41.04 / 17.20, status WAITING, and a trip read back from the ring.
EOF
