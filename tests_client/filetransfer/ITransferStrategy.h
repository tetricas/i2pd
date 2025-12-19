#pragma once

#include "../transport/IStreamingProvider.h"
#include "../core/TransferResult.h"
#include "FileTransferProtocol.h"
#include <memory>
#include <string>
#include <vector>

namespace i2p::filetransfer
{

/**
 * @brief Transfer context containing shared state and dependencies
 */
struct TransferContext
{
    std::shared_ptr<i2p::transport::IStreamingMessageProvider> messageProvider;
    std::shared_ptr<i2p::transport::IStreamingServerProvider> serverProvider;
    std::string serverB32;
    std::string filename;
    int timeoutMs;
    
    TransferContext(std::shared_ptr<i2p::transport::IStreamingMessageProvider> msgProvider,
                   const std::string& server,
                   const std::string& file,
                   int timeout)
        : messageProvider(std::move(msgProvider))
        , serverB32(server)
        , filename(file)
        , timeoutMs(timeout) {}
};

/**
 * @brief Abstract strategy for file transfer operations
 * Implements Strategy Pattern
 */
class ITransferStrategy
{
public:
    virtual ~ITransferStrategy() = default;
    
    /**
     * @brief Transfer result structure
     */
    struct TransferResult {
        TransferStats stats;
        std::vector<uint8_t> data;
        bool success{false};
        std::string error;
    };
    
    /**
     * @brief Execute the transfer strategy
     * @param context Transfer context with dependencies and parameters
     * @return Transfer result
     */
    virtual TransferResult execute(const TransferContext& context) = 0;
    
    /**
     * @brief Get strategy name for identification
     */
    [[nodiscard]] virtual std::string getName() const = 0;
    
    /**
     * @brief Get strategy description
     */
    [[nodiscard]] virtual std::string getDescription() const = 0;
    
    /**
     * @brief Check if strategy supports the given protocol
     */
    [[nodiscard]] virtual bool supportsProtocol(const std::string& protocolName) const = 0;
};

/**
 * @brief Chunked transfer strategy (existing approach)
 * Uses request-response pattern with metadata then chunks
 */
class ChunkedTransferStrategy : public ITransferStrategy
{
public:
    ~ChunkedTransferStrategy() override = default;
    
    // ITransferStrategy interface
    TransferResult execute(const TransferContext& context) override;
    [[nodiscard]] std::string getName() const override { return "chunked"; }
    [[nodiscard]] std::string getDescription() const override { return "Request metadata then fetch chunks sequentially"; }
    [[nodiscard]] bool supportsProtocol(const std::string& protocolName) const override;
};

/**
 * @brief Binary streaming strategy (optimized for Normal Streaming)
 * Uses binary protocol with continuous data stream
 */
class BinaryStreamingStrategy : public ITransferStrategy
{
public:
    ~BinaryStreamingStrategy() override = default;
    
    // ITransferStrategy interface
    TransferResult execute(const TransferContext& context) override;
    [[nodiscard]] std::string getName() const override { return "binary-stream"; }
    [[nodiscard]] std::string getDescription() const override { return "Binary protocol with continuous streaming"; }
    [[nodiscard]] bool supportsProtocol(const std::string& protocolName) const override;
};

/**
 * @brief Parallel transfer strategy (future enhancement)
 * Could use multiple connections for parallel chunk downloads
 */
class ParallelTransferStrategy : public ITransferStrategy
{
private:
    int m_parallelConnections;
    
public:
    explicit ParallelTransferStrategy(int connections = 3) : m_parallelConnections(connections) {}
    ~ParallelTransferStrategy() override = default;
    
    // ITransferStrategy interface
    TransferResult execute(const TransferContext& context) override;
    [[nodiscard]] std::string getName() const override { return "parallel"; }
    [[nodiscard]] std::string getDescription() const override { 
        return "Parallel chunk downloads using " + std::to_string(m_parallelConnections) + " connections"; 
    }
    [[nodiscard]] bool supportsProtocol(const std::string& protocolName) const override;
};

} // namespace i2p::filetransfer