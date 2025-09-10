#pragma once

#include "ITransferCommand.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

namespace i2p::filetransfer
{

/**
 * @brief Command execution result callback
 */
using CommandResultCallback = std::function<void(const std::string& commandId, const CommandTransferResult& result)>;

/**
 * @brief Queue for managing and executing transfer commands
 * Supports both synchronous and asynchronous execution
 */
class TransferCommandQueue
{
private:
    std::queue<std::unique_ptr<ITransferCommand>> m_commandQueue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    
    std::atomic<bool> m_running{false};
    std::unique_ptr<std::thread> m_executionThread;
    
    CommandResultCallback m_resultCallback;
    
    // Statistics
    std::atomic<size_t> m_totalExecuted{0};
    std::atomic<size_t> m_totalSuccessful{0};
    std::atomic<size_t> m_totalFailed{0};
    
    void executionLoop();
    
public:
    TransferCommandQueue();
    ~TransferCommandQueue();
    
    /**
     * @brief Set callback for command execution results
     */
    void setResultCallback(CommandResultCallback callback);
    
    /**
     * @brief Add command to queue
     */
    void enqueue(std::unique_ptr<ITransferCommand> command);
    
    /**
     * @brief Execute single command synchronously
     */
    CommandTransferResult executeSync(std::unique_ptr<ITransferCommand> command);
    
    /**
     * @brief Start asynchronous command processing
     */
    void startAsync();
    
    /**
     * @brief Stop asynchronous processing (waits for current command to complete)
     */
    void stopAsync();
    
    /**
     * @brief Check if queue is running asynchronously
     */
    [[nodiscard]] bool isRunning() const { return m_running; }
    
    /**
     * @brief Get number of pending commands
     */
    [[nodiscard]] size_t getPendingCount() const;
    
    /**
     * @brief Get execution statistics
     */
    struct Statistics {
        size_t totalExecuted;
        size_t totalSuccessful;
        size_t totalFailed;
        size_t pending;
        
        [[nodiscard]] double getSuccessRate() const {
            return totalExecuted > 0 ? (static_cast<double>(totalSuccessful) / totalExecuted) * 100.0 : 0.0;
        }
    };
    
    [[nodiscard]] Statistics getStatistics() const;
    
    /**
     * @brief Clear all pending commands
     */
    void clearQueue();
    
    /**
     * @brief Wait for all pending commands to complete (only works in async mode)
     */
    bool waitForCompletion(int timeoutMs = 30000);
    
    // Disable copy and move
    TransferCommandQueue(const TransferCommandQueue&) = delete;
    TransferCommandQueue& operator=(const TransferCommandQueue&) = delete;
    TransferCommandQueue(TransferCommandQueue&&) = delete;
    TransferCommandQueue& operator=(TransferCommandQueue&&) = delete;
};

/**
 * @brief Helper class for building and executing command sequences
 */
class TransferCommandBuilder
{
private:
    std::vector<std::unique_ptr<ITransferCommand>> m_commands;
    std::string m_description;
    bool m_stopOnFirstFailure{true};
    
public:
    explicit TransferCommandBuilder(std::string description = "Command sequence");
    
    /**
     * @brief Add download command
     */
    TransferCommandBuilder& download(const std::string& filename, const std::string& serverB32, 
                                   const std::string& strategy = "chunked");
    
    /**
     * @brief Add upload command
     */
    TransferCommandBuilder& upload(const std::string& localFile, const std::string& remoteFile,
                                 const std::string& serverB32, const std::string& strategy = "chunked");
    
    /**
     * @brief Add verify command
     */
    TransferCommandBuilder& verify(const std::string& filePath, const std::string& expectedHash);
    
    /**
     * @brief Add custom command
     */
    TransferCommandBuilder& addCommand(std::unique_ptr<ITransferCommand> command);
    
    /**
     * @brief Set whether to stop on first failure
     */
    TransferCommandBuilder& stopOnFailure(bool stop);
    
    /**
     * @brief Build composite command
     */
    std::unique_ptr<CompositeTransferCommand> buildComposite();
    
    /**
     * @brief Execute all commands sequentially and return results
     */
    std::vector<CommandTransferResult> execute();
    
    /**
     * @brief Get number of commands in builder
     */
    [[nodiscard]] size_t getCommandCount() const { return m_commands.size(); }
};

} // namespace i2p::filetransfer