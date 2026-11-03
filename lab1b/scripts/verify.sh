#!/usr/bin/env bash
# Acceptance check for Lab1B, runnable on the local ring and (with the
# cloud yaml) on the three CVMs. It asserts the same things the report claims:
#   1. six keys cover all three owners
#   2. every key is readable from a non-owner entry, with the hop count the
#      static ring implies
#   3. CreateRide / GetTrip still work, with 41.04 and 17.20 like Lab1A
set -uo pipefail

# Git Bash rewrites "/app/dht_cli" into "C:/Program Files/Git/app/dht_cli"
# before docker.exe sees it. 1A hit this; keep the guard in every script.
export MSYS_NO_PATHCONV=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-lab1b-dht-microservices:local}"
NETWORK="${NETWORK:-lab1b_ridenet}"
CLIENT_CONFIG="${CLIENT_CONFIG:-/app/config/ride-client.yaml}"

dht() { docker run --rm --network "$NETWORK" "$IMAGE" /app/dht_cli "$@"; }
ride() { docker run --rm --network "$NETWORK" "$IMAGE" /app/ride_client --client_config="$CLIENT_CONFIG" "$@"; }

pass=0
fail=0
ok() { echo "PASS: $*"; pass=$((pass + 1)); }
bad() { echo "FAIL: $*"; fail=$((fail + 1)); }
field() { printf '%s\n' "$1" | grep -o "$2=[^ ]*" | head -n1 | cut -d= -f2-; }

# key -> expected owner, computed offline with the same FNV-1a used by the nodes:
#   node-a 79934867 (0x04c3b593), node-b 96712486 (0x05c3b726), node-c 113490105 (0x06c3b8b9)
KEYS="lab1b:key1 lab1b:key2 lab1b:key12 lab1b:key40 lab1b:key41 lab1b:key169"
declare -A EXPECTED=(
  [lab1b:key1]=node-a
  [lab1b:key2]=node-a
  [lab1b:key12]=node-b
  [lab1b:key40]=node-b
  [lab1b:key41]=node-c
  [lab1b:key169]=node-c
)
ENTRIES="node-a node-b node-c"

ring_index() {
  case "$1" in
    node-a) echo 0 ;;
    node-b) echo 1 ;;
    node-c) echo 2 ;;
    *) echo -1 ;;
  esac
}

echo "### 1. ring coverage: 6 keys, 3 owners"
seen_owners=""
for key in $KEYS; do
  value="value for $key"
  out=$(dht --op=put --key="$key" --value="$value" --entry=node-a 2>&1)
  put_ok=$(field "$out" put_ok)
  owner=$(field "$out" owner)
  hops=$(field "$out" hops)
  expected_owner="${EXPECTED[$key]}"
  expected_hops=$(( $(ring_index "$expected_owner") - 0 ))   # entry is node-a

  if [ "$put_ok" = "1" ] && [ "$owner" = "$expected_owner" ] && [ "$hops" = "$expected_hops" ]; then
    ok "put $key entry=node-a owner=$owner hops=$hops (expected $expected_owner/$expected_hops)"
  else
    bad "put $key entry=node-a -> ok=$put_ok owner=$owner hops=$hops, expected $expected_owner/$expected_hops"
  fi
  case " $seen_owners " in
    *" $owner "*) ;;
    *) seen_owners="$seen_owners $owner" ;;
  esac
done

distinct=$(printf '%s\n' $seen_owners | sort -u | wc -l | tr -d ' ')
if [ "$distinct" = "3" ]; then
  ok "keys landed on all three owners:$seen_owners"
else
  bad "keys only landed on$seen_owners (want node-a, node-b, node-c)"
fi

echo
echo "### 2. every key readable from a non-owner entry"
for key in $KEYS; do
  owner="${EXPECTED[$key]}"
  for entry in $ENTRIES; do
    if [ "$entry" = "$owner" ]; then
      continue
    fi
    out=$(dht --op=get --key="$key" --entry="$entry" 2>&1)
    found=$(field "$out" found)
    got_owner=$(field "$out" owner)
    hops=$(field "$out" hops)
    value=$(printf '%s\n' "$out" | sed -n 's/^value_bytes=[0-9]* value=//p')
    expected_hops=$(( $(ring_index "$owner") - $(ring_index "$entry") ))
    if [ "$expected_hops" -lt 0 ]; then
      expected_hops=$(( expected_hops + 3 ))
    fi
    if [ "$found" = "1" ] && [ "$got_owner" = "$owner" ] && [ "$value" = "value for $key" ] && [ "$hops" = "$expected_hops" ]; then
      ok "get $key entry=$entry owner=$got_owner hops=$hops (expected hops $expected_hops)"
    else
      bad "get $key entry=$entry -> found=$found owner=$got_owner hops=$hops value='$value', expected owner=$owner hops=$expected_hops"
    fi
  done
done

echo
echo "### 3. ride service still honours the 1A contract"
ride_ids=""
check_ride() {
  local distance="$1" slot="$2" fare="$3"
  local out trip_id got_fare status
  out=$(ride --op=create --distance_km="$distance" --time_slot="$slot" 2>&1)
  trip_id=$(field "$out" trip_id)
  got_fare=$(field "$out" estimated_fare)
  status=$(field "$out" status)
  if [ -n "$trip_id" ] && [ "$got_fare" = "$fare" ] && [ "$status" = "WAITING" ]; then
    ok "CreateRide $slot distance=${distance}km -> $trip_id fare=$got_fare status=$status"
  else
    bad "CreateRide $slot distance=${distance}km -> fare=$got_fare status=$status (expected $fare/WAITING)"
    return
  fi
  ride_ids="$ride_ids $trip_id"

  out=$(ride --op=get --trip_id="$trip_id" 2>&1)
  local found read_fare
  found=$(field "$out" found)
  read_fare=$(field "$out" estimated_fare)
  if [ "$found" = "1" ] && [ "$read_fare" = "$fare" ]; then
    ok "GetTrip $trip_id -> found=1 fare=$read_fare (served by the DHT, not a local map)"
  else
    bad "GetTrip $trip_id -> found=$found fare=$read_fare (expected found=1 fare=$fare)"
  fi
}
check_ride 8.5 peak 41.04
check_ride 3.0 offpeak 17.20

echo
echo "### summary: pass=$pass fail=$fail"
if [ "$fail" -ne 0 ]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS"
