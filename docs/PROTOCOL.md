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

## Commands (as of Phase 10A)

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
