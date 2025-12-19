#pragma once

#include <string>
#include <memory>
#include <vector>
#include <chrono>

namespace i2p::filetransfer
{

/**
 * @brief Transfer progress event data
 */
struct TransferProgress
{
    std::string transferId;
    std::string filename;
    size_t bytesTransferred{0};
    size_t totalBytes{0};
    size_t chunksCompleted{0};
    size_t totalChunks{0};
    std::chrono::steady_clock::time_point timestamp;
    
    // Helper methods
    [[nodiscard]] double getProgressPercentage() const {
        return totalBytes > 0 ? (static_cast<double>(bytesTransferred) / totalBytes) * 100.0 : 0.0;
    }
    
    [[nodiscard]] double getChunkProgress() const {
        return totalChunks > 0 ? (static_cast<double>(chunksCompleted) / totalChunks) * 100.0 : 0.0;
    }
};

/**
 * @brief Transfer status events
 */
enum class TransferEvent
{
    STARTED,
    PROGRESS_UPDATE,
    CHUNK_COMPLETED,
    METADATA_RECEIVED,
    VERIFICATION_STARTED,
    VERIFICATION_COMPLETED,
    COMPLETED,
    FAILED,
    CANCELLED
};

/**
 * @brief Transfer event data
 */
struct TransferEventData
{
    TransferEvent event;
    std::string transferId;
    std::string message;
    TransferProgress progress;
    std::string error; // For FAILED events
    std::chrono::steady_clock::time_point timestamp;
    
    TransferEventData(TransferEvent evt, const std::string& id, const std::string& msg = "")
        : event(evt), transferId(id), message(msg), timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Abstract observer interface for transfer events
 * Implements Observer Pattern
 */
class ITransferObserver
{
public:
    virtual ~ITransferObserver() = default;
    
    /**
     * @brief Called when a transfer event occurs
     * @param eventData Transfer event data
     */
    virtual void onTransferEvent(const TransferEventData& eventData) = 0;
    
    /**
     * @brief Called when transfer progress updates (high frequency)
     * @param progress Progress information
     */
    virtual void onProgressUpdate(const TransferProgress& progress) = 0;
    
    /**
     * @brief Get observer identifier for debugging
     */
    [[nodiscard]] virtual std::string getObserverId() const = 0;
};

/**
 * @brief Subject interface that can be observed for transfer events
 * Implements Subject part of Observer Pattern
 */
class ITransferSubject
{
public:
    virtual ~ITransferSubject() = default;
    
    /**
     * @brief Add an observer to the subject
     * @param observer Observer to add
     */
    virtual void addObserver(std::shared_ptr<ITransferObserver> observer) = 0;
    
    /**
     * @brief Remove an observer from the subject
     * @param observer Observer to remove
     */
    virtual void removeObserver(std::shared_ptr<ITransferObserver> observer) = 0;
    
    /**
     * @brief Remove all observers
     */
    virtual void clearObservers() = 0;
    
    /**
     * @brief Get count of registered observers
     */
    [[nodiscard]] virtual size_t getObserverCount() const = 0;

protected:
    /**
     * @brief Notify all observers of a transfer event
     * @param eventData Event data to send
     */
    virtual void notifyObservers(const TransferEventData& eventData) = 0;
    
    /**
     * @brief Notify all observers of progress update
     * @param progress Progress data to send
     */
    virtual void notifyProgress(const TransferProgress& progress) = 0;
};

} // namespace i2p::filetransfer