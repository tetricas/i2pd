#include "TransferCommandQueue.h"
#include "../core/FileTransferLogging.h"
#include "../transport/StreamingProviderRegistry.h"
#include <chrono>

namespace i2p::filetransfer
{

// TransferCommandQueue Implementation

TransferCommandQueue::TransferCommandQueue()
{
    FT_LOG_DEBUG("TransferCommandQueue", "Command queue created");
}

TransferCommandQueue::~TransferCommandQueue()
{
    if (m_running) {
        stopAsync();
    }
    FT_LOG_DEBUG("TransferCommandQueue", "Command queue destroyed");
}

void TransferCommandQueue::setResultCallback(CommandResultCallback callback)
{
    m_resultCallback = std::move(callback);
}

void TransferCommandQueue::enqueue(std::unique_ptr<ITransferCommand> command)
{
    if (!command) {
        FT_LOG_WARNING("TransferCommandQueue", "Attempted to enqueue null command");
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_queueMutex);
    FT_LOG_DEBUG("TransferCommandQueue", "Enqueuing command: " << command->getDescription());
    m_commandQueue.push(std::move(command));
    m_queueCondition.notify_one();
}

CommandTransferResult TransferCommandQueue::executeSync(std::unique_ptr<ITransferCommand> command)
{
    if (!command) {
        return CommandTransferResult::Error("Cannot execute null command");
    }
    
    FT_LOG_INFO("TransferCommandQueue", "Executing synchronous command: " << command->getDescription());
    
    auto start = std::chrono::steady_clock::now();
    CommandTransferResult result = command->execute();
    auto end = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Update statistics
    m_totalExecuted++;
    if (result.success) {
        m_totalSuccessful++;
        FT_LOG_INFO("TransferCommandQueue", "Command completed successfully in " << duration.count() << "ms");
    } else {
        m_totalFailed++;
        FT_LOG_ERROR("TransferCommandQueue", "Command failed: " << result.error);
    }
    
    return result;
}

void TransferCommandQueue::startAsync()
{
    if (m_running) {
        FT_LOG_WARNING("TransferCommandQueue", "Attempted to start already running queue");
        return;
    }
    
    m_running = true;
    m_executionThread = std::make_unique<std::thread>(&TransferCommandQueue::executionLoop, this);
    FT_LOG_INFO("TransferCommandQueue", "Asynchronous command processing started");
}

void TransferCommandQueue::stopAsync()
{
    if (!m_running) {
        return;
    }
    
    FT_LOG_INFO("TransferCommandQueue", "Stopping asynchronous command processing...");
    
    m_running = false;
    m_queueCondition.notify_all();
    
    if (m_executionThread && m_executionThread->joinable()) {
        m_executionThread->join();
    }
    
    FT_LOG_INFO("TransferCommandQueue", "Asynchronous processing stopped");
}

size_t TransferCommandQueue::getPendingCount() const
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_commandQueue.size();
}

TransferCommandQueue::Statistics TransferCommandQueue::getStatistics() const
{
    Statistics stats;
    stats.totalExecuted = m_totalExecuted;
    stats.totalSuccessful = m_totalSuccessful;
    stats.totalFailed = m_totalFailed;
    stats.pending = getPendingCount();
    return stats;
}

void TransferCommandQueue::clearQueue()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    size_t cleared = m_commandQueue.size();
    
    while (!m_commandQueue.empty()) {
        m_commandQueue.pop();
    }
    
    FT_LOG_INFO("TransferCommandQueue", "Cleared " << cleared << " pending commands");
}

bool TransferCommandQueue::waitForCompletion(int timeoutMs)
{
    if (!m_running) {
        return getPendingCount() == 0;
    }
    
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeoutMs);
    
    while (getPendingCount() > 0) {
        auto now = std::chrono::steady_clock::now();
        if (now - start >= timeout) {
            FT_LOG_WARNING("TransferCommandQueue", "Wait for completion timed out");
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return true;
}

void TransferCommandQueue::executionLoop()
{
    FT_LOG_DEBUG("TransferCommandQueue", "Execution loop started");
    
    while (m_running) {
        std::unique_ptr<ITransferCommand> command;
        
        // Wait for command or shutdown
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCondition.wait(lock, [this] { return !m_commandQueue.empty() || !m_running; });
            
            if (!m_running) {
                break;
            }
            
            if (!m_commandQueue.empty()) {
                command = std::move(m_commandQueue.front());
                m_commandQueue.pop();
            }
        }
        
        // Execute command if we got one
        if (command) {
            FT_LOG_DEBUG("TransferCommandQueue", "Executing async command: " << command->getDescription());
            
            auto start = std::chrono::steady_clock::now();
            CommandTransferResult result = command->execute();
            auto end = std::chrono::steady_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            // Update statistics
            m_totalExecuted++;
            if (result.success) {
                m_totalSuccessful++;
                FT_LOG_DEBUG("TransferCommandQueue", "Async command completed successfully in " << duration.count() << "ms");
            } else {
                m_totalFailed++;
                FT_LOG_ERROR("TransferCommandQueue", "Async command failed: " << result.error);
            }
            
            // Call result callback if set
            if (m_resultCallback) {
                try {
                    // Generate a command ID for callback (simplified)
                    std::string commandId = "ASYNC_" + std::to_string(m_totalExecuted);
                    m_resultCallback(commandId, result);
                } catch (const std::exception& e) {
                    FT_LOG_ERROR("TransferCommandQueue", "Exception in result callback: " << e.what());
                }
            }
        }
    }
    
    FT_LOG_DEBUG("TransferCommandQueue", "Execution loop ended");
}

