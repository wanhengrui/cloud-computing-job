#!/usr/bin/env bash
# Fault observation required by the README ("stop one responsible node and
# record what happens, honestly"). The static ring has no replication, so the
# expected answer is: keys owned elsewhere keep working, keys whose route needs
# the dead node fail, and the failure is a plain RPC error, not a timeout storm.
set -uo pipefail

export MSYS_NO_PATHCONV=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-lab1b-dht-microservices:local}"
NETWORK="${NETWORK:-lab1b_ridenet}"
COMPOSE_FILE="$ROOT_DIR/docker/docker-compose.yml"
# MSYS_NO_PATHCONV stops /app/... being rewritten, but it also stops the
# compose file path from being converted, so hand docker a Windows path.
if command -v cygpath >/dev/null 2>&1; then COMPOSE_FILE="$(cygpath -w "$COMPOSE_FILE")"; fi
VICTIM="dht-b"       # the node this student is responsible for
VICTIM_NAME="node-b"

dht() { docker run --rm --network "$NETWORK" "$IMAGE" /app/dht_cli "$@"; }
field() { printf '%s\n' "$1" | grep -o "$2=[^ ]*" | head -n1 | cut -d= -f2-; }

pass=0
fail=0
ok() { echo "PASS: $*"; pass=$((pass + 1)); }
bad() { echo "FAIL: $*"; fail=$((fail + 1)); }

restore() {
  echo "==> restoring $VICTIM"
  docker compose -f "$COMPOSE_FILE" start "$VICTIM" >/dev/null 2>&1 || true
}
trap restore EXIT

echo "### 0. seed two keys: one owned by $VICTIM_NAME, one owned by node-a"
dht --op=put --key=lab1b:key12 --value="owned by node-b" --entry=node-a >/dev/null 2>&1
dht --op=put --key=lab1b:key1 --value="owned by node-a" --entry=node-a >/dev/null 2>&1
before=$(dht --op=get --key=lab1b:key12 --entry=node-a 2>&1)
if [ "$(field "$before" found)" = "1" ]; then
  ok "baseline: lab1b:key12 readable from node-a while the ring is healthy"
else
  bad "baseline: lab1b:key12 not readable before the fault: $before"
fi

echo
echo "### 1. stop $VICTIM_NAME"
docker compose -f "$COMPOSE_FILE" stop "$VICTIM" >/dev/null 2>&1
echo "    $VICTIM_NAME stopped"

echo
echo "### 2. a key owned by a *different* node still works"
out=$(dht --op=get --key=lab1b:key1 --entry=node-a 2>&1)
if [ "$(field "$out" found)" = "1" ]; then
  ok "lab1b:key1 (owner node-a) still readable from node-a while node-b is down"
else
  bad "lab1b:key1 should still be readable from node-a: $out"
fi

echo
echo "### 3. a key whose owner is the dead node fails, and says why"
out=$(dht --op=get --key=lab1b:key12 --entry=node-a 2>&1)
found=$(field "$out" found)
message=$(printf '%s\n' "$out" | sed -n 's/.*message=//p')
if [ "$found" = "0" ] && printf '%s' "$message" | grep -q "forward to successor $VICTIM_NAME failed"; then
  ok "lab1b:key12 unreadable, forward to $VICTIM_NAME failed as expected"
  echo "      raw: $message"
else
  bad "expected a forward failure for lab1b:key12, got: $out"
fi

echo
echo "### 4. the ring routes again after the node comes back"
restore
sleep 3
out=$(dht --op=get --key=lab1b:key12 --entry=node-a 2>&1)
found=$(field "$out" found)
owner=$(field "$out" owner)
if [ "$found" = "0" ] && [ "$owner" = "$VICTIM_NAME" ]; then
  ok "route restored: request reaches $VICTIM_NAME again, but the value is gone (in-memory store, no replication)"
else
  bad "expected found=0 from $VICTIM_NAME after restart, got: $out"
fi

out=$(dht --op=put --key=lab1b:key12 --value="rewritten after restart" --entry=node-a 2>&1)
if [ "$(field "$out" put_ok)" = "1" ]; then
  ok "write to $VICTIM_NAME works again after restart"
else
  bad "write to $VICTIM_NAME still failing after restart: $out"
fi

echo
echo "### summary: pass=$pass fail=$fail"
if [ "$fail" -ne 0 ]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS"
