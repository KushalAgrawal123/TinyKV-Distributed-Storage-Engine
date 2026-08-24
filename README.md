# TinyKV

A Redis-inspired in-memory key-value store built from scratch in C++17 — no existing database libraries — to learn TCP networking, concurrency, storage engines, persistence, and distributed systems internals from the ground up. See `ramkrishna.txt` for the original project vision this was built from.

**Tech stack:** C++17 · BSD sockets · `std::thread`/`std::shared_mutex`/`std::condition_variable` · CMake · Google Test

> **Status:** All 14 planned phases complete - single-node server, custom protocol, persistence, replication, sharding, failover, tests, and this documentation.

## Architecture

```
                                   ┌────────────────────────────────────┐
                                   │         tinykv-router (opt.)        │
  clients (nc / tinykv-cli) ─────▶ │  ConsistentHashRing + ShardTopology │
                                   │  + HealthChecker (failover)         │
                                   └───────┬─────────────┬───────┬──────┘
                                           │             │       │
                     ┌─────────────────────┘             │       └───────────────────┐
                     ▼                                   ▼                           ▼
              shard0 (tinykv-server)             shard1 (tinykv-server)      shard2 (tinykv-server)
              primary ──REPLICAOF──▶ replica      primary ──▶ replica         primary ──▶ replica


  One tinykv-server, expanded:

  ┌───────────────────────────────────────────────────────────────────────┐
  │  TcpServer (accept loop, one thread per connection)                    │
  │        │                                                               │
  │        ▼                                                               │
  │  Parser::parse ─▶ Command ─▶ CommandExecutor::execute ─▶ Reply         │
  │        │                          │            │                      │
  │        │                          ▼            ▼                      │
  │        │                     KVStore      ExpiryManager                │
  │        │                (shared_mutex,    (min-heap +                  │
  │        │                 LRU list)         sweeper thread)             │
  │        ▼                                                               │
  │  PersistenceManager ──▶ AOF (every write) + periodic snapshot          │
  │        │                    (ThreadPool background jobs)               │
  │        ▼                                                               │
  │  ReplicationManager ──▶ ReplicaRegistry (propagate to replicas)        │
  │                     └─▶ ReplicaLink (if this node IS a replica)        │
  └───────────────────────────────────────────────────────────────────────┘
```

A client always speaks the same line-based text protocol whether it's talking directly to a `tinykv-server` or through `tinykv-router` - the router is a transparent proxy, not a different protocol.

## Quick start

Requires CMake 3.16+ and a C++17 compiler.

```bash
mkdir build && cd build
cmake ..
make
```

This produces four executables under `build/src/`:

| Executable | Purpose |
|---|---|
| `tinykv-server` | the actual key-value store (`--config tinykv.conf`, `--port <n>`) |
| `tinykv-cli` | an interactive REPL client |
| `tinykv-router` | sharding-aware reverse proxy with automatic failover (`--config router.conf`) |
| `tinykv-bench` | the load-generating benchmark tool (see Benchmarks below) |

Start a server and talk to it:

```bash
./build/src/tinykv-server &
./build/src/tinykv-cli
# or: printf 'SET foo bar\nGET foo\n' | nc localhost 6380
```

## Supported commands

See `docs/PROTOCOL.md` for the full wire format (request/reply framing, error conventions) and worked examples. Summary:

| Command | Description |
|---|---|
| `SET key value [EX seconds]` | store a value, optionally with a TTL |
| `GET key` | fetch a value, or nil if missing |
| `DEL key` | remove a key |
| `INCR key` / `DECR key` | atomic increment/decrement (missing key reads as 0) |
| `TTL key` | seconds remaining, `-1` no TTL, `-2` no key |
| `EXPIRE key seconds` | set/replace a key's TTL |
| `PERSIST key` | remove a key's TTL |
| `PING` | liveness probe |
| `SAVE` | force an immediate snapshot + AOF reset |
| `REPLICAOF host port` / `REPLICAOF NO ONE` | become a replica of / stop being a replica |
| `SYNC` | internal - replica handshake, not meant to be typed by hand |
| `ROUTE key` | **router only** - which node currently owns `key` |
| `NODES` | **router only** - health/role/active status of every shard node |

## Configuration reference

**`tinykv.conf`** (a `tinykv-server` instance):

| Key | Default | Meaning |
|---|---|---|
| `port` | `6380` | listen port |
| `log_level` | `INFO` | `DEBUG`/`INFO`/`WARN`/`ERROR` |
| `max_keys` | `0` | LRU capacity; `0` = unbounded |
| `appendonly` | `true` | enable the append-only file |
| `dir` | `./data` | where the AOF/snapshot live |
| `dbfilename` | `dump.tkv` | snapshot filename |
| `appendfilename` | `tinykv.aof` | AOF filename |
| `save_interval` | `300` | seconds between automatic snapshots; `0` disables |

