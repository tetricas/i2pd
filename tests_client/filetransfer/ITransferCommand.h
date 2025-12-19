#pragma once

#include "../core/TransferResult.h"
#include "../transport/IStreamingProvider.h"
#include "FileTransferProtocol.h"
#include <string>
#include <memory>
#include <vector>
#include <chrono>

namespace i2p::filetransfer
{

// Define concrete TransferResult type for commands
struct CommandTransferResult {
    std::vector<uint8_t> data;
    bool success{false};
    std::string error;
    TransferStats stats;
    
    static CommandTransferResult Success(std::vector<uint8_t>&& data);
    static CommandTransferResult Error(const std::string& err);
};

/**
 * @brief Base class for file transfer commands
 * Implements Command Pattern for transfer operations
 */
class ITransferCommand
{
public:
    virtual ~ITransferCommand() = default;
    
    /**
     * @brief Execute the transfer command
     * @return Transfer result
     */
    virtual CommandTransferResult execute() = 0;
    
    /**
     * @brief Get command description for logging/debugging
     */
    [[nodiscard]] virtual std::string getDescription() const = 0;
    
    /**
     * @brief Check if command can be executed
     */
    [[nodiscard]] virtual bool canExecute() const = 0;
    
    /**
     * @brief Get estimated execution time in milliseconds
     */
    [[nodiscard]] virtual int getEstimatedTimeMs() const = 0;
};

/**
 * @brief Context data for transfer commands
 */
struct TransferCommandContext
{
    std::string filename;
    std::string serverB32;
    std::string strategyName;
    std::shared_ptr<i2p::transport::IStreamingMessageProvider> messageProvider;
    int timeoutMs{30000};
    bool verifyIntegrity{true};
    std::string outputPath;
    
    TransferCommandContext(const std::string& file, const std::string& server)
        : filename(file), serverB32(server) {}
};

/**
 * @brief Command for downloading a file
 */
class DownloadFileCommand : public ITransferCommand
{
private:
    TransferCommandContext m_context;
    std::string m_commandId;
    
public:
    explicit DownloadFileCommand(TransferCommandContext context);
    
    CommandTransferResult execute() override;
    [[nodiscard]] std::string getDescription() const override;
    [[nodiscard]] bool canExecute() const override;
    [[nodiscard]] int getEstimatedTimeMs() const override;
    
    [[nodiscard]] const std::string& getCommandId() const { return m_commandId; }
    [[nodiscard]] const TransferCommandContext& getContext() const { return m_context; }
};

/**
 * @brief Command for uploading a file
 */
class UploadFileCommand : public ITransferCommand
{
private:
    TransferCommandContext m_context;
    std::string m_commandId;
    std::string m_sourceFilePath;
    
public:
    UploadFileCommand(TransferCommandContext context, std::string sourceFile);
    
    CommandTransferResult execute() override;
    [[nodiscard]] std::string getDescription() const override;
    [[nodiscard]] bool canExecute() const override;
    [[nodiscard]] int getEstimatedTimeMs() const override;
    
    [[nodiscard]] const std::string& getCommandId() const { return m_commandId; }
    [[nodiscard]] const std::string& getSourceFile() const { return m_sourceFilePath; }
};

/**
 * @brief Command for verifying file integrity
 */
class VerifyFileCommand : public ITransferCommand
{
private:
    std::string m_filePath;
    std::string m_expectedHash;
    std::string m_commandId;
    
public:
    VerifyFileCommand(std::string filePath, std::string expectedHash);
    
    CommandTransferResult execute() override;
    [[nodiscard]] std::string getDescription() const override;
    [[nodiscard]] bool canExecute() const override;
    [[nodiscard]] int getEstimatedTimeMs() const override;
    
    [[nodiscard]] const std::string& getCommandId() const { return m_commandId; }
};

/**
 * @brief Composite command for executing multiple commands in sequence
 */
class CompositeTransferCommand : public ITransferCommand
{
private:
    std::vector<std::unique_ptr<ITransferCommand>> m_commands;
    std::string m_description;
    std::string m_commandId;
    bool m_stopOnFirstFailure{true};
    
public:
    explicit CompositeTransferCommand(std::string description, bool stopOnFailure = true);
    
    void addCommand(std::unique_ptr<ITransferCommand> command);
    
    CommandTransferResult execute() override;
    [[nodiscard]] std::string getDescription() const override;
    [[nodiscard]] bool canExecute() const override;
    [[nodiscard]] int getEstimatedTimeMs() const override;
    
    [[nodiscard]] size_t getCommandCount() const { return m_commands.size(); }
};

} // namespace i2p::filetransfer