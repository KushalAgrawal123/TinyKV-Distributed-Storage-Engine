#!/usr/bin/env bash
# TinyKV smoke test - a lightweight regression check run after each phase,
# growing over time. Formal unit tests arrive in Phase 11 (Google Test).
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
HOST=127.0.0.1
PORT="${TINYKV_SMOKE_PORT:-16380}"  # distinct from the configured default (6380) to avoid clashing with a manually-run server

PASS=0
FAIL=0

cleanup() {
  for pid in "${SERVER_PID:-}" "${SHARDING_PIDS[@]:-}" "${ROUTER_PID:-}"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
    fi
  done
}
trap cleanup EXIT
SHARDING_PIDS=()

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  echo "==> Configuring (first run)"
  cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" > "$BUILD_DIR.configure.log" 2>&1 || {
    echo "CMAKE CONFIGURE FAILED - see $BUILD_DIR.configure.log"
    exit 1
  }
fi

echo "==> Building"
cmake --build "$BUILD_DIR" --parallel > "$BUILD_DIR/smoke_test_build.log" 2>&1 || {
  echo "BUILD FAILED - see $BUILD_DIR/smoke_test_build.log"
  exit 1
}

echo "==> Starting tinykv-server on port $PORT"
# A fresh, isolated data dir each run: since Phase 8, the server persists
# by default (dir=./data), so reusing a stale directory across runs would
# resurrect leftover keys (e.g. a counter from a prior run's INCR checks)
# and make the checks below flaky/wrong.
SMOKE_DATA_DIR="$BUILD_DIR/smoke_test_data"
rm -rf "$SMOKE_DATA_DIR"
mkdir -p "$SMOKE_DATA_DIR"
SMOKE_CONFIG="$BUILD_DIR/smoke_test.conf"
cat > "$SMOKE_CONFIG" <<EOF
port=$PORT
dir=$SMOKE_DATA_DIR
appendonly=true
save_interval=0
EOF

"$BUILD_DIR/src/tinykv-server" --config "$SMOKE_CONFIG" > "$BUILD_DIR/smoke_test_server.log" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 50); do
  nc -z "$HOST" "$PORT" 2>/dev/null && break
  sleep 0.1
done
if ! nc -z "$HOST" "$PORT" 2>/dev/null; then
  echo "SERVER DIDN'T START - see $BUILD_DIR/smoke_test_server.log"
  exit 1
fi

# check <description> <input> <expected-output> [port]
# Each call opens its own connection (printf | nc), so a full smoke run
# also exercises the server accepting many sequential clients. [port]
# defaults to the main server's $PORT; the sharding checks pass
# $ROUTER_PORT explicitly to target the router instead.
check() {
  local description="$1"
  local input="$2"
  local expected="$3"
  local target_port="${4:-$PORT}"
  local actual
  actual="$(printf '%b' "$input" | nc -w 2 "$HOST" "$target_port")"
  if [[ "$actual" == "$expected" ]]; then
    echo "  [PASS] $description"
    PASS=$((PASS + 1))
  else
    echo "  [FAIL] $description"
    printf '         expected: %q\n' "$expected"
    printf '         actual:   %q\n' "$actual"
    FAIL=$((FAIL + 1))
  fi
}

# checkTtlApprox <description> <input> <expectedSeconds>
# TTL counts down in real time, so an exact match is inherently flaky -
# this only requires the last reply line to be a ":<n>" with n in
# (expectedSeconds - 2, expectedSeconds].
checkTtlApprox() {
  local description="$1"
  local input="$2"
  local expectedSeconds="$3"
  local actual actualNum
  actual="$(printf '%b' "$input" | nc -w 2 "$HOST" "$PORT" | tail -1)"
  actualNum="${actual#:}"
  if [[ "$actual" =~ ^:[0-9]+$ ]] && ((actualNum <= expectedSeconds)) && ((actualNum > expectedSeconds - 2)); then
    echo "  [PASS] $description"
    PASS=$((PASS + 1))
  else
    echo "  [FAIL] $description"
    echo "         expected last line in (:$((expectedSeconds - 2)), :$expectedSeconds], got: $actual"
    FAIL=$((FAIL + 1))
  fi
}

echo "==> Protocol checks (Phase 4)"
check "SET returns OK"                 'SET foo bar\n' '+OK'
check "GET returns the value"          'GET foo\n' '+bar'
check "GET on a missing key is nil"    'GET nope\n' '$-1'
check "DEL an existing key returns 1"  'DEL foo\n' ':1'
check "GET after DEL is nil again"     'GET foo\n' '$-1'
check "DEL a missing key returns 0"    'DEL foo\n' ':0'
check "PING returns OK"                'PING\n' '+OK'
check "lowercase command names work"   'set lower case\n' '+OK'
check "unknown command is a clean error" 'BOGUS\n' "-ERR unknown command 'BOGUS'"
check "SET with wrong arity errors"    'SET onlykey\n' "-ERR wrong number of arguments for 'SET'"
check "GET with wrong arity errors"    'GET\n' "-ERR wrong number of arguments for 'GET'"

