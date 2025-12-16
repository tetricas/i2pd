# Quick Start Guide - i2pd File Transfer System

**Goal**: Get tunnel_ftp_demo running in 15-30 minutes  
**Tool**: `tunnel_ftp_demo` (production system)  
**Audience**: First-time users  
**Prerequisites**: Linux system with root/sudo access

---

## Overview

This guide will help you:
1. Set up a basic 3-node I2P network (1 floodfill + 2 peers)
2. Transfer files using **tunnel_ftp_demo** (production tool)
3. Achieve **5-9 MB/s** performance in cloud environments

**Time Estimate**: 15-30 minutes

**Note**: This guide uses `tunnel_ftp_demo`, the production tool optimized for cloud deployments. For development history with `chunked_file_test` (experimental streaming-based version), see [Development History](03-Development-History.md).

---

## Step 1: Prerequisites Check

### System Requirements

```bash
# Check disk space (need ~5GB)
df -h /

# Check RAM (need ~2GB)
free -h

# Install dependencies
sudo apt-get update
sudo apt-get install -y build-essential cmake libboost-all-dev libssl-dev git
```

### Verify i2pd Build

```bash
# Navigate to i2pd directory, for FF and peer
cd ~/i2pd

# Check if already built
ls -la cmake-debug/i2pd

# If not built, build it
mkdir -p cmake-debug
cd cmake-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

### Build tunnel_ftp_demo (Production Tool)

```bash
# Navigate to tests_client
cd ~/i2pd/tests_client

# Build
mkdir -p build
cd build
cmake ..
make -j$(nproc)

# Verify binary exists
ls -la tunnel_ftp_demo
# Should show executable file (~29MB)
```

---

## Step 2: Configure Network

### Create Configuration Directory

```bash
mkdir -p ~/i2pd-conf
mkdir -p /tmp/i2pd-ff /tmp/i2pd-peer1 /tmp/i2pd-peer2
```

### Floodfill Configuration

All descriptions for each config option could be found at libi2pd/Config.cpp.

Create `~/i2pd-conf/ff.conf`:

```ini
netid = 123             # Local-only, no public Internet (default 2 for base routers)
floodfill = true
bandwidth = X             # no limit, or K if you want minimal

## Bind only to loopback
ipv4 = true
ipv6 = false
nat = false
ifname4 = lo0

address4 = 127.0.0.1
host = 127.0.0.1
reservedrange = false       # private networks
loglevel=debug

[ntcp2]
enabled   = true
port      = 4570
published = true

[ssu2]
enabled   = true
port      = 4670
published = true

[reseed] # No public reseed, disabling all we don't need
verify = false
urls =
file =
zipfile =
threshold = 0

[exploratory]       # keep-alive tunnels for infra
inbound.length=0
outbound.length=0
inbound.quantity=1
outbound.quantity=1

[http]
enabled=true
address = 0.0.0.0
ssl = true

# disable all extra stuff below
[addressbook]
enabled = false

[socksproxy]
enabled = false

[httpproxy]
enabled=false

[sam]
enabled=false

```

### Peer Configuration  

Create `~/i2pd-conf/client.conf`:

```ini
netid = 123
notransit = false
floodfill = false
bandwidth = X

ipv4 = true
ipv6 = false
nat = false
ifname4 = lo0

address4 = 127.0.0.1
host = 127.0.0.1
reservedrange = false

[ntcp2]
enabled   = true
port 	  = 4568
published = true

[ssu2]
enabled   = true
port 	  = 4668
published = true

[reseed]
verify = true
urls   =
yggurls=
threshold = 0
file =
floodfill = https://127.0.0.1:7071/router.info
cert = bed4cf9f102f0df065255f396b2eec7332b0d0a840b10b52d98a40d27754824f

[trust]
enabled = false

[exploratory]
inbound.length=0
outbound.length=0
inbound.quantity=1
outbound.quantity=1

[addressbook]
enabled = false

[socksproxy]
enabled = false

[http]
enabled = false

[httpproxy]
enabled=false

[sam]
enabled = false

```

---

## Step 3: Start I2P Network

```bash
# Terminal 1: Start floodfill
cd ~/i2pd/cmake-debug
./i2pd --datadir=/tmp/i2pd-ff --conf=~/i2pd-conf/ff.conf

# Terminal 2: Start peer1
./i2pd --datadir=/tmp/i2pd-peer1 --conf=~/i2pd-conf/peer1.conf

# Terminal 3: Start peer2
./i2pd --datadir=/tmp/i2pd-peer2 --conf=~/i2pd-conf/peer2.conf
```

---

## Step 4: Transfer Files with tunnel_ftp_demo

### Terminal 4: Start Server

```bash
cd ~/i2pd/tests_client/build