// TransferCommandBuilder Implementation

TransferCommandBuilder::TransferCommandBuilder(std::string description)
    : m_description(std::move(description))
{
    FT_LOG_DEBUG("TransferCommandBuilder", "Created command builder: " << m_description);
}

TransferCommandBuilder& TransferCommandBuilder::download(const std::string& filename, const std::string& serverB32, const std::string& strategy)
{
    // Create default context
    TransferCommandContext context(filename, serverB32);
    context.strategyName = strategy;
    
    // Message provider will be set when destination is available
    // For now, leave it nullptr - it should be set before command execution
    context.messageProvider = nullptr;
    
    auto command = std::make_unique<DownloadFileCommand>(std::move(context));
    m_commands.push_back(std::move(command));
    
    FT_LOG_DEBUG("TransferCommandBuilder", "Added download command for: " << filename);
    return *this;
}

TransferCommandBuilder& TransferCommandBuilder::upload(const std::string& localFile, const std::string& remoteFile, 
                                                     const std::string& serverB32, const std::string& strategy)
{
    TransferCommandContext context(remoteFile, serverB32);
    context.strategyName = strategy;
    
    // Message provider will be set when destination is available
    context.messageProvider = nullptr;
    
    auto command = std::make_unique<UploadFileCommand>(std::move(context), localFile);
    m_commands.push_back(std::move(command));
    
    FT_LOG_DEBUG("TransferCommandBuilder", "Added upload command: " << localFile << " -> " << remoteFile);
    return *this;
}

TransferCommandBuilder& TransferCommandBuilder::verify(const std::string& filePath, const std::string& expectedHash)
{
    auto command = std::make_unique<VerifyFileCommand>(filePath, expectedHash);
    m_commands.push_back(std::move(command));
    
    FT_LOG_DEBUG("TransferCommandBuilder", "Added verify command for: " << filePath);
    return *this;
}

TransferCommandBuilder& TransferCommandBuilder::addCommand(std::unique_ptr<ITransferCommand> command)
{
    if (command) {
        FT_LOG_DEBUG("TransferCommandBuilder", "Added custom command: " << command->getDescription());
        m_commands.push_back(std::move(command));
    }
    return *this;
}

TransferCommandBuilder& TransferCommandBuilder::stopOnFailure(bool stop)
{
    m_stopOnFirstFailure = stop;
    return *this;
}

std::unique_ptr<CompositeTransferCommand> TransferCommandBuilder::buildComposite()
{
    auto composite = std::make_unique<CompositeTransferCommand>(m_description, m_stopOnFirstFailure);
    
    for (auto& command : m_commands) {
        composite->addCommand(std::move(command));
    }
    
    m_commands.clear();
    FT_LOG_DEBUG("TransferCommandBuilder", "Built composite command with " << composite->getCommandCount() << " subcommands");
    
    return composite;
}

std::vector<CommandTransferResult> TransferCommandBuilder::execute()
{
    std::vector<CommandTransferResult> results;
    results.reserve(m_commands.size());
    
    FT_LOG_INFO("TransferCommandBuilder", "Executing " << m_commands.size() << " commands");
    
    for (auto& command : m_commands) {
        if (command) {
            CommandTransferResult result = command->execute();
            results.push_back(std::move(result));
            
            // Stop on failure if configured
            if (m_stopOnFirstFailure && !results.back().success) {
                FT_LOG_WARNING("TransferCommandBuilder", "Stopping execution due to command failure");
                break;
            }
        }
    }
    
    m_commands.clear();
    FT_LOG_INFO("TransferCommandBuilder", "Command sequence completed with " << results.size() << " results");
    
    return results;
}

} // namespace i2p::filetransfer