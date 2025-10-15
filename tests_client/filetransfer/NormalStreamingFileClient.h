#pragma once

#include "IFileTransfer.h"
#include "BinaryFileProtocol.h"
#include "../transport/NormalStreamingImpl.h"
#include "../core/TransferRecovery.h"
#include "../core/StreamStabilityMonitor.h"
#include <memory>
#include <chrono>
#include <atomic>

namespace i2p::filetransfer
{

/**
 * @brief High-performance file transfer client using Normal Streaming protocol
 */
class NormalStreamingFileClient : public IFileTransferClient
{
private:
    std::unique_ptr<embed::NormalStreamClient> m_streamClient;
    std::shared_ptr<client::ClientDestination> m_destination;
    mutable std::string m_lastStatus;
    
    // Recovery system
    std::unique_ptr<i2p::core::TransferRecoveryGuard> m_recoveryGuard;
    std::atomic<bool> m_recoveryInProgress{false};
    
public:
    explicit NormalStreamingFileClient(std::shared_ptr<client::ClientDestination> destination);
    ~NormalStreamingFileClient() override = default;
    
    // IFileTransferClient interface
    TransferResult downloadFile(const std::string& serverB32, 
                               const std::string& filename,
                               int timeout_ms = 30000) override;
    
    bool isReady() const override;
    std::string getStatus() const override;

private:
    /**
     * @brief Request file metadata using binary protocol
     */
    BinaryFileMetadata requestFileMetadata(const std::string& serverB32, 
                                         const std::string& filename, 
                                         int timeout_ms);
    
    /**
     * @brief Download file using streaming protocol (no chunk requests)
     */
    std::vector<uint8_t> downloadFileStream(const std::string& serverB32,
                                          const BinaryFileMetadata& metadata,
                                          int timeout_ms);
    
    /**
     * @brief Verify downloaded data against metadata
     */
    bool verifyDownloadedData(const std::vector<uint8_t>& data, 
                             const BinaryFileMetadata& metadata) const;
    
    /**
     * @brief Handle transfer recovery
     */
    bool handleRecovery(const i2p::core::TransferCheckpoint& checkpoint, 
                       i2p::core::RecoveryStrategy strategy);
    
    /**
     * @brief Resume transfer from checkpoint
     */
    TransferResult resumeTransfer(const i2p::core::TransferCheckpoint& checkpoint, 
                                 int timeout_ms);
    
    /**
     * @brief Safe data validation with progressive checking
     */
    bool validatePartialData(const std::vector<uint8_t>& data, 
                           size_t expectedSize) const;
    
    /**
     * @brief Download file stream with recovery monitoring
     */
    std::vector<uint8_t> downloadFileStreamWithRecovery(const std::string& serverB32,
                                                       const BinaryFileMetadata& metadata,
                                                       i2p::core::TransferCheckpoint& checkpoint,
                                                       int timeout_ms);
};

} // namespace i2p::filetransfer