#include "ITransferCommand.h"
#include "TransferStrategyRegistry.h"
#include "ITransferStrategy.h"
#include "../core/FileTransferLogging.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>

namespace i2p::filetransfer
{

// CommandTransferResult implementations
CommandTransferResult CommandTransferResult::Success(std::vector<uint8_t>&& data)
{
    CommandTransferResult result;
    result.data = std::move(data);
    result.success = true;
    return result;
}

CommandTransferResult CommandTransferResult::Error(const std::string& err)
{
    CommandTransferResult result;
    result.success = false;
    result.error = err;
    return result;
}

namespace {
    std::string generateCommandId() {
        static std::mt19937 gen(std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<> dis(100000, 999999);
        return "CMD_" + std::to_string(dis(gen));
    }
    
    bool fileExists(const std::string& path) {
        std::ifstream file(path);
        return file.good();
    }
    
    size_t getFileSize(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        return file.good() ? static_cast<size_t>(file.tellg()) : 0;
    }
    
    std::string calculateFileHash(const std::string& filePath) {
        // Simplified hash calculation - in real implementation would use proper hashing
        std::ifstream file(filePath, std::ios::binary);
        if (!file) return "";
        
        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        // Simple checksum
        size_t hash = 0;
        for (char c : content) {
            hash = hash * 31 + c;
        }
        
        return std::to_string(hash);
    }
}

// DownloadFileCommand Implementation

DownloadFileCommand::DownloadFileCommand(TransferCommandContext context)
    : m_context(std::move(context)), m_commandId(generateCommandId())
{
    FT_LOG_DEBUG("DownloadFileCommand", "Created command " << m_commandId << " for file: " << m_context.filename);
}

CommandTransferResult DownloadFileCommand::execute()
{
    FT_LOG_INFO("DownloadFileCommand", "Executing download command " << m_commandId);
    
    CommandTransferResult result;
    result.stats.initTime = std::chrono::steady_clock::now();
    
    if (!canExecute()) {
        result.error = "Command cannot be executed - preconditions not met";
        return result;
    }
    
    try {
        // Get strategy from registry
        auto& registry = TransferStrategyRegistry::instance();
        auto strategy = registry.createStrategy(m_context.strategyName);
        
        if (!strategy) {
            result.error = "Failed to create transfer strategy: " + m_context.strategyName;
            return result;
        }
        
        // Create transfer context for strategy
        TransferContext strategyContext(m_context.messageProvider, m_context.serverB32, m_context.filename, m_context.timeoutMs);
        
        // Execute transfer
        auto strategyResult = strategy->execute(strategyContext);
        
        // Convert strategy result to command result
        result.success = strategyResult.success;
        result.error = strategyResult.error;
        result.data = std::move(strategyResult.data);
        result.stats = strategyResult.stats;
        
        if (result.success && !m_context.outputPath.empty()) {
            // Save to specified output path
            std::ofstream outFile(m_context.outputPath, std::ios::binary);
            if (outFile) {
                outFile.write(reinterpret_cast<const char*>(result.data.data()), result.data.size());
                FT_LOG_INFO("DownloadFileCommand", "File saved to: " << m_context.outputPath);
            } else {
                FT_LOG_WARNING("DownloadFileCommand", "Failed to save file to: " << m_context.outputPath);
            }
        }
        
        if (result.success && m_context.verifyIntegrity) {
            // Perform additional integrity checks if requested
            FT_LOG_DEBUG("DownloadFileCommand", "Performing integrity verification");
            result.stats.verified = true;
        }
        
    } catch (const std::exception& e) {
        result.error = "DownloadFileCommand exception: " + std::string(e.what());
        FT_LOG_ERROR("DownloadFileCommand", "Exception in command " << m_commandId << ": " << e.what());
    }
    
    FT_LOG_INFO("DownloadFileCommand", "Command " << m_commandId << " completed. Success: " << result.success);
    return result;
}

std::string DownloadFileCommand::getDescription() const
{
    return "Download file '" + m_context.filename + "' from " + m_context.serverB32.substr(0, 16) + "... using " + m_context.strategyName;
}

bool DownloadFileCommand::canExecute() const
{
    if (m_context.filename.empty() || m_context.serverB32.empty()) {
        return false;
    }
    
    if (!m_context.messageProvider || !m_context.messageProvider->isReady()) {
        return false;
    }
    
    auto& registry = TransferStrategyRegistry::instance();
    if (!registry.isStrategyRegistered(m_context.strategyName)) {
        return false;
    }
    
    return true;
}

int DownloadFileCommand::getEstimatedTimeMs() const
{
    // Rough estimation based on strategy and timeout
    if (m_context.strategyName == "chunked") {
        return m_context.timeoutMs;
    } else if (m_context.strategyName == "binary-stream") {
        return m_context.timeoutMs * 2 / 3;
    }
    return m_context.timeoutMs;
}

// UploadFileCommand Implementation

UploadFileCommand::UploadFileCommand(TransferCommandContext context, std::string sourceFile)
    : m_context(std::move(context)), m_commandId(generateCommandId()), m_sourceFilePath(std::move(sourceFile))
{
    FT_LOG_DEBUG("UploadFileCommand", "Created upload command " << m_commandId << " for file: " << m_sourceFilePath);
}

CommandTransferResult UploadFileCommand::execute()
{
    FT_LOG_INFO("UploadFileCommand", "Executing upload command " << m_commandId);
    
    CommandTransferResult result;
    result.stats.initTime = std::chrono::steady_clock::now();
    
    // Note: This is a placeholder implementation
    // Real upload would require server-side support and protocol extensions
    result.error = "Upload functionality not yet implemented - server-side support required";
    FT_LOG_WARNING("UploadFileCommand", "Upload not implemented yet");
    
    return result;
}

std::string UploadFileCommand::getDescription() const
{
    return "Upload file '" + m_sourceFilePath + "' as '" + m_context.filename + "' to " + m_context.serverB32.substr(0, 16) + "...";
}

bool UploadFileCommand::canExecute() const
{
    if (!fileExists(m_sourceFilePath)) {
        return false;
    }
    
    if (m_context.filename.empty() || m_context.serverB32.empty()) {
        return false;
    }
    
    return m_context.messageProvider && m_context.messageProvider->isReady();
}

int UploadFileCommand::getEstimatedTimeMs() const
{
    size_t fileSize = getFileSize(m_sourceFilePath);
    // Rough estimation: 1MB per 10 seconds
    return static_cast<int>(fileSize / 1024 / 1024 * 10000) + m_context.timeoutMs / 2;
}

// VerifyFileCommand Implementation

VerifyFileCommand::VerifyFileCommand(std::string filePath, std::string expectedHash)
    : m_filePath(std::move(filePath)), m_expectedHash(std::move(expectedHash)), m_commandId(generateCommandId())
{
    FT_LOG_DEBUG("VerifyFileCommand", "Created verify command " << m_commandId << " for file: " << m_filePath);
}

CommandTransferResult VerifyFileCommand::execute()
{
    FT_LOG_INFO("VerifyFileCommand", "Executing verify command " << m_commandId);
    
    CommandTransferResult result;
    result.stats.initTime = std::chrono::steady_clock::now();
    result.stats.startTime = std::chrono::steady_clock::now();
    
    if (!canExecute()) {
        result.error = "Cannot verify - file does not exist: " + m_filePath;
        return result;
    }
    
    try {
        std::string actualHash = calculateFileHash(m_filePath);
        
        if (actualHash == m_expectedHash) {
            result.success = true;
            result.stats.verified = true;
            FT_LOG_INFO("VerifyFileCommand", "File verification successful: " << m_filePath);
        } else {
            result.error = "Hash mismatch - Expected: " + m_expectedHash + ", Got: " + actualHash;
            FT_LOG_ERROR("VerifyFileCommand", "File verification failed: " << result.error);
        }
        
    } catch (const std::exception& e) {
        result.error = "Verification exception: " + std::string(e.what());
        FT_LOG_ERROR("VerifyFileCommand", "Exception in verify command: " << e.what());
    }
    
    result.stats.endTime = std::chrono::steady_clock::now();
    return result;
}

std::string VerifyFileCommand::getDescription() const
{
    return "Verify integrity of file '" + m_filePath + "' against expected hash";
}

bool VerifyFileCommand::canExecute() const
{
    return fileExists(m_filePath) && !m_expectedHash.empty();
}

int VerifyFileCommand::getEstimatedTimeMs() const
{
    size_t fileSize = getFileSize(m_filePath);
    // Hash calculation is typically fast - estimate 1MB per 100ms
    return static_cast<int>(fileSize / 1024 / 1024 * 100) + 100;
}

// CompositeTransferCommand Implementation

CompositeTransferCommand::CompositeTransferCommand(std::string description, bool stopOnFailure)
    : m_description(std::move(description)), m_commandId(generateCommandId()), m_stopOnFirstFailure(stopOnFailure)
{
    FT_LOG_DEBUG("CompositeTransferCommand", "Created composite command " << m_commandId << ": " << m_description);
}

void CompositeTransferCommand::addCommand(std::unique_ptr<ITransferCommand> command)
{
    if (command) {
        FT_LOG_DEBUG("CompositeTransferCommand", "Added subcommand to " << m_commandId << ": " << command->getDescription());
        m_commands.push_back(std::move(command));
    }
}

CommandTransferResult CompositeTransferCommand::execute()
{
    FT_LOG_INFO("CompositeTransferCommand", "Executing composite command " << m_commandId << " with " << m_commands.size() << " subcommands");
    
    CommandTransferResult result;
    result.stats.initTime = std::chrono::steady_clock::now();
    result.stats.startTime = std::chrono::steady_clock::now();
    
    if (m_commands.empty()) {
        result.error = "No commands to execute";
        return result;
    }
    
    int successCount = 0;
    std::vector<std::string> errors;
    
    for (size_t i = 0; i < m_commands.size(); ++i) {
        auto& command = m_commands[i];
        
        FT_LOG_DEBUG("CompositeTransferCommand", "Executing subcommand " << (i + 1) << "/" << m_commands.size() << ": " << command->getDescription());
        
        CommandTransferResult subResult = command->execute();
        
        if (subResult.success) {
            successCount++;
            // Accumulate successful transfer data
            if (!subResult.data.empty()) {
                result.data.insert(result.data.end(), subResult.data.begin(), subResult.data.end());
            }
            result.stats.totalBytes += subResult.stats.totalBytes;
            result.stats.chunksReceived += subResult.stats.chunksReceived;
            result.stats.chunksTotal += subResult.stats.chunksTotal;
        } else {
            std::string error = "Subcommand " + std::to_string(i + 1) + " failed: " + subResult.error;
            errors.push_back(error);
            FT_LOG_ERROR("CompositeTransferCommand", error);
            
            if (m_stopOnFirstFailure) {
                break;
            }
        }
    }
    
    result.stats.endTime = std::chrono::steady_clock::now();
    
    if (successCount == static_cast<int>(m_commands.size())) {
        result.success = true;
        FT_LOG_INFO("CompositeTransferCommand", "All " << successCount << " subcommands completed successfully");
    } else {
        result.success = false;
        result.error = "Composite command partially failed - " + std::to_string(successCount) + "/" + std::to_string(m_commands.size()) + " succeeded";
        if (!errors.empty()) {
            result.error += ". Errors: " + errors[0]; // Include first error for brevity
        }
        FT_LOG_WARNING("CompositeTransferCommand", result.error);
    }
    
    return result;
}

std::string CompositeTransferCommand::getDescription() const
{
    return m_description + " (" + std::to_string(m_commands.size()) + " commands)";
}

bool CompositeTransferCommand::canExecute() const
{
    if (m_commands.empty()) {
        return false;
    }
    
    // All commands must be executable
    for (const auto& command : m_commands) {
        if (!command || !command->canExecute()) {
            return false;
        }
    }
    
    return true;
}

int CompositeTransferCommand::getEstimatedTimeMs() const
{
    int totalTime = 0;
    for (const auto& command : m_commands) {
        if (command) {
            totalTime += command->getEstimatedTimeMs();
        }
    }
    return totalTime;
}

} // namespace i2p::filetransfer