# API Reference - i2pd File Transfer System

**Purpose**: Code interface documentation
**Audience**: Developers, integrators
**Level**: Technical

---

## ⚠️ IMPORTANT: Architecture Context

This API reference covers **TWO ARCHITECTURES**:

### 🟢 **PRODUCTION** - tunnel_ftp_demo (USE THIS)
- **Tool**: `tunnel_ftp_demo` (tunnel-based)
- **Architecture**: Direct tunnel messaging via DatagramDestination
- **Performance**: 5-6 MB/s (100MB), 8-9 MB/s (1GB)
- **Status**: ✅ Production-ready
- **API**: See "Production APIs" section below

### 🔴 **LEGACY** - chunked_file_test (DEPRECATED)
- **Tool**: `chunked_file_test` (streaming-based)
- **Architecture**: i2p streaming layer with application chunking
- **Performance**: 2.5-4 MB/s maximum
- **Status**: ❌ DEPRECATED
- **API**: Historical reference only

**For new integrations**: Use tunnel_ftp_demo APIs

---

## 🟢 Production APIs (tunnel_ftp_demo)

### TunnelFTPClient

**Location**: `tunnel_ftp_demo/tunnel_ftp_client.h`

```cpp
class TunnelFTPClient {
public:
    /**
     * Constructor
     * @param localDest I2P local destination with DatagramDestination
     */
    explicit TunnelFTPClient(
        std::shared_ptr<i2p::client::ClientDestination> localDest
    );

    /**
     * Transfer file using tunnel messaging
     * @param filePath Local file path to transfer
     * @param serverIdentHash Server destination identity hash
     * @throws TransferException on failure
     */
    void transferFile(
        const std::string& filePath,
        const i2p::data::IdentHash& serverIdentHash
    );

    /**
     * Set server B32 address
     * @param b32Address Server B32 address
     */
    void setServerAddress(const std::string& b32Address);

    /**
     * Get transfer statistics
     * @return TransferStats structure with throughput, time, etc.
     */
    TransferStats getStats() const;
};
```

### TunnelFTPServer

**Location**: `tunnel_ftp_demo/tunnel_ftp_server.h`

```cpp
class TunnelFTPServer {
public:
    /**
     * Constructor
     * @param localDest I2P local destination with DatagramDestination
     * @param outputDir Directory to save received files
     */
    explicit TunnelFTPServer(
        std::shared_ptr<i2p::client::ClientDestination> localDest,
        const std::string& outputDir
    );

    /**
     * Start server and listen for tunnel messages
     * Blocks until stopped
     */
    void run();

    /**
     * Stop server
     */
    void stop();

    /**
     * Get server B32 address
     * @return B32 address string
     */
    std::string getB32Address() const;

    /**
     * Enable HTTPS reseed server (Session 20)
     * @param port HTTPS port (default 7071)
     * @param certPath SSL certificate path
     */
    void enableHTTPSReseed(int port = 7071,
                           const std::string& certPath = "");
};
```

### Tunnel Messaging Protocol

**31KB Chunk Format**:

```cpp
struct TunnelChunk {
    uint32_t index;       // Chunk index (0-based)
    uint32_t size;        // Actual data size (≤ 31744)
    uint32_t crc32;       // CRC32 checksum
    uint8_t data[31744];  // 31KB data (tunnel-optimized)
};

// Total size: 31,756 bytes (fits in single tunnel message)

/**
 * Send chunk via tunnel datagram
 * @param chunk TunnelChunk to send
 * @param destination Target destination hash
 */
void sendTunnelChunk(
    const TunnelChunk& chunk,
    const i2p::data::IdentHash& destination
);

/**
 * Receive chunk from tunnel datagram
 * @param timeout Timeout in milliseconds
 * @return Received TunnelChunk
 * @throws TimeoutException if no data received
 */
TunnelChunk receiveTunnelChunk(int timeout = 30000);
```

### SSU2-Preferred Tunnel Pool

