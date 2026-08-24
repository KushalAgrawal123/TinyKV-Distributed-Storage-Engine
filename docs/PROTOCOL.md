# TinyKV Wire Protocol

TinyKV speaks a simple, line-based, text protocol over TCP. It's loosely
inspired by Redis's RESP, but trades RESP's binary-safe length-prefixed
framing for something a human can drive directly from `nc`.

## Transport

- One TCP connection per client.
- Request/response, no pipelining: the client sends one request line and
  waits for the matching response line before sending the next request.
- All text is ASCII. Every line (request or response) ends with `\n`. A
  trailing `\r` before the `\n` is tolerated and stripped, so both plain
  `\n` clients (e.g. `nc`) and `\r\n` clients work.
- A request line must not exceed **8192 bytes**, or the connection is
  closed - this bounds how much a single broken/hostile client can force
  the server to buffer while waiting for a `\n` that never arrives.

## Requests

```
<COMMAND> <arg1> <arg2> ...
```

- The command name and arguments are separated by whitespace.
- The command name is case-insensitive (`set`, `SET`, and `Set` are
  equivalent).
- Values cannot contain embedded whitespace, since whitespace is the only
  argument separator and there is no quoting mechanism. This is a
  deliberate simplification, not a bug - see Known Limitations in the
  project README.
- An unrecognized command name is not a protocol-level error: the
  connection stays open and the server replies with a normal `-ERR` line.

## Replies

Every reply is exactly one line, starting with a type prefix:

| Prefix | Meaning                  | Example                        |
|--------|--------------------------|---------------------------------|
| `+`    | Simple string / success  | `+OK`, `+Kushal`                |
| `-`    | Error                    | `-ERR unknown command 'BOGUS'`  |
| `:`    | Integer                  | `:1`                            |
| `$-1`  | Nil (no value)           | `$-1`                           |

`$-1` means "no value" (e.g. `GET` on a missing key) - it is never an
error.

## Commands (as of Phase 10C)

| Command                  | Arguments        | Reply                                                     |
|---------------------------|------------------|---------------------------------------------------------------|
| `SET key value`           | key, value       | `+OK`                                                          |
| `SET key value EX seconds`| key, value, seconds (positive integer) | `+OK`; sets the value and a TTL in one step |
| `GET key`                 | key              | `+<value>`, or `$-1` if the key doesn't exist                  |
| `DEL key`                 | key              | `:1` if the key existed, else `:0`                             |
| `PING`                    | (none)           | `+OK`                                                           |
| `INCR key`                | key              | `:<new value>`, after adding 1 (a missing key reads as 0)       |
| `DECR key`                | key              | `:<new value>`, after subtracting 1                             |
| `TTL key`                 | key              | seconds remaining as `:<n>`; `:-1` if the key has no TTL, `:-2` if the key doesn't exist |
| `EXPIRE key seconds`      | key, seconds (positive integer) | `:1` if a TTL was set, `:0` if the key doesn't exist |
| `PERSIST key`             | key              | `:1` if a TTL was removed, `:0` if the key didn't exist or had none |
| `SAVE`                    | (none)           | `+OK`; immediately snapshots the dataset and resets the AOF |
| `REPLICAOF host port`     | host, port       | `+OK`; becomes a replica of host:port                          |
| `REPLICAOF NO ONE`        | literal `NO ONE` | `+OK`; stops replicating and becomes a primary again            |
| `SYNC`                    | (none)           | internal - see Replication below, not meant to be typed by hand |
| `ROUTE key`                | key              | `+<host>:<port>`; **only valid against `tinykv-router`**, see Sharding below |
| `NODES`                    | (none)           | `+<node>;<node>;...`; **only valid against `tinykv-router`**, see Fault tolerance below |

Any write command (`SET`, `DEL`, `INCR`, `DECR`, `EXPIRE`, `PERSIST`) sent
directly by a client to a replica is rejected with
`-READONLY You can't write against a replica.` instead of being executed.
`REPLICAOF` itself is always allowed, regardless of current role - that's
the only way to change or exit replica mode.

### Persistence