echo "==> Storage engine checks (Phase 6)"
check "INCR on a missing key starts at 1" 'INCR counter\n' ':1'
check "INCR again reaches 2"           'INCR counter\n' ':2'
check "DECR brings it back to 1"       'DECR counter\n' ':1'
check "INCR on a non-integer value errors" $'SET notanumber abc\nINCR notanumber\n' $'+OK\n-ERR value is not an integer or out of range'

echo "==> Expiration checks (Phase 7)"
check "TTL on a missing key is -2"     'TTL nosuchkey\n' ':-2'
check "SET without EX has no TTL"      $'SET permanent here\nTTL permanent\n' $'+OK\n:-1'
checkTtlApprox "SET ... EX sets a TTL" $'SET withttl here EX 100\nTTL withttl\n' 100
check "PERSIST removes the TTL"        $'PERSIST withttl\nTTL withttl\n' $':1\n:-1'
check "EXPIRE on a missing key returns 0" 'EXPIRE nosuchkey 10\n' ':0'
checkTtlApprox "EXPIRE sets a TTL on an existing key" $'EXPIRE permanent 50\nTTL permanent\n' 50
check "SET ... EX with a bad seconds value errors" 'SET x y EX notanumber\n' "-ERR invalid expire time in 'SET' command"
check "plain SET clears a previous TTL" $'EXPIRE permanent 50\nSET permanent again\nTTL permanent\n' $':1\n+OK\n:-1'

echo "==> Concurrency checks (Phase 5)"
CONCURRENT_TOTAL=30
CONCURRENT_DIR="$BUILD_DIR/smoke_test_concurrency"
rm -rf "$CONCURRENT_DIR"
mkdir -p "$CONCURRENT_DIR"
CONCURRENT_PIDS=()
for i in $(seq 1 "$CONCURRENT_TOTAL"); do
  (printf 'PING\n' | nc -w 2 "$HOST" "$PORT" > "$CONCURRENT_DIR/$i.out") &
  CONCURRENT_PIDS+=("$!")
