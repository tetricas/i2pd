#pragma once

#include "FileTransferProtocol.h"
#include "Destination.h"
#include <memory>
#include <string>
#include <vector>

namespace i2p::filetransfer
{

/**
 * @brief Basic readiness and status interface
 */
class IReadyStatusProvider
{
public:
    virtual ~IReadyStatusProvider() = default;
    
    /**
     * @brief Check if component is ready for operation
     */
    [[nodiscard]] virtual bool isReady() const = 0;
    
    /**
     * @brief Get component status/diagnostic info
     */
    [[nodiscard]] virtual std::string getStatus() const = 0;
};

/**
 * @brief Interface for components that have network addresses
 */
class IAddressProvider
{
public:
    virtual ~IAddressProvider() = default;
    
    /**
     * @brief Get component's base32 address
     */
    [[nodiscard]] virtual std::string getB32Address() const = 0;
};

/**
 * @brief Interface for lifecycle management (start/stop)
 */
class ILifecycleManaged
{
public:
    virtual ~ILifecycleManaged() = default;
    
    /**
     * @brief Start the component
     */
    virtual void start() = 0;
    
    /**
     * @brief Stop the component
     */
    virtual void stop() = 0;
};

/**
 * @brief Interface for file operations
 */
class IFileManager
{
public:
    virtual ~IFileManager() = default;
    
    /**
     * @brief Add a file to manage
     * @param filename Name of the file
     * @param data File content
     */
    virtual void addMockFile(const std::string& filename, const std::vector<uint8_t>& data) = 0;
    
    /**
     * @brief Generate and add a file with specified size
     * @param filename Name of the file
     * @param size Size in bytes
     * @param seed Random seed for content generation
     */
    virtual void generateMockFile(const std::string& filename, size_t size, const std::string& seed = "") = 0;
    
    /**
     * @brief Get list of available files
     */
    [[nodiscard]] virtual std::vector<std::string> getFileList() const = 0;
};

/**
 * @brief Interface for file downloads
 */
class IFileDownloader
{
public:
    virtual ~IFileDownloader() = default;
    
    /**
     * @brief Result structure for file transfers
     */
    struct TransferResult {
        TransferStats stats;
        std::vector<uint8_t> data;
        bool success;
        std::string error;
    };
    
    /**
     * @brief Download a file from server
     * @param serverB32 Server's base32 address
     * @param filename Name of file to download
     * @param timeout_ms Total timeout for entire transfer
     * @return Transfer result with data and statistics
     */
    virtual TransferResult downloadFile(const std::string& serverB32, 
                                       const std::string& filename,
                                       int timeout_ms = 30000) = 0;
};

/**
 * @brief Composed interface for file transfer clients
 * Uses Interface Segregation Principle - only depends on what it needs
 */
class IFileTransferClient : public IFileDownloader, public IReadyStatusProvider
{
public:
    virtual ~IFileTransferClient() = default;
};

/**
 * @brief Composed interface for file transfer servers
 * Uses Interface Segregation Principle - only depends on what it needs
 */
class IFileTransferServer : public IFileManager, 
                           public ILifecycleManaged, 
                           public IAddressProvider, 
                           public IReadyStatusProvider
{
public:
    virtual ~IFileTransferServer() = default;
};

} // namespace i2p::filetransfer