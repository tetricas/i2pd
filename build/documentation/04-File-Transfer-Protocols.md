# File Transfer Protocols - i2pd File Transfer System

**Purpose**: Detailed protocol specifications and wire formats
**Audience**: Protocol implementers, developers, security analysts
**Level**: Advanced

---

## ⚠️ IMPORTANT: Architecture Status

This document covers **TWO ARCHITECTURES**:

### 🟢 **PRODUCTION** - Tunnel/Datagram Architecture (tunnel_ftp_demo)
- **Status**: ✅ Production-ready (Sessions 18-20)
- **Performance**: 5-6 MB/s (100MB), 8-9 MB/s (1GB) in cloud
- **Transport**: Direct tunnel messaging with SSU2 preferences
- **Sections**: See "Tunnel-Based Protocol" below

### 🔴 **LEGACY** - Streaming Architecture (chunked_file_test)
- **Status**: ❌ DEPRECATED (Sessions 1-16)
- **Performance**: 2.5-4 MB/s maximum (local networks only)
- **Transport**: Simple Messaging & Normal Streaming protocols
- **Value**: Historical learning path, NOT for production use
- **Sections**: Simple Messaging, Normal Streaming (marked LEGACY)

**For new deployments**: Use tunnel_ftp_demo (tunnel-based architecture)

---

## Protocol Overview

The i2pd File Transfer System evolved through two major architectures:

### Production: Tunnel-Based (tunnel_ftp_demo)
- **Direct tunnel messaging** - Bypasses streaming layer
- **SSU2-preferred transports** - UDP-optimized for cloud
- **31KB chunk datagrams** - Single-message chunks

### Legacy: Streaming-Based (chunked_file_test)
- **Simple Messaging** - Fire-and-forget with echo (DEPRECATED)
- **Normal Streaming** - Full i2p streaming (DEPRECATED)
- **Application-layer chunking** - Works but slower (DEPRECATED)

---

## 🟢 Tunnel-Based Protocol (PRODUCTION - tunnel_ftp_demo)

### Architecture

**Direct Tunnel Messaging**:
- Bypasses i2p streaming layer entirely
- Uses `DatagramDestination` for direct datagram send/receive
- Each 31KB chunk sent as single tunnel message
- SSU2-preferred for UDP optimization

### Key Characteristics

**Chunk Size**: 31KB (31,744 bytes)
- Optimized for tunnel message capacity
- Single message per chunk (no fragmentation)
- CRC32 integrity verification per chunk

**Transport Selection**:
```cpp
// SSU2-preferring tunnel pool
auto tunnelPool = std::make_shared<SSU2PreferringTunnelPool>(
    hops,      // 1 hop for performance
    hops,      // 1 hop for performance
    10,        // 10 inbound tunnels for 10 MB/s
    10,        // 10 outbound tunnels
    0,         // variance
    0,         // variance
    true       // high bandwidth enabled
);
```

**Performance**:
- **100MB files**: 5-6 MB/s
- **1GB files**: 8-9 MB/s
- **Cloud-optimized**: Works reliably in cloud environments

### Wire Format (31KB Chunks)

```
Datagram Message:
┌─────────────┬────────────┬──────────────────┬──────────┐
│ CHUNK_INDEX │ DATA_SIZE  │     RAW_DATA     │  CRC32   │
│  4 bytes    │  4 bytes   │   31744 bytes    │ 4 bytes  │
└─────────────┴────────────┴──────────────────┴──────────┘

CHUNK_INDEX = uint32_t (little-endian, 0-based)
DATA_SIZE = uint32_t (actual data size ≤ 31744)
RAW_DATA = binary file data
CRC32 = checksum for integrity
```

**Total Message Size**: 31,756 bytes per chunk

### Transfer Flow

