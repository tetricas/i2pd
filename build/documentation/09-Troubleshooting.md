# Troubleshooting Guide - i2pd File Transfer System

**Purpose**: Common issues and solutions
**Audience**: Support engineers, administrators, developers
**Level**: Operational

---

## ⚠️ IMPORTANT: Architecture Context

This guide covers troubleshooting for **TWO ARCHITECTURES**:

### 🟢 **PRODUCTION** - tunnel_ftp_demo (USE THIS)
- **Tool**: `tunnel_ftp_demo` (tunnel-based)
- **Performance**: 5-6 MB/s (100MB), 8-9 MB/s (1GB)
- **Status**: ✅ Production-ready
- **Issues**: Focus troubleshooting here

### 🔴 **LEGACY** - chunked_file_test (DEPRECATED)
- **Tool**: `chunked_file_test` (streaming-based)
- **Performance**: 2.5-4 MB/s maximum
- **Status**: ❌ DEPRECATED
- **Issues**: Historical reference only

**For production deployments**: Troubleshoot `tunnel_ftp_demo`

---

## 🟢 Production Issues (tunnel_ftp_demo)

### Issue 1: Transfer Timeout / Connection Failure

**Symptoms**:
- Client cannot connect to server
- "Connection timeout" errors
- Transfer never starts

**Diagnosis**:
```bash
# Check if server is running
ps aux | grep tunnel_ftp_server

# Check server B32 address
cat server_b32.txt

# Check i2pd tunnels built
grep "Tunnel.*created" /tmp/i2pd.log | wc -l
# Should show >= 10 (10+10 tunnel pool)

# Check datagram destination
grep "DatagramDestination" /tmp/i2pd.log
```

**Solutions**:

1. **Verify server is running**:
```bash
# Start server
cd /path/to/tunnel_ftp_demo
./tunnel_ftp_server --port 7000 &

# Verify
ps aux | grep tunnel_ftp_server
```

2. **Check B32 address distribution**:
```bash
# Server should auto-publish via HTTPS (Session 20)
curl -k https://<server_ip>:7071/router.info

# Or use manual B32
SERVER_B32=$(cat server_b32.txt)
./tunnel_ftp_client --server "$SERVER_B32" --file test.bin
```

3. **Wait for tunnel build**:
```bash
# 10+10 tunnels take 60-90 seconds to build
sleep 90
./tunnel_ftp_client ...
```

4. **Check tunnel pool size**:
```bash
# Should see 10 inbound + 10 outbound
grep "Tunnel pool" /tmp/i2pd.log
# If fewer than 10, throughput will be limited
```

---

### Issue 2: Slow Transfer Speed (<5 MB/s)

**Symptoms**:
- Transfer speed < 4 MB/s
- Expected 5-9 MB/s but getting much less
- Performance degraded from baseline

**Diagnosis**:
```bash
# Check tunnel count
grep "Tunnels.*active" /tmp/i2pd.log
# Should show 10+ active tunnels

# Check for rate limiting
grep "Rate limit" /tmp/server.log

# Check chunk send rate
grep "Sending chunk" /tmp/server.log | tail -20
```

**Solutions**:

1. **Verify 10+10 tunnel pool** (Session 19):
```bash
# Server should create 10 inbound + 10 outbound
grep "Creating tunnel pool.*10" /tmp/i2pd.log

# If using default 2+2, performance will be ~2 MB/s max
```

2. **Check rate limiting**:
```bash
# Production uses 8 MB/s target rate
grep "TARGET_THROUGHPUT" tunnel_ftp_server

# Should be ~8.0, not lower
```

3. **Verify SSU2 preference** (Session 18):
```bash
# Should prefer SSU2 (UDP) over NTCP2 (TCP)
grep "SSU2.*session" /tmp/i2pd.log

# If mostly NTCP2, performance will be suboptimal
```

---

### Issue 3: Memory Leak / Growing Memory

**Symptoms**:
- i2pd memory grows continuously
- Eventually crashes with OOM
- Memory doesn't stabilize

**Diagnosis**:
```bash
# Monitor i2pd memory
watch -n 5 'ps aux | grep i2pd | grep -v grep'

# Check for Session 19 fixes
grep "Transit message.*limit" /tmp/i2pd.log
grep "Fragment cleanup" /tmp/i2pd.log
```

**Solutions**:

1. **Verify Session 19 memory leak fixes applied**:
```bash
# Check i2pd source for fixes:
# - Transit message list bounds
# - I2NP fragment cleanup
# - Memory pool management
# - Tunnel queue limits

# Rebuild i2pd with Session 19 patches
cd ~/i2pd
git log --oneline | grep -i "memory\|leak"
```