**Location**: `tunnel_ftp_demo/SSU2PreferringTunnelPool.h`

```cpp
class SSU2PreferringTunnelPool : public i2p::tunnel::TunnelPool {
public:
    /**
     * Constructor with SSU2 preference
     * @param inboundHops Number of inbound hops (typically 1)
     * @param outboundHops Number of outbound hops (typically 1)
     * @param inboundQuantity Number of inbound tunnels (10 for 8-10 MB/s)
     * @param outboundQuantity Number of outbound tunnels (10 for 8-10 MB/s)
     */
    SSU2PreferringTunnelPool(
        int inboundHops,
        int outboundHops,
        int inboundQuantity,
        int outboundQuantity,
        int inboundVariance = 0,
        int outboundVariance = 0,
        bool isHighBandwidth = true
    );

    /**
     * Select peer with SSU2 preference
     * @param peers Available peers
     * @return Selected peer (SSU2 preferred)
     */
    std::shared_ptr<const i2p::data::RouterInfo>
        selectPeerWithSSU2Preference(
            const std::vector<std::shared_ptr<const i2p::data::RouterInfo>>& peers
        ) override;
};
```

### HTTPS Reseed (Session 20)

**Location**: `tunnel_ftp_demo/HTTPSReseedServer.h`

```cpp
class HTTPSReseedServer {
public:
    /**
     * Start HTTPS server for router.info distribution
     * @param port HTTPS port (default 7071)
     * @param certPath SSL certificate path
     * @param routerInfoPath Path to router.info file
     */
    void start(int port,
               const std::string& certPath,
               const std::string& routerInfoPath);

    /**
     * Stop HTTPS server
     */
    void stop();

    /**
     * Get certificate fingerprint (SHA-256)
     * @return Certificate hash for pinning
     */
    std::string getCertificateFingerprint() const;
};

/**
 * Fetch router.info via HTTPS with certificate pinning
 * @param url HTTPS URL to router.info
 * @param expectedFingerprint Expected cert fingerprint (SHA-256)
 * @return router.info data
 * @throws SecurityException if fingerprint mismatch
 */
std::vector<uint8_t> fetchWithCertificatePinning(
    const std::string& url,
    const std::string& expectedFingerprint
);
```

---

## 🔴 Legacy APIs (chunked_file_test - DEPRECATED)

**⚠️ WARNING**: The following APIs are for the DEPRECATED streaming-based architecture. Use tunnel_ftp_demo APIs for new integrations.

## Core Interfaces

### IFileTransferClient

**Location**: `tests_client/filetransfer/IFileTransfer.h`

```cpp
class IFileTransferClient {
public:
    virtual ~IFileTransferClient() = default;
    
    /**
     * Transfer a file to the server
     * @param filePath Local file path to transfer
     * @throws TransferException on failure
     */
    virtual void transferFile(const std::string& filePath) = 0;
    
    /**
     * Set server destination address
     * @param b32Address Server B32 address
     */
    virtual void setServerAddress(const std::string& b32Address) = 0;
    
    /**
     * Set transfer timeout
     * @param timeoutMs Timeout in milliseconds
     */
    virtual void setTimeout(int timeoutMs) = 0;
};
```

### IFileTransferServer

```cpp
class IFileTransferServer {
public:
    virtual ~IFileTransferServer() = default;
    
    /**
     * Start server and wait for connections
     * Blocks until stopped
     */
    virtual void run() = 0;
    
    /**
     * Stop server
     */
    virtual void stop() = 0;
    
    /**
     * Get server B32 address
     * @return B32 address string
     */
    virtual std::string getB32Address() const = 0;
};
```

---

## File Transfer Protocol

### FileTransferProtocol

**Location**: `tests_client/filetransfer/FileTransferProtocol.h`

