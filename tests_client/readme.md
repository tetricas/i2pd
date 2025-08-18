
# Embedded I2P Stream Test — Overview & Guide

This document explains the purpose of the files under `tests_client/`, how the embedded test app works, and how to build and run a tiny two-node I2P “iso-net” (floodfill + server + client) over **NTCP2 only**.

---

## What’s in this folder?

```
tests_client/
├─ client.conf            # Node config for the client app (embedded router)
├─ server.conf            # Node config for the server app (embedded router)
├─ floodfill.conf         # Config for your local floodfill router
├─ embedded_stream_itest.cpp  # Single-file client/server test app (this doc explains it)
└─ CMakeLists.txt         # Minimal CMake target to build the test
```

### Config highlights

- **All three nodes run as embedded I2P routers** sharing an isolated NetDB.
- **NTCP2** is enabled; **SSU2 is disabled** (the code calls `InitTransports()` then starts NTCP2 only).
- To keep the iso-net deterministic, bandwidth caps are small and public IP checks are turned off; on loopback you should set `reservedrange=false` and `notransit=false`.
- For quick bring-up in a two-node net, the app forces **zero-hop per-destination tunnels** (see below).

---

## The test app at a glance (`embedded_stream_itest.cpp`)

The same binary runs in two modes:

- `server` – publishes a destination, accepts an incoming stream, **echoes** what it receives.
- `client` – resolves the server’s `b32`, opens a stream, sends `"hello"`, then waits for an echo.

### Key pieces in the code

- **CLI parsing**: `parseCli()` supports:
    - `server|client`
    - `--datadir DIR` (required)
    - `--conf FILE` (i2pd-style config file for this node)
    - `--server-b32 <b32>` (client only)
    - `--pingOnly` (sends a ping instead of payload; handy for connectivity checks)

- **Router bring-up**: `initNode()` + `start_core()`
    - Parses config, sets app data dir, initializes logging/FS.
    - Configures NetID, reserved range checks, bandwidth, floodfill/transit flags.
    - Starts NetDB → NTCP2 transports → tunnels → router context → client context.
    - `verifyNTCP2Published()` sanity-checks the NTCP2 address is present in RouterInfo.

- **Trust / allowlists**: `initTrust()`
    - Optional explicit trust (families / router lists) driven by config keys:
        - `trust.enabled=true|false`
        - `trust.family=...` / `trust.routers=...`
        - `trust.hidden=true|false` (hidden mode)
    - In this minimal iso-net, explicit trust isn’t required when using **0‑hop** destination tunnels.

- **Zero-hop destination tunnels**: `kZeroHop`
    - Forces both inbound and outbound **length=0**, **quantity=1**, **variance=0**.
    - Rationale: with only a couple of routers, 1-hop selection will fail with “no peers available”. Zero-hop avoids peer selection entirely and makes the demo deterministic.
    - The server sets `i2cp.dontPublishLeaseSet=false`; the client sets it to `true`.

- **Server flow**
    1. Creates a **public** destination with `kZeroHop`.
    2. Waits until `IsReady()` and NetDb can return its **LeaseSet**.
    3. Prints its address: `Server b32: <...>.b32.i2p`.
    4. `AcceptStreams(...)` → for each stream calls `recvLoop()` which logs, then **echoes**.

- **Client flow**
    1. Creates a **private** destination with `kZeroHop`.
    2. Resolves the server `--server-b32` to `IdentHash`; calls `RequestDestination(...)`.
    3. Repeatedly tries `CreateStream(...)` until a stream exists.
    4. **Kick the connect** with `stream->Send(nullptr, 0)`. This is intentional; it causes the streaming session to initiate even before app payload.
    5. Sends `"hello"`, then waits for the echo in `recvLoop()`.

- **recvLoop()**
    - Uses short per-read timeouts to log progress without hanging.
    - Server echoes back; client prints what it received.
    - Both sides close the stream explicitly.

