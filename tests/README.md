# Test suite

Black-box tests: the suite behaves like a normal SOCKS5 user. It builds
hev-socks5-server, starts it with a YAML config, and talks to it with a small
SOCKS5 client written from the RFCs (`CONNECT`, `UDP ASSOCIATE`, the `FWD UDP`
extension, username/password auth). No internal symbols are touched.

```sh
tests/run.sh func              # functional tests, ~30 s, pass/fail
tests/run.sh bench             # benchmarks, report numbers only
tests/run.sh all               # both (two independent sessions)
tests/run.sh list              # list tests / benchmarks
```

Options: `-b BIN` use an existing binary (default: `make`), `--no-build` do
not run `make`, `-k NAME` select tests/benchmarks by substring (repeatable),
`-x` stop at first failure, `-v` verbose, `-d SECONDS` per-measurement
duration (default 3, `BENCH_DURATION`). Results are printed as they are
measured; when `GITHUB_STEP_SUMMARY` is set the tables are also appended to
the job summary.

Functional and benchmark runs are separate commands, so a CI job can run them
independently. The exit code is non-zero if a functional test fails.
Benchmarks only fail when they cannot run; numbers are reported, never
compared with history.

## Requirements (Ubuntu)

root (the script re-executes itself through `sudo`), Python >= 3.12, a C
compiler, `make`, `iproute2`; `nftables` is optional and only needed by the
socket mark tests (they are skipped without it). Everything runs in
throw-away network namespaces; nothing on the host is reconfigured.

```sh
sudo apt-get install -y iproute2 nftables python3 build-essential
```

## What is covered

| area | tests |
|---|---|
| TCP `CONNECT` | IPv4/IPv6 targets and clients, domain names, echo/upload digest/download, half-close, concurrency, idle sessions, refused/unresolvable target, reset, aborted handshakes, pipelined and byte-by-byte handshakes, `BIND`/unknown commands, garbage input |
| UDP | `UDP ASSOCIATE` and `FWD UDP`: IPv4/IPv6, payload sizes, full cone replies, domain targets, many targets per association, concurrency, lifetime tied to the control connection, foreign senders, garbage datagrams |
| authentication | username/password (valid, wrong, missing, long), auth file with several users, file precedence, `SIGUSR1` live reload (also with workers), per-user socket mark |
| config | `listen-address`, `listen-ipv6-only`, `port`, `workers`, `tcp-fastopen`, `udp-port` (fixed/range), `udp-listen-address`, `udp-public-address-v4/v6`, `bind-address[-v4/-v6]`, `bind-interface`, `domain-address-type`, `mark`, all `misc` timeouts, buffers, `task-stack-size`, `limit-nofile`, logging |
| process | usage/version, bad config files, `conf/main.yml`, `SIGINT`/`SIGTERM`/`SIGPIPE`/`SIGUSR1`, daemon + `pid-file` |
| benchmarks | TCP throughput + CPU (1 and 4 workers), UDP throughput/loss (UDP-in-UDP and UDP-in-TCP), TCP/UDP latency, connection rate (plain, pipelined, authenticated), memory per session |

## Layout

```
tests/run.sh               entry point
tests/hevtest/             framework
  main.py                  CLI driver / discovery
  platform/base.py         platform-neutral testbed definition (topology)
  platform/linux.py        Linux back-end (network namespaces + veth)
  client.py                SOCKS5 client (what an application would do)
  targets.py               echo/sink/source/reflect/whoami services
  case.py config.py benchlib.py report.py build.py
tests/functional/test_*.py unittest cases, one server instance per test
tests/bench/bench_*.py     benchmarks: run(bed, report, args)
```

Topology (Linux): a *client* namespace holds the test process (the user). The
*server* namespace runs hev-socks5-server and has two links: one to the
client (where the server listens) and one to the *remote* namespace. The
remote namespace holds the target services. The target addresses
(`203.0.113.10`, `2001:db8::10`) exist only in the remote namespace and are
routed only from the server namespace, so a connection can succeed only if it
really went through the server. The server also resolves a few names
(`target.test`, ...) through a private `/etc/netns/<ns>/hosts`.

## Adding a platform

Create `hevtest/platform/<name>.py` with a `Bed` class deriving from
`base.Bed` (set up the environments, `spawn_server_env`/`spawn_remote_env`,
`enter_client`, `wait_listen`, `watch_mark`/`mark_packets`, `proc_stats`,
`nofile_limit`, `listening`) and register it in
`hevtest/platform/__init__.py`. Tests and benchmarks use only the `Bed` API.
Then add an entry to the `Tests` matrix in `.github/workflows/ci.yml`.
Tests that cannot work on a platform call `self.require(cond, why)`.

## Adding a test

Functional: add `tests/functional/test_<x>.py`, derive from `ServerTestCase`,
call `self.server(main={...}, misc={...}, auth={...})` and use
`hevtest.client`. Benchmark: add `tests/bench/bench_<x>.py` with
`run(bed, rep, args)` using `benchlib.running(...)`, and list its name in
`BENCH_ORDER` in `hevtest/main.py` to control the order.