```cpp
namespace FileTransferProtocol {
    
    enum MessageType : uint8_t {
        FILE_REQUEST = 0x01,
        METADATA_RESPONSE = 0x02,
        CHUNK_DATA = 0x03,
        TRANSFER_COMPLETE = 0x04
    };
    
    struct Metadata {
        uint64_t fileSize;
        uint32_t chunkSize;
        uint32_t chunkCount;
        std::string checksum;  // SHA-256
    };
    
    struct ChunkData {
        uint32_t index;
        uint32_t size;
        std::vector<uint8_t> data;
    };
    
    /**
     * Serialize metadata to binary
     * @param meta Metadata structure
     * @return Binary representation
     */
    std::vector<uint8_t> serializeMetadata(const Metadata& meta);
    
    /**
     * Deserialize metadata from binary
     * @param data Binary data
     * @return Metadata structure
     */
    Metadata deserializeMetadata(const std::vector<uint8_t>& data);
    
    /**
     * Serialize chunk to binary
     * @param chunk ChunkData structure
     * @return Binary representation
     */
    std::vector<uint8_t> serializeChunk(const ChunkData& chunk);
    
    /**
     * Deserialize chunk from binary
     * @param data Binary data
     * @return ChunkData structure
     */
    ChunkData deserializeChunk(const std::vector<uint8_t>& data);
}
```

---

## Transport Implementations

### SimpleStreamingImpl

**Location**: `tests_client/transport/SimpleStreamingImpl.h`

```cpp
class SimpleStreamingImpl : public IStreamingProvider {
public:
    /**
     * Constructor
     * @param localDestination I2P local destination
     */
    explicit SimpleStreamingImpl(
        std::shared_ptr<i2p::client::ClientDestination> localDestination
    );
    
    /**
     * Send data using simple messaging protocol
     * @param buf Data buffer
     * @param len Data length
     */
    void SimpleSend(const uint8_t* buf, size_t len) override;
    
    /**
     * Receive data using simple messaging protocol
     * @param buf Buffer to receive into
     * @param len Maximum length
     * @param timeout Timeout in milliseconds
     * @return Bytes received
     */
    size_t SimpleReceive(uint8_t* buf, size_t len, int timeout) override;
};
```

### NormalStreamingImpl

**Location**: `tests_client/transport/NormalStreamingImpl.h`

```cpp
class NormalStreamingImpl : public IStreamingProvider {
public:
    /**
     * Constructor
     * @param localDestination I2P local destination
     */
    explicit NormalStreamingImpl(
        std::shared_ptr<i2p::client::ClientDestination> localDestination
    );
    
    /**
     * Create stream to destination
     * @param destination Target I2P destination
     * @return Shared pointer to stream
     */
    std::shared_ptr<i2p::stream::Stream> CreateStream(
        const i2p::data::IdentHash& destination
    );
    
    /**
     * Send data on stream
     * @param buf Data buffer
     * @param len Data length
     */
    void Send(const uint8_t* buf, size_t len) override;
    
    /**
     * Receive data from stream
     * @param buf Buffer to receive into
     * @param len Maximum length
     * @param timeout Timeout in milliseconds
     * @return Bytes received
     */
    size_t Receive(uint8_t* buf, size_t len, int timeout) override;
};
```

---

## Utility Classes

### I2PdUtils

**Location**: `tests_client/core/I2PdUtils.h`

```cpp
class I2PdUtils {
public:
    /**
     * Initialize I2P daemon
     * @param configPath Path to configuration file
     */
    static void initializeI2P(const std::string& configPath);
    
    /**
     * Create client destination
     * @param hopCount Number of hops (0-3)
     * @return Shared pointer to destination
     */
    static std::shared_ptr<i2p::client::ClientDestination>
        createDestination(int hopCount);
    
    /**
     * Configure exploratory tunnels
     * @param hopCount Number of hops
     */
    static void configureExploratoryTunnels(int hopCount);
    
    /**
     * Get B32 address from destination
     * @param dest Client destination
     * @return B32 address string
     */
    static std::string getB32Address(
        std::shared_ptr<i2p::client::ClientDestination> dest
    );
    
    /**
     * Save B32 address to file
     * @param address B32 address
     * @param filePath File path to save to
     */
    static void saveB32Address(const std::string& address,
                               const std::string& filePath);
    
    /**
     * Load B32 address from file
     * @param filePath File path to load from
     * @return B32 address string
     */
    static std::string loadB32Address(const std::string& filePath);
};
```