2. **Monitor memory during transfer**:
```bash
# Memory should stabilize, not grow
PID=$(pgrep i2pd)
for i in {1..20}; do
    ps -o rss= -p $PID
    sleep 30
done
# Should remain roughly constant
```

---

### Issue 4: Data Corruption / CRC32 Failure

**Symptoms**:
- "CRC32 mismatch" errors
- Received file differs from sent
- Transfer fails verification

**Diagnosis**:
```bash
# Check CRC32 verification logs
grep "CRC32" /tmp/client.log

# Check chunk integrity
grep "chunk.*integrity" /tmp/i2pd.log
```

**Solutions**:

1. **Verify 31KB chunk size** (Session 18):
```bash
# Chunks must be exactly 31744 bytes (31KB)
grep "CHUNK_SIZE.*31744" tunnel_ftp_server.cpp

# Incorrect size causes tunnel fragmentation
```

2. **Check tunnel message capacity**:
```bash
# Tunnel messages support ~32KB
# 31KB data + 12 byte header = 31756 bytes total
# Must fit in single tunnel message
```

3. **Retransfer with verification**:
```bash
# Generate test file with known hash
dd if=/dev/urandom of=test.bin bs=1M count=10
sha256sum test.bin > test.bin.sha256

# Transfer
./tunnel_ftp_client --server "$SERVER_B32" --file test.bin

# Verify
sha256sum -c test.bin.sha256
```

---

### Issue 5: HTTPS Reseed Failed (Session 20)

**Symptoms**:
- Client cannot fetch router.info
- "Certificate pinning failed" errors
- Manual router.info distribution required

**Diagnosis**:
```bash
# Check HTTPS server
curl -k https://<server_ip>:7071/router.info

# Check certificate hash
cat /var/lib/i2pd-ff/httpd.crt_hash.txt

# Check client config
grep "cert.*hash" /etc/i2pd/peer.conf
```

**Solutions**:

1. **Verify HTTPS server running**:
```bash
# Server should auto-start HTTPS on port 7071
grep "HTTPS.*7071" /tmp/server.log

# Test endpoint
curl -k https://localhost:7071/router.info | head -c 100
# Should return binary data
```

2. **Update certificate hash** (Session 20):
```bash
# Get correct hash from server
SERVER_HASH=$(ssh server "cat /var/lib/i2pd-ff/httpd.crt_hash.txt")

# Update client config
sed -i "s/cert = .*/cert = $SERVER_HASH/" /etc/i2pd/peer.conf

# Restart client
systemctl restart i2pd-peer
```

3. **Fallback to no-verification mode**:
```bash
# Temporary workaround (less secure)
[reseed]
url = https://<server_ip>:7071/router.info
verify = false  # Disables certificate pinning
```

---

## 🔴 Legacy Issues (chunked_file_test - DEPRECATED)

**⚠️ WARNING**: The following sections describe issues with DEPRECATED streaming-based architecture. Use tunnel_ftp_demo for production.

## Common Issues

### Issue 1: Transfer Timeout / Hangs

**Symptoms**:
- Client hangs waiting for chunks
- Logs show "Timeout receiving chunk"
- Transfer never completes

**Diagnosis**:
```bash
# Check if tunnels are built
grep "Tunnel created" /tmp/i2pd.log | wc -l
# Should show >= 2

# Check if destination is reachable
grep "Stream created" /tmp/client.log

# Check SavedPackets queue
grep "SavedPackets" /tmp/i2pd.log | tail -10
```

**Solutions**:

1. **Wait longer for network**:
```bash
# Allow 30-60 seconds after starting routers
sleep 60
./chunked_file_test client ...
```

2. **Reduce hop count**:
```bash
# Try 0-hop first
./chunked_file_test server --hops 0 ...
./chunked_file_test client --hops 0 ...
```

3. **Check NetDb population**:
```bash
ls /tmp/i2pd-peer/netDb/r*/ | wc -l
# Should be > 1
```

4. **Restart with cleanup**:
```bash
pkill -f i2pd
rm -rf /tmp/i2pd-*
# Manually restart i2pd instances as needed
i2pd --datadir=/tmp/i2pd-ff --floodfill=true &
i2pd --datadir=/tmp/i2pd-peer1 &
```

---

### Issue 2: "Cannot connect to server"

**Symptoms**:
- Client cannot establish stream
- Error: "Failed to create stream"
- B32 address not found

**Diagnosis**:
```bash
# Check server is running
ps aux | grep chunked_file_test | grep server

# Check B32 address saved
cat /tmp/server_b32.txt

# Check server destination created
grep "Destination created" /tmp/server.log
```

**Solutions**:

1. **Verify B32 address**:
```bash
# Server should print B32
cat /tmp/server_b32.txt
# Example: abc123...xyz.b32.i2p
```

2. **Manual B32 specification**:
```bash
./chunked_file_test client \
  --server-b32 "$(cat /tmp/server_b32.txt)" \
  --file test.bin
```

3. **Check server logs**:
```bash
tail -f /tmp/server.log
# Should show "Waiting for connections"
```

---

### Issue 3: Slow Transfer Speed

**Symptoms**:
- Transfer speed < 1 MB/s
- Expected 3-4 MB/s but getting much less
- Long pauses between chunks

**Diagnosis**:
```bash
# Check SavedPackets accumulation
grep "SavedPackets" /tmp/i2pd.log | tail -20

# Check for buffering delays
grep "High-throughput detected" /tmp/i2pd.log

# Check flow control
grep "Flow control" /tmp/client.log
```

**Solutions**:

1. **Verify Session 15 optimizations active**:
```bash
# Check for 16K limit
grep "SavedPackets limit reached" /tmp/i2pd.log

# Check for 4K batch processing
grep "High-throughput detected" /tmp/i2pd.log
```

2. **Check network detection**:
```bash
# Should detect local network
grep "Local network detected" /tmp/client.log
```

3. **Reduce hop count**:
```bash
# 0-hop for maximum speed (local testing)
./chunked_file_test ... --hops 0
```

---

### Issue 4: Segmentation Fault / Crash

**Symptoms**:
- i2pd process crashes
- "Segmentation fault" in logs
- Core dump generated

**Diagnosis**:
```bash
# Check for core dump
ls -lh core.*

# Check memory usage before crash
grep "memory" /var/log/syslog

# Check SavedPackets size
grep "SavedPackets" /tmp/i2pd.log | tail -50
```

**Solutions**:

1. **Verify 16K limit active**:
```bash
# Check Streaming.cpp line 377
grep -A 3 "16384" ~/i2pd/libi2pd/Streaming.cpp

# Should show:
# if (m_SavedPackets.size() >= 16384) {
```

2. **Rebuild with Session 15 fixes**:
```bash
cd ~/i2pd/cmake-debug
make clean
make -j$(nproc)
```

3. **Reduce file size**:
```bash
# Test with smaller files first
./chunked_file_test client --file test_10mb.bin
```

---

### Issue 5: Data Corruption

**Symptoms**:
- SHA-256 verification fails
- "Checksum mismatch" error
- Received file differs from sent

**Diagnosis**:
```bash
# Check if ReadSome() is being used
grep "ReadSome" ~/i2pd/tests_client/filetransfer/ChunkedFileClient.cpp

# Check packet ordering
grep "out of order" /tmp/i2pd.log
```

**Solutions**:

1. **Verify using Receive() not ReadSome()**:
```bash
# ChunkedFileClient.cpp should use:
# stream->Receive(buffer, len, timeout)
# NOT:
# stream->ReadSome(buffer, len)
```

2. **Check Session 15 fixes**:
```bash
# Ensure hybrid reading strategy active
grep "CHUNK_TIMEOUT" ~/i2pd/tests_client/filetransfer/ChunkedFileClient.cpp
```

---

### Issue 6: "Reseed Failed" / "No Floodfill"

**Symptoms**:
- Peer cannot bootstrap
- "Reseed: failed" in logs
- Empty NetDb

**Diagnosis**:
```bash
# Check floodfill accessible
curl -k https://<FF_IP>:7071/router.info | head -c 100

# Check certificate hash
cat /var/lib/i2pd-ff/httpd.crt_hash.txt

# Check peer configuration
grep "reseed" /etc/i2pd/peer.conf
```

**Solutions**:

1. **Verify HTTPS endpoint**:
```bash
# Test HTTPS connection
curl -k https://10.114.0.2:7071/router.info

# Should return binary data (router.info)
```

2. **Check certificate pinning**:
```bash
# Get correct hash from floodfill
FF_HASH=$(sudo cat /var/lib/i2pd-ff/httpd.crt_hash.txt)

# Update peer config
sudo sed -i "s/cert = .*/cert = $FF_HASH/" /etc/i2pd/peer.conf
```

3. **Fallback to manual router.info**:
```bash
# Copy router.info manually
sudo cp /var/lib/i2pd-ff/router.info /tmp/ff-router.info

# Update peer config
[reseed]
floodfill = /tmp/ff-router.info
verify = false
```

---

## Performance Issues

### Issue: Chunk Processing Delays

**Symptoms**:
- Chunks process every 20+ seconds
- "Processing saved packets" delays
- Transfer takes 10x longer than expected

**Solutions**:

1. **Verify 4K batch processing**:
```bash
grep "MAX_BATCH_SIZE.*4096" ~/i2pd/libi2pd/Streaming.cpp
```

2. **Check auto-trigger active**:
```bash
grep "1024" ~/i2pd/libi2pd/Streaming.cpp
# Should trigger processing at 1024+ packets
```

3. **Monitor during transfer**:
```bash
watch -n 1 'grep "High-throughput detected" /tmp/i2pd.log | tail -5'
```

---

### Issue: Low Throughput on Local Network

**Symptoms**:
- Local transfer < 5 MB/s
- Expected 10+ MB/s
- Network is 1Gbps+

**Solutions**:

1. **Check network detection**:
```bash
grep "NetworkDetector" /tmp/client.log
# Should detect local network
```

2. **Verify no bandwidth cap**:
```bash
grep "bandwidth" /etc/i2pd/peer.conf
# Should be "X" or high value
```

3. **Check flow control delays**:
```bash
grep "Flow control delay" /tmp/client.log
# Should be 5μs for local
```

---

## Logging & Debugging

### Enable Debug Logging

```bash
# Edit i2pd.conf
[log]
level = debug
file = /tmp/i2pd_debug.log

# Restart i2pd
sudo systemctl restart i2pd-peer

# Monitor
tail -f /tmp/i2pd_debug.log
```

### Structured Log Analysis

```bash
# Extract errors
grep -i "error" /tmp/i2pd.log | sort | uniq -c

# Extract warnings
grep -i "warn" /tmp/i2pd.log | tail -20

# Analyze transfer performance
grep "throughput\|MB/s" /tmp/client.log
```

---

## Network Issues

### Issue: Tunnels Not Building

**Diagnosis**:
```bash
# Check tunnel attempts
grep "Building.*tunnel" /tmp/i2pd.log

# Check failures
grep "Tunnel.*failed" /tmp/i2pd.log

# Check router connectivity
grep "SSU2.*Session with" /tmp/i2pd.log
```

**Solutions**:

1. **Check NetDb**:
```bash
ls /tmp/i2pd-peer/netDb/r*/ | wc -l
# Need at least 3-5 routers for tunnels
```

2. **Wait for router discovery**:
```bash
# Takes 60-120 seconds
sleep 120
```

3. **Check network connectivity**:
```bash
# Verify UDP ports accessible
nc -u -v <peer_ip> 9111
```

---

## Emergency Procedures

### Complete System Reset

```bash
# Complete system reset procedure

echo "=== EMERGENCY RESET ==="

# Stop all services
sudo systemctl stop file-transfer-server
sudo systemctl stop i2pd-peer
sudo systemctl stop i2pd-ff

# Clean all data
sudo rm -rf /var/lib/i2pd-*
sudo rm -rf /tmp/i2pd-*
sudo rm -f /tmp/*.log
sudo rm -f /tmp/server_b32.txt

# Regenerate certificates
sudo rm -f /var/lib/i2pd-ff/httpd.*

# Restart floodfill
sudo systemctl start i2pd-ff
sleep 30

# Copy new certificate hash
FF_HASH=$(sudo cat /var/lib/i2pd-ff/httpd.crt_hash.txt)

# Update peer configs
for conf in /etc/i2pd/peer*.conf; do
    sudo sed -i "s/cert = .*/cert = $FF_HASH/" $conf
done

# Restart peers
sudo systemctl start i2pd-peer
sleep 60

# Verify network
ls /var/lib/i2pd-peer/netDb/r*/ | wc -l

echo "=== RESET COMPLETE ==="
```

---

## Getting Help

### Information to Collect

```bash
# Collect diagnostic information

DIAG_DIR="diagnostic_$(date +%Y%m%d_%H%M%S)"
mkdir -p $DIAG_DIR

# System info
uname -a > $DIAG_DIR/system.txt
free -h >> $DIAG_DIR/system.txt
df -h >> $DIAG_DIR/system.txt

# i2pd info
./i2pd --version > $DIAG_DIR/version.txt
ps aux | grep i2pd > $DIAG_DIR/processes.txt

# Logs
cp /tmp/i2pd.log $DIAG_DIR/ 2>/dev/null
cp /tmp/client.log $DIAG_DIR/ 2>/dev/null
cp /tmp/server.log $DIAG_DIR/ 2>/dev/null

# Configuration
cp /etc/i2pd/*.conf $DIAG_DIR/ 2>/dev/null

# Network state
netstat -tulpn | grep i2pd > $DIAG_DIR/network.txt

# Create archive
tar -czf $DIAG_DIR.tar.gz $DIAG_DIR
echo "Diagnostic info saved to $DIAG_DIR.tar.gz"
```

---

**Document Version**: 1.0
**Last Updated**: December 2025