```
Client                                    Server
  │                                          │
  │  1. Request file via tunnel              │
  │  ─────────────────────────────────────►  │
  │                                          │
  │  2. Receive 31KB chunk [0]               │
  │  ◄─────────────────────────────────────  │
  │     (single tunnel message)              │
  │                                          │
  │  3. Receive 31KB chunk [1]               │
  │  ◄─────────────────────────────────────  │
  │                                          │
  │  ... (continuous)                        │
  │                                          │
  │  N. Receive final chunk                  │
  │  ◄─────────────────────────────────────  │
  │                                          │
  │  Verify CRC32 on each chunk              │
  │  Calculate SHA-256 of complete file      │
```

### Implementation Reference

**Source Files**:
- `tunnel_ftp_demo/tunnel_ftp_client.cpp`
- `tunnel_ftp_demo/tunnel_ftp_server.cpp`
- Uses `DatagramDestination::SendDatagramTo()`
- Direct tunnel messaging API

---

## 🔴 LEGACY PROTOCOLS (DEPRECATED - chunked_file_test)

**⚠️ WARNING**: The following sections document the DEPRECATED streaming-based architecture used in chunked_file_test (Sessions 1-16). These protocols are preserved for historical understanding but should NOT be used for new deployments.

**Why deprecated**: Streaming-based approach hits 2.5-4 MB/s ceiling in cloud environments. Tunnel-based architecture (above) achieves 5-9 MB/s.

**Use tunnel_ftp_demo instead.**

---

## Application-Layer Protocol (LEGACY)

### Message Types

```cpp
enum MessageType : uint8_t {
    FILE_REQUEST      = 0x01,  // Client → Server
    METADATA_RESPONSE = 0x02,  // Server → Client
    CHUNK_DATA        = 0x03,  // Server → Client (continuous stream)
    TRANSFER_COMPLETE = 0x04   // Client → Server
};
```

### Protocol Flow

```
Client                                    Server
  │                                          │
  │  1. FILE_REQUEST                         │
  │  ─────────────────────────────────────►  │
  │                                          │
  │                   2. METADATA_RESPONSE   │
  │  ◄─────────────────────────────────────  │
  │                                          │
  │                   3. CHUNK_DATA[0]       │
  │  ◄─────────────────────────────────────  │
  │                                          │
  │                   4. CHUNK_DATA[1]       │
  │  ◄─────────────────────────────────────  │
  │                                          │
  │                   ... (continuous)       │
  │  ◄─────────────────────────────────────  │
  │                                          │
  │                   N. CHUNK_DATA[last]    │
  │  ◄─────────────────────────────────────  │
  │                                          │
  │  N+1. TRANSFER_COMPLETE                  │
  │  ─────────────────────────────────────►  │
  │                                          │
```

### Wire Formats

#### FILE_REQUEST Message

```
┌────────┬──────────────┬────────────┐
│  TYPE  │  FILENAME    │  PADDING   │
│ 1 byte │  64 bytes    │  variable  │
└────────┴──────────────┴────────────┘

TYPE = 0x01
FILENAME = null-terminated UTF-8 string
```

#### METADATA_RESPONSE Message

```
┌────────┬───────────┬────────────┬─────────────┬──────────┐
│  TYPE  │FILE_SIZE  │ CHUNK_SIZE │ CHUNK_COUNT │ RESERVED │
│ 1 byte │  8 bytes  │  4 bytes   │  4 bytes    │ 48 bytes │
└────────┴───────────┴────────────┴─────────────┴──────────┘

TYPE = 0x02
FILE_SIZE = uint64_t (little-endian)
CHUNK_SIZE = uint32_t (little-endian, typically 512-65536)
CHUNK_COUNT = uint32_t (little-endian)
RESERVED = zeros for future use
```

#### CHUNK_DATA Message

