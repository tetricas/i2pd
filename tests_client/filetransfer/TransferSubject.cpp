#include "TransferSubject.h"
#include "../core/FileTransferLogging.h"

namespace i2p::filetransfer
{

void TransferSubject::addObserver(std::shared_ptr<ITransferObserver> observer)
{
    if (!observer) {
        FT_LOG_WARNING("TransferSubject", "Attempted to add null observer");
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_observersMutex);
    
    // Check if already exists
    auto it = std::find(m_observers.begin(), m_observers.end(), observer);
    if (it == m_observers.end()) {
        m_observers.push_back(observer);
        FT_LOG_DEBUG("TransferSubject", "Added observer: " << observer->getObserverId() << " (total: " << m_observers.size() << ")");
    } else {
        FT_LOG_DEBUG("TransferSubject", "Observer already exists: " << observer->getObserverId());
    }
}

void TransferSubject::removeObserver(std::shared_ptr<ITransferObserver> observer)
{
    if (!observer) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_observersMutex);
    
    auto it = std::find(m_observers.begin(), m_observers.end(), observer);
    if (it != m_observers.end()) {
        FT_LOG_DEBUG("TransferSubject", "Removed observer: " << observer->getObserverId());
        m_observers.erase(it);
    }
}

void TransferSubject::clearObservers()
{
    std::lock_guard<std::mutex> lock(m_observersMutex);
    size_t count = m_observers.size();
    m_observers.clear();
    FT_LOG_DEBUG("TransferSubject", "Cleared " << count << " observers");
}

size_t TransferSubject::getObserverCount() const
{
    std::lock_guard<std::mutex> lock(m_observersMutex);
    return m_observers.size();
}

void TransferSubject::notifyObservers(const TransferEventData& eventData)
{
    std::lock_guard<std::mutex> lock(m_observersMutex);
    
    FT_LOG_DEBUG("TransferSubject", "Notifying " << m_observers.size() << " observers of event: " << static_cast<int>(eventData.event));
    
    // Create a copy to avoid holding the lock during notifications
    auto observers = m_observers;
    
    // Release lock before notifications to prevent deadlocks
    lock.~lock_guard();
    
    for (const auto& observer : observers) {
        if (observer) {
            try {
                observer->onTransferEvent(eventData);
            } catch (const std::exception& e) {
                FT_LOG_ERROR("TransferSubject", "Observer " << observer->getObserverId() << " threw exception: " << e.what());
            }
        }
    }
}

void TransferSubject::notifyProgress(const TransferProgress& progress)
{
    std::lock_guard<std::mutex> lock(m_observersMutex);
    
    // Create a copy to avoid holding the lock during notifications
    auto observers = m_observers;
    
    // Release lock before notifications
    lock.~lock_guard();
    
    for (const auto& observer : observers) {
        if (observer) {
            try {
                observer->onProgressUpdate(progress);
            } catch (const std::exception& e) {
                FT_LOG_ERROR("TransferSubject", "Observer " << observer->getObserverId() << " threw exception in progress update: " << e.what());
            }
        }
    }
}

void TransferSubject::notifyTransferStarted(const std::string& transferId, const std::string& filename)
{
    TransferEventData eventData(TransferEvent::STARTED, transferId, "Transfer started for: " + filename);
    eventData.progress.transferId = transferId;
    eventData.progress.filename = filename;
    notifyObservers(eventData);
}

void TransferSubject::notifyTransferCompleted(const std::string& transferId, const std::string& filename)
{
    TransferEventData eventData(TransferEvent::COMPLETED, transferId, "Transfer completed for: " + filename);
    eventData.progress.transferId = transferId;
    eventData.progress.filename = filename;
    notifyObservers(eventData);
}

void TransferSubject::notifyTransferFailed(const std::string& transferId, const std::string& error)
{
    TransferEventData eventData(TransferEvent::FAILED, transferId, "Transfer failed");
    eventData.error = error;
    notifyObservers(eventData);
}

void TransferSubject::notifyChunkCompleted(const std::string& transferId, size_t chunkIndex, size_t totalChunks)
{
    TransferEventData eventData(TransferEvent::CHUNK_COMPLETED, transferId, 
        "Completed chunk " + std::to_string(chunkIndex + 1) + "/" + std::to_string(totalChunks));
    eventData.progress.transferId = transferId;
    eventData.progress.chunksCompleted = chunkIndex + 1;
    eventData.progress.totalChunks = totalChunks;
    notifyObservers(eventData);
}

void TransferSubject::notifyMetadataReceived(const std::string& transferId, size_t totalBytes, size_t totalChunks)
{
    TransferEventData eventData(TransferEvent::METADATA_RECEIVED, transferId, 
        "Metadata received: " + std::to_string(totalBytes) + " bytes, " + std::to_string(totalChunks) + " chunks");
    eventData.progress.transferId = transferId;
    eventData.progress.totalBytes = totalBytes;
    eventData.progress.totalChunks = totalChunks;
    notifyObservers(eventData);
}

} // namespace i2p::filetransfer