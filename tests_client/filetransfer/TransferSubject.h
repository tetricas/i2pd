#pragma once

#include "ITransferObserver.h"
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>

namespace i2p::filetransfer
{

/**
 * @brief Concrete implementation of transfer subject
 * Thread-safe observer management
 */
class TransferSubject : public ITransferSubject
{
private:
    mutable std::mutex m_observersMutex;
    std::vector<std::shared_ptr<ITransferObserver>> m_observers;
    
public:
    ~TransferSubject() override = default;
    
    // ITransferSubject interface
    void addObserver(std::shared_ptr<ITransferObserver> observer) override;
    void removeObserver(std::shared_ptr<ITransferObserver> observer) override;
    void clearObservers() override;
    [[nodiscard]] size_t getObserverCount() const override;

protected:
    // Protected methods for subclasses to notify observers
    void notifyObservers(const TransferEventData& eventData) override;
    void notifyProgress(const TransferProgress& progress) override;
    
    // Helper methods for common event notifications
    void notifyTransferStarted(const std::string& transferId, const std::string& filename);
    void notifyTransferCompleted(const std::string& transferId, const std::string& filename);
    void notifyTransferFailed(const std::string& transferId, const std::string& error);
    void notifyChunkCompleted(const std::string& transferId, size_t chunkIndex, size_t totalChunks);
    void notifyMetadataReceived(const std::string& transferId, size_t totalBytes, size_t totalChunks);
};

} // namespace i2p::filetransfer