---

## Why zero-hop for this demo?

In a tiny iso-net, tunnel peer selection often fails with messages like:

```
Tunnels: Can't create outbound tunnel, no peers available
```

Setting per-destination pools to **0-hop** removes peer selection from the equation, so your app traffic isn’t blocked behind tunnel builds. (Exploratory tunnels can still exist for NetDb and floodfill interaction.)

Snippet used by both client and server:

```cpp
std::map<std::string,std::string> kZeroHop{
  {"inbound.length","0"}, {"outbound.length","0"},
  {"inbound.lengthVariance","0"}, {"outbound.lengthVariance","0"},
  {"inbound.quantity","1"}, {"outbound.quantity","1"},
  {"i2cp.leaseSetEncType","0"}
};
```

Server adds: `i2cp.dontPublishLeaseSet=false`  
Client adds: `i2cp.dontPublishLeaseSet=true`

---

## Running the three nodes

1. **Start the floodfill** (separate terminal, main daemon):
   ```bash
   ./cmake-debug/i2pd --datadir=/tmp/i2pd-main --conf=/tmp/i2pd-main/i2pd.conf
   ```
   Make sure it binds NTCP2 and becomes reachable (watch logs).

2. **Start the server**:
   ```bash
   ./embedded_stream_itest server --datadir /tmp/i2pd-iso/srv --conf tests_client/server.conf --pingOnly
   ```
   Wait until it prints:
   ```
   Server b32: <server-address>.b32.i2p
   ```

3. **Start the client** (use the printed server b32):
   ```bash
   ./embedded_stream_itest client --datadir /tmp/i2pd-iso/cli --conf tests_client/client.conf --pingOnly --server-b32 <server-address>
   ```

---

## Reading the logs (what “good” looks like)

- NTCP2 handshake & RouterInfo:
    - `NTCP2: Start listening v4 TCP port ...`
    - `NTCP2 in RI: 127.0.0.1:...`

- LeaseSet flow (server):
    - `Destination: Publish LeaseSet ...` → `Publishing LeaseSet confirmed`

- Client connects:
    - `Stream status: <...>` then `Client send: empty to initiate connect`
    - `Server got: stream`

- Payload:
    - Client: Ping-pong messages
    - Server: Ping-pong messages

---

## Common pitfalls & fixes

- **“no peers available” when building destination tunnels**  
  Use the included **0-hop** settings (already in the app). In tiny nets, 1-hop often fails.

- **LeaseSet not found / publish too fast**  
  The server waits for readiness and a visible LeaseSet in NetDb before announcing its b32.

- **Stream appears connected but payload times out**  
  Keep the **`Send(nullptr, 0)`** “connect kick”. Without it, the session establishment can stall.

- **Explicit trust on a tiny net**  
  Not required for 0-hop. If you switch to 1-hop, either disable trust or explicitly whitelist peers.

---

## Reference: config keys used by the app

- `netid` - isolates your network.
- `reservedrange=false` - allow loopback/localhost addresses in RouterInfo.
- `bandwidth=K` - small bandwidth class; logs show computed KBps.
- `trust.enabled`, `trust.family`, `trust.routers`, `trust.hidden` - optional route restrictions/hidden mode.

---

## Limitations of this demo

- This is a **single-process-per-node** embedded router demo. It’s great for learning and CI, but it isn’t tuned for production routing.
- Zero-hop is for **testing only**. For realistic anonymity, increase hop counts and run with a healthier peer set.

---

## Appendix: Stream lifecycle (quick)

1. Resolve server b32 → `IdentHash`
2. Request LeaseSet (NetDb)
3. Create stream
4. Kick connect with `Send(nullptr, 0)`
5. Send ping
6. Send pong
7. Close stream

---

Happy testing! If you need a trimmed README variant for your repo, ping me with your exact folder layout and I’ll adapt this doc.