```
┌────────┬─────────────┬────────────┬──────────────────┐
│  TYPE  │ CHUNK_INDEX │ DATA_SIZE  │     RAW_DATA     │
│ 1 byte │  4 bytes    │  4 bytes   │   N bytes        │
└────────┴─────────────┴────────────┴──────────────────┘

TYPE = 0x03
CHUNK_INDEX = uint32_t (little-endian, 0-based)
DATA_SIZE = uint32_t (little-endian)
RAW_DATA = binary file data
```

#### TRANSFER_COMPLETE Message

```
┌────────┬───────────┬──────────┐
│  TYPE  │  STATUS   │ CHECKSUM │
│ 1 byte │  1 byte   │ 32 bytes │
└────────┴───────────┴──────────┘

TYPE = 0x04
STATUS = 0x00 (success) or 0x01 (failure)
CHECKSUM = SHA-256 hash (optional, may be zeros)
```

---

## Simple Messaging Protocol (LEGACY)

**⚠️ DEPRECATED**: Part of chunked_file_test streaming architecture

### Overview

Fire-and-forget protocol with echo-based reliability (Session 01).

### Wire Format

```
┌──────────┬──────────┬────────────┬──────────────┐
│   TYPE   │   SEQ    │   LENGTH   │     DATA     │
│  1 byte  │ 2 bytes  │  2 bytes   │   N bytes    │
└──────────┴──────────┴────────────┴──────────────┘

TYPE = message type identifier
SEQ = sequence number (uint16_t, little-endian)
LENGTH = payload length (uint16_t, little-endian)
DATA = payload data
```

### Reliability Mechanism

Uses i2pd's `PACKET_FLAG_ECHO`:

```cpp
packet->SetFlags(PACKET_FLAG_ECHO);
```

- Server echoes received packets
- Client waits for echo before considering message sent
- No explicit ACK protocol needed

### Implementation

```cpp
void SimpleSend(const uint8_t* buf, size_t len) {
    auto packet = m_LocalDestination.NewPacket();
    packet->len = len;
    memcpy(packet->buf, buf, len);
    packet->SetFlags(PACKET_FLAG_ECHO);
    m_Stream->SendPacket(packet);
}
```

---

## Normal Streaming Protocol (LEGACY)

**⚠️ DEPRECATED**: Part of chunked_file_test streaming architecture

### Overview

Full i2p streaming with proper connection establishment (Session 07).

### Connection Establishment

**Critical**: Must send empty packet after `CreateStream()`:

```cpp
auto stream = CreateStream(destination);
stream->Send(nullptr, 0);  // Mandatory handshake
```

### Stream Management

**Single Bidirectional Stream**:
- Client and server communicate on same stream
- Avoid creating multiple streams (causes routing failures)
- Proper closure: client closes, server waits

### Threading Model

```cpp
// Separate receive thread to avoid blocking
std::thread receiveThread([this]() {
    while (running) {
        size_t received = stream->Receive(buffer, len, timeout);
        processData(buffer, received);
    }
});
```

### Flow Control

i2pd streaming provides built-in:
- Packet reordering
- Retransmission
- Window-based flow control
- Congestion avoidance

---

## Performance Optimizations

### Binary Protocol (Session 08)

**Chunked Transfer Optimization**:

Original (JSON):
```json
{"chunk_index": 42, "data": "base64..."}
```

Optimized (Binary):
```
[INDEX:4][SIZE:4][DATA:N]
8 bytes header vs ~50+ bytes JSON
```

**Improvement**: 6x header reduction

### Continuous Streaming (Session 08)

**Problem**: Request-response per chunk adds latency

**Solution**: Server streams all chunks automatically after metadata

```cpp
// Server: Continuous chunk streaming
sendMetadata();
for (int i = 0; i < chunkCount; i++) {
    sendChunk(i);  // No wait for request
}
```

**Improvement**: Eliminates N round-trips

### Bulk Transfer Mode (Session 10)

Activated for transfers ≥64KB:

```cpp
bool isBulkTransfer = (fileSize >= 65536);
if (isBulkTransfer) {
    enableSignatureBatching();    // 93.7% reduction
    enableACKBatching();           // 87.5% reduction
    disableCompressionForBinary(); // Save CPU
}
```

