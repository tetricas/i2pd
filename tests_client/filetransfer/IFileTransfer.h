#pragma once

/**
 * @file IFileTransfer.h
 * @brief Backward compatibility header - use IFileTransferInterfaces.h for new code
 * @deprecated This file provides the original monolithic interfaces for backward compatibility.
 *             New code should use the segregated interfaces in IFileTransferInterfaces.h
 */

#include "FileTransferProtocol.h"
#include "Destination.h"
#include <memory>
#include <string>
#include <vector>

namespace i2p::filetransfer
{

/**
 * @brief Abstract interface for file transfer clients
 * @deprecated Use IFileDownloader + IReadyStatusProvider from IFileTransferInterfaces.h
 */
class IFileTransferClient
{
public:
    virtual ~IFileTransferClient() = default;
    
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
    
    /**
     * @brief Check if client is ready for transfers
     */
    [[nodiscard]] virtual bool isReady() const = 0;
    
    /**
     * @brief Get client status/diagnostic info
     */
    [[nodiscard]] virtual std::string getStatus() const = 0;
};

/**
 * @brief Abstract interface for file transfer servers
 * @deprecated Use segregated interfaces from IFileTransferInterfaces.h
 */
class IFileTransferServer
{
public:
    virtual ~IFileTransferServer() = default;
    
    /**
     * @brief Add a mock file to serve
     * @param filename Name of the file
     * @param data File content
     */
    virtual void addMockFile(const std::string& filename, const std::vector<uint8_t>& data) = 0;
    
    /**
     * @brief Generate and add a mock file with specified size
     * @param filename Name of the file
     * @param size Size in bytes
     * @param seed Random seed for content generation
     */
    virtual void generateMockFile(const std::string& filename, size_t size, const std::string& seed = "") = 0;
    
    /**
     * @brief Start server
     */
    virtual void start() = 0;
    
    /**
     * @brief Stop server
     */
    virtual void stop() = 0;
    
    /**
     * @brief Get server's base32 address
     */
    [[nodiscard]] virtual std::string getB32Address() const = 0;
    
    /**
     * @brief Check if server is ready
     */
    [[nodiscard]] virtual bool isReady() const = 0;
    
    /**
     * @brief Get server status
     */
    [[nodiscard]] virtual std::string getStatus() const = 0;
    
    /**
     * @brief Get list of available files
     */
    [[nodiscard]] virtual std::vector<std::string> getFileList() const = 0;
};

} // namespace i2p::filetransfer