# Create server config
cat > srv.conf << 'EOF'
# Minimal i2p config
EOF

# Start server with medium file (100MB)
./tunnel_ftp_demo server \
  --conf srv.conf \
  --datadir /tmp/srv \
  --hops 1 \
  --filesize medium

# Server will print:
# Server B32 address: abc123...xyz.b32.i2p
# Saved to: /tmp/server_b32.txt
# Test file created: /tmp/testfile.bin (100MB)
# Server listening on tunnel...
```

### Terminal 5: Start Client

```bash
cd ~/i2pd/tests_client/build

# Create client config  
cat > cli.conf << 'EOF'
# Minimal i2p config
EOF

# Wait 5-10 seconds for server initialization

# Get server address
SERVER_B32=$(cat /tmp/server_b32.txt)

# Start client transfer
./tunnel_ftp_demo client \
  --conf cli.conf \
  --datadir /tmp/cli \
  --hops 1 \
  --server-b32 "$SERVER_B32"

# Output will show:
# Connecting to server...
# File transfer started
# [Progress updates]
# Transfer completed: 100.00 MB in 18.5 seconds
# Throughput: 5.4 MB/s
# CRC32 verified: PASS
```

---

## Step 5: Verify Results

### Check Transfer Success

```bash
# Client logs
grep "Transfer completed" /tmp/cli/i2pd.log

# Expected:
# Transfer completed: 100.00 MB in ~18 seconds
# Throughput: 5-6 MB/s
# CRC32 verified: PASS
```

### Verify File Integrity

```bash
# Compare CRC32
echo "Server file:"
crc32 /tmp/testfile.bin

echo "Received file:"
crc32 /tmp/received_file.bin

# Should match
```

### Performance Metrics

Expected for **tunnel_ftp_demo** in cloud:
- **10MB (small)**: 3-5 MB/s (2-3 seconds)
- **100MB (medium)**: 5-6 MB/s (17-20 seconds)
- **1GB (large)**: 8-9 MB/s (~2 minutes)

---

## Step 6: Test Different Configurations

### Test Large File (1GB)

```bash
# Server
./tunnel_ftp_demo server \
  --conf srv.conf \
  --datadir /tmp/srv-large \
  --hops 1 \
  --filesize large

# Client
./tunnel_ftp_demo client \
  --conf cli.conf \
  --datadir /tmp/cli-large \
  --hops 1 \
  --server-b32 "$(cat /tmp/server_b32.txt)"

# Expected: 8-9 MB/s throughput (~2 minutes for 1GB)
```

---

## Comparison: tunnel_ftp_demo vs chunked_file_test

### Why tunnel_ftp_demo?

**tunnel_ftp_demo** (Production):
- ✅ Cloud-optimized: 5-9 MB/s in cloud environments  
- ✅ Tunnel/datagram-based communication
- ✅ Simpler protocol, better performance
- ✅ Production ready

**chunked_file_test** (Experimental):
- 📚 Streaming-based (Sessions 01-16)
- 📚 Good local performance
- ❌ Couldn't achieve peak speed in cloud
- 📚 Valuable development foundation

**Development Journey**: chunked_file_test experiments (streaming protocols, extensive optimizations) → insights → tunnel_ftp_demo (production system)

---

## Troubleshooting

### "Cannot connect to server"

```bash
# Verify server is running
ps aux | grep tunnel_ftp_demo | grep server

# Check B32 address saved
cat /tmp/server_b32.txt

# Ensure tunnels built (wait 20-30 seconds after starting i2pd)
```

### Slow Transfer Speed

```bash
# Check hop count (0 = fastest)
# Use --hops 0 for maximum speed

# Verify network connectivity
ping <peer_ip>

# Check tunnel creation
grep "Tunnel created" /tmp/i2pd-peer1/i2pd.log
```

---

## Summary

You have successfully:
- ✅ Built i2pd and tunnel_ftp_demo
- ✅ Configured a 3-node I2P network
- ✅ Transferred files with **5-9 MB/s** performance (cloud)
- ✅ Verified CRC32 integrity
- ✅ Tested multiple configurations

**Production Tool**: tunnel_ftp_demo achieves peak performance in cloud environments.

**Experimental Tool**: chunked_file_test provided valuable development foundation (see [Development History](03-Development-History.md)).

---

## Next Steps

1. **[Architecture Overview](02-Architecture-Overview.md)** - Understand tunnel vs streaming
2. **[Development History](03-Development-History.md)** - Complete evolution story
3. **[Deployment Guide](07-Deployment-Guide.md)** - Deploy in production
4. **[Performance Optimizations](05-Performance-Optimizations.md)** - How it works

---

**Document Version**: 1.0  
**Last Updated**: December 2025  
**Production Tool**: tunnel_ftp_demo