---

## Data Integrity

### SHA-256 Verification

```cpp
// File generation with deterministic checksum
std::string generateMockFile(size_t size) {
    std::vector<uint8_t> data(size);
    std::mt19937 gen(seed);
    for (auto& byte : data) {
        byte = gen() % 256;
    }
    return calculateSHA256(data);
}

// Client verification
bool verifyIntegrity(const std::string& received, 
                     const std::string& expected) {
    return calculateSHA256(received) == expected;
}
```

### Chunk-Level Validation

Each chunk can be validated individually:

```cpp
uint32_t chunkChecksum = crc32(chunkData);
// Optional per-chunk CRC for early error detection
```

---

## Error Handling

### Timeout Strategy

```cpp
// Client receive with timeout
size_t received = stream->Receive(
    buffer, 
    maxLen, 
    timeout=1000  // 1 second per chunk
);

if (received == 0) {
    if (++timeoutCount > MAX_RETRIES) {
        throw TransferException("Transfer timeout");
    }
}
```

### Retry Logic

```cpp
const int MAX_CHUNK_RETRIES = 3;
for (int retry = 0; retry < MAX_CHUNK_RETRIES; retry++) {
    try {
        receiveChunk(index);
        break;  // Success
    } catch (const ChunkException& e) {
        if (retry == MAX_CHUNK_RETRIES - 1) throw;
        sleep(RETRY_DELAY);
    }
}
```

---

## Configuration Parameters

### Chunk Size Selection

| File Size | Chunk Size | Chunks | Rationale |
|-----------|-----------|--------|-----------|
| <1MB | 512 B | <2048 | Minimize per-chunk overhead |
| 1-100MB | 4 KB | 256-25600 | Balance latency vs overhead |
| 100MB-1GB | 64 KB | 1600-16000 | Optimize throughput |
| >1GB | 1 MB | >1000 | Maximum throughput |

### Timeout Configuration

```cpp
// Per-chunk timeout
const int CHUNK_TIMEOUT_MS = 1000;  // 1 second

// Total transfer timeout
const int TOTAL_TIMEOUT_MS = fileSize / 1024;  // 1s per KB
```

### Buffer Sizes

```cpp
// Receive buffer
const size_t RECV_BUFFER_SIZE = CHUNK_SIZE + 1024;  // +overhead

// Send buffer  
const size_t SEND_BUFFER_SIZE = 64 * 1024;  // 64KB
```

---

## Protocol Extensions (Future)

### Parallel Chunk Requests

```cpp
// Request multiple chunks in parallel
for (int i = 0; i < PARALLEL_FACTOR; i++) {
    requestChunk(currentChunk + i);
}
```

### Compression Support

```cpp
METADATA_RESPONSE {
    ...
    compression: uint8_t  // 0=none, 1=gzip, 2=lz4
}

CHUNK_DATA {
    ...
    compressed_size: uint32_t
    uncompressed_size: uint32_t
}
```

### Resume Support

```cpp
FILE_REQUEST {
    ...
    resume_from: uint64_t  // Byte offset to resume from
}
```

---

## Security Considerations

### Protocol Security

**Not Encrypted at App Layer**:
- Relies on I2P garlic encryption
- No additional application-layer encryption
- Binary protocol provides minimal obfuscation

**Authentication**:
- No authentication in protocol
- Relies on I2P destination verification
- Implicitly authenticated by I2P routing

### Threat Model

**Protected Against**:
- Eavesdropping (I2P encryption)
- Message tampering (I2P authentication)
- Replay attacks (sequence numbers)

**NOT Protected Against**:
- Compromised I2P nodes
- Traffic analysis (timing, size)
- DoS (no rate limiting in protocol)

---

**Document Version**: 1.0
**Last Updated**: December 2025
**Status**: Complete specification for production use
