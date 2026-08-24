# TinyKV

A Redis-inspired in-memory key-value store built from scratch in C++17 — implemented manually (no existing database libraries) to learn TCP networking, concurrency, storage engines, persistence, and distributed systems internals.

> **Status:** Under active development, built phase by phase. See `ramkrishna.txt` for the original project vision.

## Build

Requires CMake 3.16+ and a C++17 compiler.

```bash
mkdir build && cd build
cmake ..
make
```

This produces executables under `build/src/`:
- `tinykv-server` — the TinyKV server
- `tinykv-cli` — a client for talking to the server
- `tinykv-router` — a sharding-aware reverse proxy in front of multiple `tinykv-server` shards, each optionally replicated for automatic failover (`--config router.conf`, default port 7000; shard/primary/replica layout comes from `topology.conf`; see `docs/PROTOCOL.md`)

`tinykv-server`/`tinykv-cli` accept `--config <path>` (default: `tinykv.conf`) and `--port <n>` (overrides the config file); `tinykv-router` accepts the same, defaulting to `router.conf`.

## Testing

Requires [Google Test](https://github.com/google/googletest) (`brew install googletest`). With that installed, re-run `cmake ..` once to pick it up, then:

```bash
cd build
ctest --output-on-failure
```

40 tests across five categories - storage (`test_storage.cpp`), the LRU cache and TTL expiration (`test_cache.cpp`), real-socket request/response networking (`test_networking.cpp`), concurrent/thread-safety races (`test_concurrency.cpp`), and AOF/snapshot persistence (`test_persistence.cpp`). Each networking test spins up its own real `TcpServer` on a fixed high port and talks to it over an actual socket via `TcpClient`; each persistence test gets its own scratch directory under the OS temp dir. Distributed features (replication, sharding, failover - Phases 10A/10B/10C) are intentionally left to `scripts/smoke_test.sh` instead: those scenarios are multi-process kill/restart tests that fit scripted, real-process verification better than fast in-process unit tests.

`scripts/smoke_test.sh` remains the broader end-to-end regression check (protocol, persistence, concurrency, and the full sharding/replication/failover flow against real running processes) and should still be run after any change.

## Project layout

```
include/tinykv/   public headers
src/               implementation + entry points (server_main.cpp, cli_main.cpp)
tests/             unit tests (Google Test, from Phase 11)
benchmarks/        performance benchmarking tools (from Phase 9)
docs/              protocol and architecture documentation
scripts/           helper scripts
data/              runtime AOF/snapshot files (gitignored, from Phase 8)
```

Full documentation (architecture diagram, supported commands, benchmark results, design decisions) will be filled in as the project progresses.