done
# Wait only on the nc jobs above, not the still-running server (also a
# background job of this shell) - a bare `wait` would block forever.
wait "${CONCURRENT_PIDS[@]}"
CONCURRENT_OK=$(grep -lx '+OK' "$CONCURRENT_DIR"/*.out 2>/dev/null | wc -l | tr -d ' ')
if [[ "$CONCURRENT_OK" -eq "$CONCURRENT_TOTAL" ]]; then
  echo "  [PASS] $CONCURRENT_TOTAL concurrent PING connections all succeeded"
  PASS=$((PASS + 1))
else
  echo "  [FAIL] only $CONCURRENT_OK/$CONCURRENT_TOTAL concurrent PING connections succeeded"
  FAIL=$((FAIL + 1))
fi
rm -rf "$CONCURRENT_DIR"

echo "==> Sharding & routing checks (Phase 10B/10C)"
PRIMARY_BASE_PORT=$((PORT + 1))
REPLICA_BASE_PORT=$((PORT + 51))
ROUTER_PORT=$((PORT + 10))
PRIMARY_PIDS=()
PRIMARY_ADDRS=()
REPLICA_ADDRS=()

# Sets LAST_SHARD_PID and appends to SHARDING_PIDS. Not used via command
# substitution ($(...)) anywhere - that would run it in a subshell and
# lose both of those mutations back in the caller.
startShardNode() {
  local node_port="$1"
  local node_dir="$BUILD_DIR/smoke_test_shard_$node_port"
  rm -rf "$node_dir"
  mkdir -p "$node_dir"
  local node_conf="$BUILD_DIR/smoke_test_shard_$node_port.conf"
  cat > "$node_conf" <<EOF
port=$node_port
dir=$node_dir
appendonly=false
save_interval=0
EOF
  "$BUILD_DIR/src/tinykv-server" --config "$node_conf" > "$BUILD_DIR/smoke_test_shard_$node_port.log" 2>&1 &
  LAST_SHARD_PID=$!
  SHARDING_PIDS+=("$LAST_SHARD_PID")
}

for i in 0 1 2; do
  primary_port=$((PRIMARY_BASE_PORT + i))
  replica_port=$((REPLICA_BASE_PORT + i))
  startShardNode "$primary_port"; PRIMARY_PIDS+=("$LAST_SHARD_PID")
  startShardNode "$replica_port"
  PRIMARY_ADDRS+=("$HOST:$primary_port")
  REPLICA_ADDRS+=("$HOST:$replica_port")
done

# Wait for all 6 shard-node ports before wiring up replication between
# them, so REPLICAOF below isn't racing a primary that hasn't bound yet.
ALL_UP=1
for i in 0 1 2; do
  for p in "$((PRIMARY_BASE_PORT + i))" "$((REPLICA_BASE_PORT + i))"; do
    ok=0
    for _ in $(seq 1 50); do
      nc -z "$HOST" "$p" 2>/dev/null && { ok=1; break; }
      sleep 0.1
    done
    [[ "$ok" -eq 1 ]] || ALL_UP=0
  done
done

if [[ "$ALL_UP" -ne 1 ]]; then
  echo "  [FAIL] shard primaries/replicas didn't all start"
  FAIL=$((FAIL + 1))
else
  for i in 0 1 2; do
    replica_port=$((REPLICA_BASE_PORT + i))
    printf 'REPLICAOF %s %s\n' "$HOST" "$((PRIMARY_BASE_PORT + i))" | nc -w 2 "$HOST" "$replica_port" > /dev/null
  done
  sleep 0.3  # let each replica finish its initial SYNC

  TOPOLOGY_CONF="$BUILD_DIR/smoke_test_topology.conf"
  {
    echo "shards=shard0,shard1,shard2"
    for i in 0 1 2; do
      echo "shard$i.primary=${PRIMARY_ADDRS[$i]}"
      echo "shard$i.replica=${REPLICA_ADDRS[$i]}"
    done
  } > "$TOPOLOGY_CONF"

  ROUTER_CONF="$BUILD_DIR/smoke_test_router.conf"
  cat > "$ROUTER_CONF" <<EOF
port=$ROUTER_PORT
topology_file=$TOPOLOGY_CONF
health_check_interval_ms=200
max_missed_pings=2
EOF
  "$BUILD_DIR/src/tinykv-router" --config "$ROUTER_CONF" > "$BUILD_DIR/smoke_test_router.log" 2>&1 &
  ROUTER_PID=$!

  ROUTER_UP=0
  for _ in $(seq 1 50); do
    nc -z "$HOST" "$ROUTER_PORT" 2>/dev/null && { ROUTER_UP=1; break; }
    sleep 0.1
  done

  if [[ "$ROUTER_UP" -ne 1 ]]; then
    echo "  [FAIL] router didn't start - see $BUILD_DIR/smoke_test_router.log"
    FAIL=$((FAIL + 1))
  else
    routeCheck() {
      local description="$1" key="$2"
      local node
      node="$(printf 'ROUTE %s\n' "$key" | nc -w 2 "$HOST" "$ROUTER_PORT")"
      if [[ "$node" =~ ^\+$HOST:[0-9]+$ ]]; then
        echo "  [PASS] $description ($node)"
        PASS=$((PASS + 1))
      else
        echo "  [FAIL] $description (got: $node)"
        FAIL=$((FAIL + 1))
      fi
    }

    echo "-- Phase 10B: basic routing --"
    check "SET through the router" "SET shardkey shardval\n" '+OK' "$ROUTER_PORT"
    check "GET through the router returns the value" 'GET shardkey\n' '+shardval' "$ROUTER_PORT"
    routeCheck "ROUTE reports a valid node address" 'shardkey'
    check "PING through the router is answered locally" 'PING\n' '+OK' "$ROUTER_PORT"
    check "ROUTE with wrong arity errors" 'ROUTE\n' "-ERR wrong number of arguments for 'ROUTE'" "$ROUTER_PORT"
    check "ROUTE sent directly to a shard is rejected" 'ROUTE x\n' "-ERR ROUTE is only valid against tinykv-router, not tinykv-server" "$PRIMARY_BASE_PORT"

    # Consistency: the node ROUTE names must be the one actually holding
    # the value - write and read 12 distinct keys through the router,
    # then read each one straight from the node ROUTE reported.
    DIRECT_OK=1
    for i in $(seq 1 12); do
      printf 'SET rkey%d rval%d\n' "$i" "$i" | nc -w 2 "$HOST" "$ROUTER_PORT" > /dev/null
    done
    for i in $(seq 1 12); do
      node="$(printf 'ROUTE rkey%d\n' "$i" | nc -w 2 "$HOST" "$ROUTER_PORT")"
      node="${node#+}"
      node_host="${node%:*}"
      node_port="${node#*:}"
      direct="$(printf 'GET rkey%d\n' "$i" | nc -w 2 "$node_host" "$node_port")"
      [[ "$direct" == "+rval$i" ]] || DIRECT_OK=0
    done
    if [[ "$DIRECT_OK" -eq 1 ]]; then
      echo "  [PASS] 12/12 keys found directly on the node ROUTE reported"
      PASS=$((PASS + 1))
    else
      echo "  [FAIL] at least one key wasn't found directly on the node ROUTE reported"
      FAIL=$((FAIL + 1))
    fi

    echo "-- Phase 10C: NODES + failover --"
    initial_nodes="$(printf 'NODES\n' | nc -w 2 "$HOST" "$ROUTER_PORT")"
    if [[ "$initial_nodes" == +shard0:* ]] && [[ "$initial_nodes" != *DOWN* ]]; then
      echo "  [PASS] NODES reports all 6 nodes UP before any failure"
      PASS=$((PASS + 1))
    else
      echo "  [FAIL] NODES before any failure: $initial_nodes"
      FAIL=$((FAIL + 1))
    fi

    printf 'SET failoverkey beforecrash\n' | nc -w 2 "$HOST" "$ROUTER_PORT" > /dev/null
    owner="$(printf 'ROUTE failoverkey\n' | nc -w 2 "$HOST" "$ROUTER_PORT")"
    owner="${owner#+}"
    owner_index=-1
    for i in 0 1 2; do
      [[ "$owner" == "${PRIMARY_ADDRS[$i]}" ]] && owner_index=$i
    done

    if [[ "$owner_index" -lt 0 ]]; then
      echo "  [FAIL] failoverkey didn't route to a known primary (got: $owner)"
      FAIL=$((FAIL + 1))
    else
      target_pid="${PRIMARY_PIDS[$owner_index]}"
      target_replica="${REPLICA_ADDRS[$owner_index]}"
      kill -9 "$target_pid" 2>/dev/null
      wait "$target_pid" 2>/dev/null

      # health_check_interval_ms=200 * max_missed_pings=2 = 400ms to
      # detect + promote; give it generous headroom for a loaded CI box.
      sleep 2

      afterCrashGet="$(printf 'GET failoverkey\n' | nc -w 2 "$HOST" "$ROUTER_PORT")"
      if [[ "$afterCrashGet" == "+beforecrash" ]]; then
        echo "  [PASS] data written before the crash survives on the promoted replica"
        PASS=$((PASS + 1))
      else
        echo "  [FAIL] GET failoverkey after primary crash: $afterCrashGet"
        FAIL=$((FAIL + 1))
      fi

      afterCrashRoute="$(printf 'ROUTE failoverkey\n' | nc -w 2 "$HOST" "$ROUTER_PORT")"
      if [[ "$afterCrashRoute" == "+$target_replica" ]]; then
        echo "  [PASS] ROUTE now points at the promoted replica ($target_replica)"
        PASS=$((PASS + 1))
      else
        echo "  [FAIL] ROUTE after failover: expected +$target_replica, got $afterCrashRoute"
        FAIL=$((FAIL + 1))
      fi

      check "writes still work via the promoted (now-primary) replica" 'SET failoverkey2 aftercrash\n' '+OK' "$ROUTER_PORT"
      check "the write above is actually readable back" 'GET failoverkey2\n' '+aftercrash' "$ROUTER_PORT"

      nodesAfter="$(printf 'NODES\n' | nc -w 2 "$HOST" "$ROUTER_PORT")"
      expected_entry="shard$owner_index:PRIMARY:${PRIMARY_ADDRS[$owner_index]}:DOWN:STANDBY"
      if [[ "$nodesAfter" == *"$expected_entry"* ]]; then
        echo "  [PASS] NODES reports the crashed primary as DOWN/STANDBY"
        PASS=$((PASS + 1))
      else
        echo "  [FAIL] NODES after failover didn't show the expected entry ($expected_entry): $nodesAfter"
        FAIL=$((FAIL + 1))
      fi

      # Restart the old primary (simulated recovery) and confirm it does
      # NOT silently resume - the whole point of latching failedOver.
      startShardNode "${PRIMARY_ADDRS[$owner_index]#*:}"
      sleep 1.5  # let a couple of health-check cycles observe it's back UP
      routeAfterRestart="$(printf 'ROUTE failoverkey\n' | nc -w 2 "$HOST" "$ROUTER_PORT")"
      if [[ "$routeAfterRestart" == "+$target_replica" ]]; then
        echo "  [PASS] recovered old primary does not auto-resume - replica still active"
        PASS=$((PASS + 1))
      else
        echo "  [FAIL] recovered primary auto-resumed (split-brain risk): $routeAfterRestart"
        FAIL=$((FAIL + 1))
      fi
    fi
  fi

  kill "$ROUTER_PID" 2>/dev/null; wait "$ROUTER_PID" 2>/dev/null
  ROUTER_PID=""
fi

for pid in "${SHARDING_PIDS[@]}"; do kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
SHARDING_PIDS=()

echo ""
echo "$PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