**`router.conf`** (a `tinykv-router` instance):

| Key | Default | Meaning |
|---|---|---|
| `port` | `7000` | listen port |
| `topology_file` | `topology.conf` | where the shard layout lives |
| `health_check_interval_ms` | `1000` | how often each node is PINGed |
| `max_missed_pings` | `3` | consecutive misses before a node is DOWN |

**`topology.conf`** (the shard/primary/replica layout a router loads):

```
shards=shard0,shard1,shard2
shard0.primary=127.0.0.1:6381
shard0.replica=127.0.0.1:6391   # optional - omit for a shard with no failover target
...
```

Each shard's replica must separately be told to replicate (`REPLICAOF <primary host> <primary port>`, run directly against it) - the router only *fails over* an already-replicating pair, it doesn't wire up replication itself.

## Testing

Requires [Google Test](https://github.com/google/googletest) (`brew install googletest`); re-run `cmake ..` once after installing it to pick it up.

```bash
cd build
ctest --output-on-failure
```

40 unit tests across five categories - storage, the LRU cache and TTL expiration, real-socket networking, concurrency/thread-safety races, and AOF/snapshot persistence. Distributed features (replication, sharding, failover) are deliberately left to `scripts/smoke_test.sh` instead: those are multi-process kill/restart scenarios that fit scripted, real-process verification better than fast in-process unit tests. Run the smoke test for full end-to-end coverage, including sharding/replication/failover against real running processes:

```bash
./scripts/smoke_test.sh
```

## Benchmarks

```bash
python3 scripts/run_bench_suite.py
```

runs `tinykv-bench` (a small C++ load generator reusing TinyKV's own `TcpClient`, so interpreter overhead never pollutes the latency numbers) across a matrix of client counts, each client holding one persistent connection and issuing requests sequentially - real round-trip latency, matching the protocol's own no-pipelining design.

Actual results from this machine (Apple Silicon, single `tinykv-server` process, 500 requests/client, 50/50 SET/GET mix, 64-byte values, `appendonly=false`):

| Clients | Throughput (ops/sec) | Latency avg | p50 | p95 | p99 |
|---:|---:|---:|---:|---:|---:|
| 10 | 123,579 | 79.6 µs | 86.5 µs | 132.7 µs | 164.6 µs |
| 100 | 203,324 | 485.8 µs | 480.0 µs | 619.3 µs | 868.8 µs |
| 1000 | 160,685 | 5.37 ms | 5.08 ms | 11.30 ms | 33.20 ms |

Throughput peaks around 100 concurrent clients and then falls off at 1000 - the thread-per-connection model means 1000 clients means 1000 OS threads all contending for `KVStore`'s lock and CPU scheduling time, not 1000 clients being served efficiently in parallel. This is the direct, visible cost of the project's thread-per-connection design choice (see Design decisions) and exactly the kind of result an event-loop server (or a real Redis) avoids.

For scale, `redis-benchmark -p 6379 -t set,get -c 100` against a real local Redis on the same machine: **~196,850 SET/sec, ~248,756 GET/sec** at the same concurrency. TinyKV is roughly an order of magnitude off a production database's single-threaded epoll loop and decades of micro-optimization - a reasonable outcome for a learning project, and a useful, concrete number for *why* real databases are built the way they are.

## Design decisions

**Hash table for storage.** `KVStore` is `unordered_map<string,string>` plus an intrusive doubly-linked list for LRU order - O(1) average get/set/del, which is the entire point of a key-value store. The map+list combo (rather than, say, a sorted structure) trades range-scan support (never a requirement here) for guaranteed O(1) point lookups.

**`shared_mutex`, not a single global lock.** Started as one `std::mutex` (Phase 6) and upgraded to `std::shared_mutex` once LRU tracking made the trade-off worth quantifying: `shared_lock` for reads, `unique_lock` for writes, so concurrent `GET`s don't serialize behind each other. The LRU list means `get()` still needs a write-ish lock to update recency order, quietly eroding some of that read concurrency - the same trade-off real Redis-likes make, accepted here rather than building an approximate clock-based LRU to avoid it.

**Thread-per-connection, not an event loop.** Each accepted client gets its own detached `std::thread` (`TcpServer::run`). This is directly debuggable in a normal debugger (one thread = one client, no callback-stack archaeology) and matches what the original vision doc asks for. The benchmark table above shows its actual cost: throughput falls off past ~100 clients as thread/lock contention dominates, which an epoll-based single-threaded loop (what real Redis and this project's `tinykv-router`'s health-checker-adjacent alternative would use) wouldn't hit nearly as early. A separate, generic `ThreadPool` exists for a different job entirely - internal background work (the TTL sweeper, the periodic snapshotter) - so connection I/O and background jobs never compete for the same thread pool.

**AOF + snapshot persistence, both plain text.** Every successful write is appended to an append-only file as the exact wire-protocol line that produced it; a snapshot is just a sequence of `SET key value` lines - the same format the AOF and the live protocol already use. That means loading either one is *just replaying it through the normal Parser → CommandExecutor pipeline* - no separate serialization format to build, test, or keep in sync with the command set as it grows. The cost: TTLs aren't captured precisely (a replayed `SET ... EX` restores a TTL relative to replay time, not the original deadline), and `SAVE`'s AOF "rewrite" is a full truncate-and-restart rather than real offset tracking - both disclosed trade-offs, not oversights, in exchange for one format instead of two.

**Consistent hashing for sharding, not `hash(key) % N`.** `tinykv-router` hashes each *logical shard id* (not physical address) onto a ring via 150 virtual positions per shard (64-bit FNV-1a with a MurmurHash3-style finalizing mix - plain FNV-1a alone was measured, during Phase 10B verification, to cluster keys differing only in a trailing digit onto the same shard; the finalizer was added specifically to fix that). Hashing shard ids rather than addresses is what lets a failover change *which node* serves a shard without changing *which shard* a key belongs to. Adding a shard remaps only the fraction of keys between its new virtual positions and their neighbors, not the whole keyspace - verified directly in Phase 10B (4/25 keys remapped on a 3→4 shard resize, versus nearly all of them under naive modulo hashing).

**Single-coordinator failover, not consensus.** `tinykv-router`'s `HealthChecker` is the sole authority deciding when a shard's primary is DOWN and promoting its replica - there's no Raft/Paxos, no quorum, no split-brain protection beyond one hard rule: a promotion never reverses itself automatically, even once the old primary is reachable again. Building real consensus was explicitly out of scope for what the original vision doc asked for; this is the simplest failover model that's still genuinely useful and honestly labeled as what it is.

**A deliberately simple wire protocol.** Space-delimited, no quoting, one reply per line, RESP-inspired type prefixes (`+`/`-`/`:`/`$-1`). Every example in the original vision doc uses single-word values, so there was never a real requirement for binary-safe length-prefixed framing - the simpler format keeps the parser, AOF, snapshot, and replication stream all sharing one trivial format, at the cost of not supporting values with embedded whitespace (see Known limitations).

## Known limitations

By design, for a project scoped around learning rather than production readiness:

- **Values can't contain embedded whitespace** - the wire protocol has no quoting mechanism (see Design decisions above).
- **No authentication or ACLs** - anyone who can open a TCP connection has full read/write access.
- **TTLs aren't persisted precisely** - AOF/snapshot replay restores a TTL relative to replay time, not the original wall-clock deadline.
- **AOF "rewrite" is truncate-on-`SAVE`**, not real offset-tracked incremental rewriting.
- **No dynamic shard rebalancing** - `topology.conf` is read once at router startup; adding/removing a shard changes future routing decisions but never physically moves existing data.
- **Failover has no split-brain protection beyond a one-way latch** - see Design decisions; there's no quorum, and a recovered primary must be manually re-pointed with `REPLICAOF`.
- **Health checks have no explicit timeout** - `HealthChecker` and `ReplicaLink` both rely on the OS delivering `ECONNREFUSED`/`RST` promptly (true for a killed process, not guaranteed for a merely unresponsive one).

## Project layout

```
include/tinykv/    public headers, mirrors src/'s subfolders
src/                implementation + entry points (server_main.cpp, cli_main.cpp, router_main.cpp)
tests/              unit tests (Google Test)
benchmarks/         tinykv-bench + run_bench_suite.py
docs/               PROTOCOL.md - full wire protocol reference
scripts/            smoke_test.sh (end-to-end regression suite) + bench orchestration
data/               runtime AOF/snapshot files (gitignored)
tinykv.conf / router.conf / topology.conf   default runtime configuration
```

## What this project covers

Built phase by phase - project setup, an in-memory storage engine, TCP client/server architecture, a custom database protocol, multi-client concurrency, thread-safe storage, LRU caching and key expiration, an AOF+snapshot persistence layer, performance benchmarking, primary/replica replication, consistent-hash sharding and routing, single-coordinator fault tolerance, a Google Test suite, and this documentation - directly exercising TCP networking, OS-level concurrency and thread synchronization, storage engine design, persistence and crash recovery, and distributed systems trade-offs (replication, partitioning, failover) from first principles.