### NetworkDetector

**Location**: `tests_client/core/NetworkDetector.h`

```cpp
class NetworkDetector {
public:
    /**
     * Detect if running on local network
     * @return true if local (>100MB/s), false otherwise
     */
    bool isLocalNetwork();
    
    /**
     * Optimize for local network
     * Removes bandwidth caps, increases buffers
     */
    void optimizeForLocal();
    
    /**
     * Get estimated bandwidth
     * @return Bandwidth in MB/s
     */
    double getEstimatedBandwidth();
};
```

---

## Factory Pattern

### FileTransferFactory

**Location**: `tests_client/filetransfer/FileTransferFactory.h`

```cpp
enum class ProtocolType {
    SIMPLE,
    NORMAL
};

class FileTransferFactory {
public:
    /**
     * Create file transfer client
     * @param type Protocol type
     * @param dest Local destination
     * @return Unique pointer to client
     */
    static std::unique_ptr<IFileTransferClient> createClient(
        ProtocolType type,
        std::shared_ptr<i2p::client::ClientDestination> dest
    );
    
    /**
     * Create file transfer server
     * @param type Protocol type
     * @param dest Local destination
     * @return Unique pointer to server
     */
    static std::unique_ptr<IFileTransferServer> createServer(
        ProtocolType type,
        std::shared_ptr<i2p::client::ClientDestination> dest
    );
};
```

---

## Error Handling

### Exception Hierarchy

```cpp
// Base exception
class TransferException : public std::runtime_error {
public:
    explicit TransferException(const std::string& message);
};

// Specific exceptions
class TimeoutException : public TransferException {
    explicit TimeoutException(const std::string& message);
};

class IntegrityException : public TransferException {
    explicit IntegrityException(const std::string& message);
};

class NetworkException : public TransferException {
    explicit NetworkException(const std::string& message);
};
```

---

## Usage Examples

### Basic Client

```cpp
#include "FileTransferFactory.h"
#include "I2PdUtils.h"

int main() {
    // Initialize I2P
    I2PdUtils::initializeI2P("/etc/i2pd/client.conf");
    
    // Create destination
    auto dest = I2PdUtils::createDestination(1);  // 1-hop
    
    // Create client
    auto client = FileTransferFactory::createClient(
        ProtocolType::NORMAL, dest
    );
    
    // Load server address
    std::string serverB32 = I2PdUtils::loadB32Address("/tmp/server_b32.txt");
    client->setServerAddress(serverB32);
    
    // Transfer file
    try {
        client->transferFile("/path/to/file.bin");
        std::cout << "Transfer successful\n";
    } catch (const TransferException& e) {
        std::cerr << "Transfer failed: " << e.what() << "\n";
    }
    
    return 0;
}
```

### Basic Server

```cpp
#include "FileTransferFactory.h"
#include "I2PdUtils.h"

int main() {
    // Initialize I2P
    I2PdUtils::initializeI2P("/etc/i2pd/server.conf");
    
    // Create destination
    auto dest = I2PdUtils::createDestination(1);  // 1-hop
    
    // Create server
    auto server = FileTransferFactory::createServer(
        ProtocolType::NORMAL, dest
    );
    
    // Get and save B32 address
    std::string b32 = server->getB32Address();
    I2PdUtils::saveB32Address(b32, "/tmp/server_b32.txt");
    
    std::cout << "Server ready: " << b32 << "\n";
    
    // Run server (blocks)
    server->run();
    
    return 0;
}
```

---

**Document Version**: 1.0
**Last Updated**: December 2025
