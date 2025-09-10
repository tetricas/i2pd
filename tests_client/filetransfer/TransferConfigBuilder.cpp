#include "TransferConfigBuilder.h"
#include "TransferStrategyRegistry.h"
#include "../transport/StreamingProviderRegistry.h"
#include "../core/FileTransferLogging.h"
#include <stdexcept>
#include <algorithm>
#include <sstream>

namespace i2p::filetransfer
{

// TransferConfigBuilder Implementation

TransferConfigBuilder::TransferConfigBuilder()
{
    // Initialize with safe defaults
    m_config.connectionTimeoutMs = TransferConfig::getConnectionTimeout();
    m_config.transferTimeoutMs = TransferConfig::getTransferTimeout();
    m_config.maxRetries = TransferConfig::getMaxRetries();
    m_config.retryDelayMs = TransferConfig::getRetryDelayMs();
    m_config.chunkSize = 8192; // 8KB default
    
    FT_LOG_DEBUG("TransferConfigBuilder", "Created new config builder with defaults");
}

void TransferConfigBuilder::validateAndUpdateErrors()
{
    std::ostringstream errors;
    
    if (m_config.filename.empty()) {
        errors << "Filename is required. ";
    }
    
    if (m_config.serverB32.empty()) {
        errors << "Server B32 address is required. ";
    }
    
    if (m_config.strategyName.empty()) {
        errors << "Strategy name is required. ";
    } else {
        auto& registry = TransferStrategyRegistry::instance();
        if (!registry.isStrategyRegistered(m_config.strategyName)) {
            errors << "Strategy '" << m_config.strategyName << "' is not registered. ";
        }
    }
    
    if (m_config.connectionTimeoutMs <= 0) {
        errors << "Connection timeout must be positive. ";
    }
    
    if (m_config.transferTimeoutMs <= 0) {
        errors << "Transfer timeout must be positive. ";
    }
    
    if (m_config.maxRetries < 0) {
        errors << "Max retries cannot be negative. ";
    }
    
    if (m_config.chunkSize == 0 || m_config.chunkSize > 1024 * 1024) {
        errors << "Chunk size must be between 1 and 1MB. ";
    }
    
    m_validationErrors = errors.str();
}

TransferConfigBuilder& TransferConfigBuilder::forFile(const std::string& filename)
{
    m_config.filename = filename;
    FT_LOG_DEBUG("TransferConfigBuilder", "Set filename: " << filename);
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::fromServer(const std::string& serverB32)
{
    m_config.serverB32 = serverB32;
    FT_LOG_DEBUG("TransferConfigBuilder", "Set server: " << serverB32.substr(0, 16) << "...");
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::usingStrategy(const std::string& strategyName)
{
    m_config.strategyName = strategyName;
    FT_LOG_DEBUG("TransferConfigBuilder", "Set strategy: " << strategyName);
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::saveToPath(const std::string& outputPath)
{
    m_config.outputPath = outputPath;
    FT_LOG_DEBUG("TransferConfigBuilder", "Set output path: " << outputPath);
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withConnectionTimeout(int timeoutMs)
{
    m_config.connectionTimeoutMs = timeoutMs;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withTransferTimeout(int timeoutMs)
{
    m_config.transferTimeoutMs = timeoutMs;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withMaxRetries(int retries)
{
    m_config.maxRetries = retries;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withRetryDelay(int delayMs)
{
    m_config.retryDelayMs = delayMs;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withMaxConcurrentTransfers(size_t count)
{
    m_config.maxConcurrentTransfers = count;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withChunkSize(size_t size)
{
    m_config.chunkSize = size;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::enableCompression(bool enable)
{
    m_config.enableCompression = enable;
    FT_LOG_DEBUG("TransferConfigBuilder", "Compression " << (enable ? "enabled" : "disabled"));
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::verifyIntegrity(bool verify)
{
    m_config.verifyIntegrity = verify;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withExpectedHash(const std::string& hash)
{
    m_config.expectedHash = hash;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::useEncryption(bool encrypt)
{
    m_config.useEncryption = encrypt;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::enableProgressReporting(bool enable)
{
    m_config.enableProgressReporting = enable;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withProgressInterval(int intervalMs)
{
    m_config.progressUpdateIntervalMs = intervalMs;
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::addObserver(std::shared_ptr<ITransferObserver> observer)
{
    if (observer) {
        m_config.observers.push_back(observer);
        FT_LOG_DEBUG("TransferConfigBuilder", "Added observer: " << observer->getObserverId());
    }
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withMessageProvider(std::shared_ptr<i2p::transport::IStreamingMessageProvider> provider)
{
    m_config.messageProvider = provider;
    if (provider) {
        FT_LOG_DEBUG("TransferConfigBuilder", "Set custom message provider");
    }
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::usingProtocol(const std::string& protocolName)
{
    m_config.protocolName = protocolName;
    
    // Auto-set message provider if not already set
    // Note: Provider will be created when destination is available
    // For now, we just store the protocol name
    
    FT_LOG_DEBUG("TransferConfigBuilder", "Set protocol: " << protocolName);
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::withCustomOption(const std::string& key, const std::string& value)
{
    m_config.customOptions[key] = value;
    FT_LOG_DEBUG("TransferConfigBuilder", "Added custom option: " << key << " = " << value);
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::enableResume(const std::string& checkpointPath)
{
    m_config.resumeTransfer = true;
    m_config.resumeCheckpointPath = checkpointPath;
    FT_LOG_DEBUG("TransferConfigBuilder", "Enabled resume with checkpoint: " << checkpointPath);
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::useHighPerformancePreset()
{
    m_config.maxConcurrentTransfers = 4;
    m_config.chunkSize = 32768; // 32KB
    m_config.connectionTimeoutMs = 15000;
    m_config.transferTimeoutMs = 120000; // 2 minutes
    m_config.maxRetries = 5;
    m_config.retryDelayMs = 500;
    m_config.enableCompression = true;
    m_config.progressUpdateIntervalMs = 250; // More frequent updates
    
    FT_LOG_DEBUG("TransferConfigBuilder", "Applied high performance preset");
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::useSecurePreset()
{
    m_config.useEncryption = true;
    m_config.verifyIntegrity = true;
    m_config.maxRetries = 3;
    m_config.connectionTimeoutMs = 30000; // More conservative
    m_config.transferTimeoutMs = 90000;
    m_config.chunkSize = 4096; // Smaller chunks for security
    
    FT_LOG_DEBUG("TransferConfigBuilder", "Applied secure preset");
    return *this;
}

TransferConfigBuilder& TransferConfigBuilder::useDebugPreset()
{
    m_config.enableProgressReporting = true;
    m_config.progressUpdateIntervalMs = 1000;
    m_config.maxRetries = 1; // Fail fast for debugging
    m_config.connectionTimeoutMs = 10000;
    m_config.transferTimeoutMs = 30000;
    
    FT_LOG_DEBUG("TransferConfigBuilder", "Applied debug preset");
    return *this;
}

bool TransferConfigBuilder::isValid() const
{
    const_cast<TransferConfigBuilder*>(this)->validateAndUpdateErrors();
    return m_validationErrors.empty();
}

AdvancedTransferConfig TransferConfigBuilder::build()
{
    validateAndUpdateErrors();
    if (!m_validationErrors.empty()) {
        throw std::runtime_error("Invalid transfer configuration: " + m_validationErrors);
    }
    
    // Note: Message provider should be set by the application when destination is available
    // The configuration is still valid without it - it will be created at runtime
    
    FT_LOG_INFO("TransferConfigBuilder", "Built valid config for: " << m_config.filename);
    return m_config;
}

TransferConfigBuilder& TransferConfigBuilder::reset()
{
    m_config = AdvancedTransferConfig{};
    m_validationErrors.clear();
    
    // Restore defaults
    m_config.connectionTimeoutMs = TransferConfig::getConnectionTimeout();
    m_config.transferTimeoutMs = TransferConfig::getTransferTimeout();
    m_config.maxRetries = TransferConfig::getMaxRetries();
    m_config.retryDelayMs = TransferConfig::getRetryDelayMs();
    m_config.chunkSize = 8192;
    
    return *this;
}

TransferConfigBuilder TransferConfigBuilder::copy() const
{
    TransferConfigBuilder copy;
    copy.m_config = m_config;
    copy.m_validationErrors = m_validationErrors;
    return copy;
}

TransferConfigBuilder TransferConfigBuilder::forDownload(const std::string& filename, const std::string& serverB32)
{
    return TransferConfigBuilder()
        .forFile(filename)
        .fromServer(serverB32)
        .usingStrategy("chunked")
        .usingProtocol("simple");
}

TransferConfigBuilder TransferConfigBuilder::forUpload(const std::string& filename, const std::string& serverB32)
{
    return TransferConfigBuilder()
        .forFile(filename)
        .fromServer(serverB32)
        .usingStrategy("binary-stream")
        .usingProtocol("normal");
}

TransferConfigBuilder TransferConfigBuilder::fromDefaults()
{
    return TransferConfigBuilder();
}

// BatchTransferConfigBuilder Implementation

BatchTransferConfigBuilder::BatchTransferConfigBuilder()
{
    FT_LOG_DEBUG("BatchTransferConfigBuilder", "Created batch config builder");
}

BatchTransferConfigBuilder& BatchTransferConfigBuilder::withTemplate(const TransferConfigBuilder& templateBuilder)
{
    m_templateBuilder = templateBuilder;
    FT_LOG_DEBUG("BatchTransferConfigBuilder", "Set template configuration");
    return *this;
}

BatchTransferConfigBuilder& BatchTransferConfigBuilder::addFile(const std::string& filename, const std::string& serverB32)
{
    auto builder = m_templateBuilder.copy();
    
    builder.forFile(filename);
    if (!serverB32.empty()) {
        builder.fromServer(serverB32);
    }
    
    try {
        m_configs.push_back(builder.build());
        FT_LOG_DEBUG("BatchTransferConfigBuilder", "Added file: " << filename);
    } catch (const std::exception& e) {
        FT_LOG_ERROR("BatchTransferConfigBuilder", "Failed to add file " << filename << ": " << e.what());
    }
    
    return *this;
}

BatchTransferConfigBuilder& BatchTransferConfigBuilder::addFiles(const std::vector<std::string>& filenames, const std::string& serverB32)
{
    for (const auto& filename : filenames) {
        addFile(filename, serverB32);
    }
    return *this;
}

BatchTransferConfigBuilder& BatchTransferConfigBuilder::addFileWithConfig(const std::string& filename, 
                                                                         const std::function<void(TransferConfigBuilder&)>& configFunc)
{
    auto builder = m_templateBuilder.copy();
    builder.forFile(filename);
    
    if (configFunc) {
        configFunc(builder);
    }
    
    try {
        m_configs.push_back(builder.build());
        FT_LOG_DEBUG("BatchTransferConfigBuilder", "Added file with custom config: " << filename);
    } catch (const std::exception& e) {
        FT_LOG_ERROR("BatchTransferConfigBuilder", "Failed to add file " << filename << ": " << e.what());
    }
    
    return *this;
}

std::vector<AdvancedTransferConfig> BatchTransferConfigBuilder::build()
{
    FT_LOG_INFO("BatchTransferConfigBuilder", "Built batch configuration with " << m_configs.size() << " transfers");
    return m_configs;
}

BatchTransferConfigBuilder& BatchTransferConfigBuilder::clear()
{
    m_configs.clear();
    FT_LOG_DEBUG("BatchTransferConfigBuilder", "Cleared all configurations");
    return *this;
}

// TransferConfigValidator Implementation

TransferConfigValidator::ValidationResult TransferConfigValidator::validate(const AdvancedTransferConfig& config, ValidationLevel level)
{
    ValidationResult result;
    
    // Basic validation
    if (config.filename.empty()) {
        result.errors.push_back("Filename is required");
    }
    
    if (config.serverB32.empty()) {
        result.errors.push_back("Server B32 address is required");
    }
    
    if (config.strategyName.empty()) {
        result.errors.push_back("Transfer strategy is required");
    }
    
    if (level >= ValidationLevel::STANDARD) {
        // Standard validation
        if (config.connectionTimeoutMs < 1000) {
            result.warnings.push_back("Connection timeout is very short (< 1s)");
        }
        
        if (config.maxRetries > 10) {
            result.warnings.push_back("High retry count may cause long delays");
        }
        
        if (config.chunkSize < 1024) {
            result.warnings.push_back("Small chunk size may reduce performance");
        }
        
        if (config.chunkSize > 64 * 1024) {
            result.warnings.push_back("Large chunk size may cause timeout issues");
        }
        
        if (!config.messageProvider) {
            result.warnings.push_back("No message provider set - will use default");
        }
    }
    
    if (level >= ValidationLevel::STRICT) {
        // Strict validation
        if (config.progressUpdateIntervalMs < 100) {
            result.suggestions.push_back("Consider using less frequent progress updates to reduce overhead");
        }
        
        if (config.maxConcurrentTransfers > 8) {
            result.suggestions.push_back("High concurrency may overwhelm the network");
        }
        
        if (config.verifyIntegrity && config.expectedHash.empty()) {
            result.suggestions.push_back("Enable hash verification for better integrity checking");
        }
    }
    
    result.isValid = result.errors.empty();
    return result;
}

std::string TransferConfigValidator::ValidationResult::getSummary() const
{
    std::ostringstream summary;
    
    summary << "Validation " << (isValid ? "PASSED" : "FAILED");
    
    if (!errors.empty()) {
        summary << " - " << errors.size() << " errors";
    }
    
    if (!warnings.empty()) {
        summary << " - " << warnings.size() << " warnings";
    }
    
    if (!suggestions.empty()) {
        summary << " - " << suggestions.size() << " suggestions";
    }
    
    return summary.str();
}

AdvancedTransferConfig TransferConfigValidator::getRecommendedConfig(const std::string& useCase)
{
    if (useCase == "fast-download") {
        return TransferConfigBuilder::fromDefaults()
            .useHighPerformancePreset()
            .usingStrategy("binary-stream")
            .buildUnsafe();
    } else if (useCase == "secure-download") {
        return TransferConfigBuilder::fromDefaults()
            .useSecurePreset()
            .usingStrategy("chunked")
            .buildUnsafe();
    } else if (useCase == "debug") {
        return TransferConfigBuilder::fromDefaults()
            .useDebugPreset()
            .buildUnsafe();
    }
    
    return TransferConfigBuilder::fromDefaults().buildUnsafe();
}

void TransferConfigValidator::optimizeConfig(AdvancedTransferConfig& config, size_t estimatedFileSize)
{
    // Optimize timeouts based on file size
    if (estimatedFileSize > 100 * 1024 * 1024) { // > 100MB
        config.transferTimeoutMs = std::max(config.transferTimeoutMs, 300000); // 5 minutes
        config.chunkSize = std::max(config.chunkSize, static_cast<size_t>(32768)); // 32KB
    } else if (estimatedFileSize < 1024 * 1024) { // < 1MB
        config.chunkSize = std::min(config.chunkSize, static_cast<size_t>(4096)); // 4KB
    }
    
    // Adjust progress reporting frequency
    if (estimatedFileSize < 1024 * 1024) { // < 1MB
        config.progressUpdateIntervalMs = std::max(config.progressUpdateIntervalMs, 1000);
    }
    
    FT_LOG_DEBUG("TransferConfigValidator", "Optimized config for estimated file size: " << estimatedFileSize << " bytes");
}

} // namespace i2p::filetransfer