Every successfully-executed write command (`SET`, `DEL`, `INCR`, `DECR`,
`EXPIRE`, `PERSIST` - `SAVE` itself doesn't count) is appended verbatim to
the append-only file (`<dir>/<appendfilename>`) as it happens. On
startup, TinyKV loads the most recent snapshot (`<dir>/<dbfilename>`, if
any) and then replays the AOF on top of it, reconstructing the dataset
exactly as it was. A background timer flushes the AOF to disk roughly
once a second and, every `save_interval` seconds, takes a fresh snapshot
and resets the AOF (the snapshot already captures everything the AOF
would have replayed, so there's no need to replay both). `SAVE` does the
same thing on demand.

The snapshot format is literally a sequence of `SET key value` lines -
the same format the AOF and the wire protocol already use - so loading
either one is just replaying it through the normal parser/executor
pipeline. This keeps persistence consistent with the rest of the
project's "simple text protocol" design, at the cost of not persisting
TTLs: a key's expiration is in-memory only and does not survive a
restart (a replayed `SET ... EX` line, if one is still in the AOF, does
restore a TTL, but relative to replay time, not the original deadline).

`INCR`/`DECR` fail with `-ERR value is not an integer or out of range` if
the key holds a value that can't be parsed as an integer. The read,
modify, and write happen under a single lock in `KVStore::incrementBy`,
so concurrent `INCR`s on the same key can't lose an update the way a
separate `GET` then `SET` from the client would.

### Replication

`REPLICAOF host port` connects to a primary and reuses `SYNC`, plus the
same idea behind the snapshot format above: a replica connection sends
the single word `SYNC`, and the primary responds not with one reply line
but with an open-ended stream - first a burst of `SET key value` lines
covering the whole current dataset, then, with no separator or framing
between the two, every future write line as it happens. The replica
doesn't need to tell them apart: both are just command lines, and
replaying either one through the normal parser/executor pipeline
reconstructs the same state. This is also why replication has no extra
config or CLI flags - `REPLICAOF`/`REPLICAOF NO ONE` are the only
interface, matching how the vision doc's own example describes it.

A replica rejects direct client writes (see the command table above) but
still applies everything it receives from its primary, and durably
records it to its own AOF/snapshot the same way a primary would - a
replica can be killed and restarted and recover its own state without
needing the primary to still be reachable. It also propagates what it
applies to any replicas of its own, so chaining works without special
handling.

If a replica loses its connection to the primary, it retries once a
second until it reconnects (logged, not fatal). `REPLICAOF NO ONE`
disconnects from the current primary and returns to being a normal,
writable primary.

A plain `SET key value` (no `EX`) clears any existing TTL on the key, matching
the everyday expectation that overwriting a key resets it completely.
`INCR`/`DECR`, which modify a value in place rather than replacing the
key, leave an existing TTL untouched. Expired keys are removed by a
background sweeper (see `ExpiryManager`) that sleeps until the next real
deadline rather than polling; a `TTL` of `0` or a negative `seconds`
argument to `SET ... EX`/`EXPIRE` is rejected as an error rather than
supported.

### Sharding & routing

`tinykv-router` is a separate executable that sits in front of a fixed
set of shards declared in `topology.conf` (see Fault tolerance below),
each shard being one or two ordinary, unmodified `tinykv-server`
instances. The router speaks the exact same wire protocol as a normal
server, so an existing client can't tell the difference except for two
extra commands (`ROUTE`, `NODES`):

- For any command whose first argument is a key (`SET`, `GET`, `DEL`,
  `INCR`, `DECR`, `TTL`, `EXPIRE`, `PERSIST`), the router extracts that
  key, decides which shard owns it and which physical node is currently
  serving that shard, and transparently forwards the exact request line
  to it, relaying the reply back unmodified. The client never talks to a
  shard directly.
- `ROUTE key` doesn't forward anything - it just answers with the
  `host:port` of whichever node is currently serving the shard that owns
  `key`, using the identical decision the forwarding path itself uses
  (they share one `addressForKey()`, so the two can't disagree). Handy
  for verifying where a key actually lives - and, after a failover (see
  below), for seeing that the answer has changed.
- `PING` is answered locally by the router (it carries no key to route
  by). Any other command with no arguments, `ROUTE`/`NODES` with the
  wrong arity, or an unavailable node produces a normal `-ERR ...` reply
  rather than dropping the connection.

Which shard owns which keys is decided by a **consistent hash ring**
(`ConsistentHashRing`, keyed by a 64-bit FNV-1a hash with a
MurmurHash3-style finalizing mix for better bit diffusion on short keys):
each shard is given 150 "virtual node" positions scattered around the
ring, and a key belongs to the nearest virtual node clockwise from the
key's own hash position. This is the reason to prefer consistent hashing
over naive `hash(key) % shard_count`: adding or removing a shard only
remaps the fraction of keys that fell between that shard's virtual
positions and their new neighbors, not the entire keyspace. Critically,
the ring hashes **shard ids** (`shard0`, `shard1`, ...), not physical
addresses - which node is currently serving a shard can change (see
failover below) without changing which shard a key belongs to, so a
failover never causes a key to remap to a different shard, only to a
different node serving the same shard.

The shard list is static, read once from `topology.conf` at router
startup - there's no dynamic membership or automatic data rebalancing.
Adding a 4th shard to a running deployment means restarting the router
with an updated topology; existing data physically stays on whichever
shard originally received it, so a small number of keys will (correctly,
if perhaps surprisingly) appear to "disappear" under the new routing
until they're re-written.

The router keeps one persistent, mutex-guarded connection per shard
(reused across all client connections) rather than opening a fresh
connection per request; a broken connection is dropped and transparently
reopened on the next request routed to that shard - including after a
failover moves the shard's active address, which the connection notices
and reconnects to automatically.

### Fault tolerance & failover

Each shard in `topology.conf` can declare a `<shardId>.replica` in
addition to its `<shardId>.primary` - an ordinary Phase 10A replica of
that primary, wired up the normal way (`REPLICAOF <primary host> <primary
port>` run against it directly, outside the router entirely, before or
after the router starts). A shard with no configured replica still
routes normally; it just has nothing to fail over to.

The router runs a background `HealthChecker` thread that, every
`health_check_interval_ms` (`router.conf`, default 1000), opens one
short-lived connection to each shard's primary and replica and sends a
single `PING`. A node's health flips to DOWN after `max_missed_pings`
(default 3) *consecutive* failed pings, and back to UP on the very next
successful one.

When a shard's primary is DOWN and its replica is UP and hasn't already
been promoted for this failure, the router itself sends that replica
`REPLICAOF NO ONE` (turning it into an independent, writable primary)
and flips the shard's active address to it - all client traffic for that
shard's keys is transparently redirected from that point on, with no
client-visible interruption beyond the detection window itself.

This is deliberately **single-coordinator failover**: the router is the
sole authority making this decision, with no quorum, no consensus
protocol (no Raft/Paxos), and no split-brain protection beyond one rule -
**a promotion never reverses itself automatically**. A primary that comes
back up after being marked DOWN is reported as UP again in `NODES`, but
the router keeps routing to the promoted replica regardless; auto-
reverting would risk both the old primary and the promoted replica
believing they're simultaneously authoritative for the same shard. Un-
doing a failover (e.g. after restoring or replacing the failed primary)
is a manual operation: point the recovered node at the new primary with
`REPLICAOF <new primary host> <new primary port>` run directly against
it, outside the router.

`NODES` reports every physical node the router knows about, one
semicolon-separated entry per node, each formatted
`<shardId>:<PRIMARY|REPLICA>:<host:port>:<UP|DOWN>:<ACTIVE|STANDBY>` -
e.g. `shard0:PRIMARY:127.0.0.1:6381:DOWN:STANDBY;shard0:REPLICA:127.0.0.1:6391:UP:ACTIVE`
after the scenario above. `ACTIVE` marks whichever node is currently
serving that shard's traffic (initially always the primary); it is the
same address `ROUTE` would return for any key belonging to that shard.

Wrong argument counts and unknown commands both produce a `-ERR ...`
reply; the connection is never dropped because of bad input.

## Example session

```
> SET username Kushal
< +OK
> GET username
< +Kushal
> GET missing
< $-1
> DEL username
< :1
> PING
< +OK
> BOGUS
< -ERR unknown command 'BOGUS'
```
