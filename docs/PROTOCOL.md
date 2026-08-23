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

## Commands (as of Phase 4)

| Command         | Arguments   | Reply                                       |
|-----------------|-------------|-----------------------------------------------|
| `SET key value` | key, value  | `+OK`                                          |
| `GET key`       | key         | `+<value>`, or `$-1` if the key doesn't exist  |
| `DEL key`       | key         | `:1` if the key existed, else `:0`             |
| `PING`          | (none)      | `+OK`                                           |

Wrong argument counts and unknown commands both produce a `-ERR ...`
reply; the connection is never dropped because of bad input. More
commands (`EXISTS`, `TTL`, `EXPIRE`, ...) are added in later phases and
will be documented here as they land.

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
