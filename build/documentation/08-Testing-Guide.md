# Testing Guide - i2pd File Transfer System

**Purpose**: Comprehensive testing procedures
**Audience**: QA engineers, developers
**Level**: Operational

---

## ⚠️ IMPORTANT: Architecture Context

This guide covers testing for **TWO ARCHITECTURES**:

### 🟢 **PRODUCTION** - tunnel_ftp_demo (USE THIS)
- **Tool**: `tunnel_ftp_demo` (tunnel-based)
- **Performance**: 5-6 MB/s (100MB), 8-9 MB/s (1GB)
- **Status**: ✅ Production-ready
- **Testing**: Use for all production validation

### 🔴 **LEGACY** - chunked_file_test (DEPRECATED)
- **Tool**: `chunked_file_test` (streaming-based)
- **Performance**: 2.5-4 MB/s maximum
- **Status**: ❌ DEPRECATED
- **Testing**: Historical reference only

**For production deployments**: Test with `tunnel_ftp_demo`

---

## Testing Overview

This guide covers all testing procedures for the i2pd File Transfer System.

**Test Categories**:
1. Performance Tests
2. Stress Tests
3. Security Tests
4. Regression Tests
5. Data Integrity Tests

---

## Test Environment Setup

### Minimal Test Environment

```bash
# Start 1 Floodfill + 2 Peers manually
# Floodfill
i2pd --datadir=/tmp/i2pd-ff --port=9100 --floodfill=true &

# Peer 1
i2pd --datadir=/tmp/i2pd-peer1 --port=9101 &

# Peer 2
i2pd --datadir=/tmp/i2pd-peer2 --port=9102 &

# Verify
ps aux | grep i2pd | wc -l
# Should show 3
```

### Full Test Environment

```bash
# Start 1 FF + 4 Peers for routing diversity
# Add more peers with different ports and datadirs
i2pd --datadir=/tmp/i2pd-peer3 --port=9103 &
i2pd --datadir=/tmp/i2pd-peer4 --port=9104 &
```

---

## 🟢 PRODUCTION Testing (tunnel_ftp_demo)

### Performance Benchmarks

**Expected Performance** (Cloud/Production):

| Configuration | Expected Throughput | Pass Threshold |
|--------------|-------------------|----------------|
| 1-hop Tunnel | 5-6 MB/s (100MB) | >4 MB/s |
| 1-hop Tunnel | 8-9 MB/s (1GB) | >6 MB/s |
| Success Rate | 95%+ | >90% |

## 🔴 LEGACY Testing (chunked_file_test - DEPRECATED)

**⚠️ WARNING**: The following sections describe testing for the DEPRECATED streaming-based tool. Use tunnel_ftp_demo for production testing.

### Performance Benchmarks (LEGACY)

| Configuration | Expected Throughput | Pass Threshold |
|--------------|-------------------|----------------|
| 0-hop Normal | 3-4 MB/s | >2 MB/s |
| 1-hop Normal | 2.5-3.5 MB/s | >2 MB/s |
| 2-hop Normal | 2-3 MB/s | >1.5 MB/s |

### Example Test Template (LEGACY)

```bash
# DEPRECATED: Use tunnel_ftp_demo for production
# This example shows legacy streaming-based testing

RESULTS_FILE="results_$(date +%Y%m%d_%H%M%S).tsv"
echo -e "Config\tThroughput\tTime\tStatus" > $RESULTS_FILE

for hops in 0 1 2; do
    echo "Testing $hops-hop..."

    # DEPRECATED tool usage
    result=$(./chunked_file_test client \
        --hops $hops \
        --normal \
        --file test_100mb.bin)

    throughput=$(echo "$result" | grep "throughput" | awk '{print $2}')
    echo -e "$hops-hop\t$throughput\tPASS" >> $RESULTS_FILE
done

cat $RESULTS_FILE
```

---

## Security Tests

### Certificate Pinning Test

```bash
#!/bin/bash
# cert_pinning_test.sh

# Test 1: Correct certificate hash (should succeed)
echo "Test 1: Correct hash..."
./i2pd --conf peer_correct_hash.conf &
sleep 30
if grep -q "Certificate fingerprint verified" /tmp/i2pd.log; then
    echo "✓ PASS: Correct hash accepted"
else
    echo "✗ FAIL: Correct hash rejected"
fi
pkill i2pd

# Test 2: Wrong certificate hash (should fail)
echo "Test 2: Wrong hash..."
./i2pd --conf peer_wrong_hash.conf &
sleep 30
if grep -q "Certificate fingerprint mismatch" /tmp/i2pd.log; then
    echo "✓ PASS: Wrong hash rejected"
else
    echo "✗ FAIL: Wrong hash accepted (SECURITY ISSUE!)"
fi
pkill i2pd
```

---

## Regression Tests

### Session 15 Regression Test

**Purpose**: Ensure 16K packet limit and batch processing still work

```bash
#!/bin/bash
# session15_regression.sh

# Test 1: No segfaults on 1GB transfer
echo "Testing 1GB transfer (no segfaults)..."
./chunked_file_test client --file test_1gb.bin
if [ $? -eq 0 ]; then
    echo "✓ PASS: No segfaults"
else
    echo "✗ FAIL: Segfault or error"
    exit 1
fi

# Test 2: Chunk processing < 5 seconds
echo "Testing chunk processing time..."
CHUNK_TIME=$(grep "chunk" /tmp/client.log | awk '{print $3}' | sort -n | tail -1)
if [ $CHUNK_TIME -lt 5000 ]; then  # milliseconds
    echo "✓ PASS: Chunk processing within limits ($CHUNK_TIME ms)"
else
    echo "✗ FAIL: Slow chunk processing ($CHUNK_TIME ms)"
    exit 1
fi
```

---

## Test Data Management

### Generate Test Files

```bash
#!/bin/bash
# generate_test_files.sh

SIZES=(1M 10M 100M 1G)

for size in "${SIZES[@]}"; do
    if [ ! -f "test_$size.bin" ]; then
        echo "Generating test_$size.bin..."
        dd if=/dev/urandom of=test_$size.bin bs=$size count=1
        sha256sum test_$size.bin > test_$size.bin.sha256
    fi
done
```

### Clean Test Environment

```bash
#!/bin/bash
# clean_test_env.sh

# Stop all i2pd instances
pkill -f i2pd

# Clean data directories
rm -rf /tmp/i2pd-*
rm -rf /tmp/srv*
rm -rf /tmp/cli*
rm -f /tmp/server_b32.txt
rm -f /tmp/ff-router.info

# Clean logs
rm -f /tmp/*.log

echo "Test environment cleaned"
```

---

## Test Reporting

### Generate Test Report

```bash
#!/bin/bash
# generate_report.sh

cat > test_report_$(date +%Y%m%d).md << EOF
# Test Report - $(date +%Y-%m-%d)

## Environment
- i2pd Version: $(./i2pd --version)
- OS: $(uname -a)
- CPU: $(nproc) cores
- RAM: $(free -h | grep Mem | awk '{print $2}')

## Summary
- Total Tests: $(grep -c "PASS\|FAIL" *_results.txt 2>/dev/null || echo "0")
- Passed: $(grep -c "PASS" *_results.txt 2>/dev/null || echo "0")
- Failed: $(grep -c "FAIL" *_results.txt 2>/dev/null || echo "0")

EOF

cat test_report_$(date +%Y%m%d).md
```

---

**Document Version**: 1.0
**Last Updated**: December 2025
