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
- `tinykv-router` — a sharding-aware reverse proxy in front of multiple `tinykv-server` shards (`--config router.conf`, default port 7000; see `docs/PROTOCOL.md`)

`tinykv-server`/`tinykv-cli` accept `--config <path>` (default: `tinykv.conf`) and `--port <n>` (overrides the config file); `tinykv-router` accepts the same, defaulting to `router.conf`.

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
