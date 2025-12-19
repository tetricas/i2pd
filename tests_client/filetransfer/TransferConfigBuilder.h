#pragma once

#include "ITransferObserver.h"
#include "../transport/IStreamingProvider.h"
#include "../core/TransferConfig.h"
#include <string>
#include <memory>
#include <vector>
#include <chrono>
#include <map>

namespace i2p::filetransfer
{

/**
 * @brief Advanced transfer configuration with all options
 */
struct AdvancedTransferConfig
{
    // Basic transfer settings
    std::string filename;
    std::string serverB32;
    std::string strategyName{"chunked"};
    std::string outputPath;
    
    // Network settings
    int connectionTimeoutMs{20000};
    int transferTimeoutMs{60000};
    int maxRetries{3};
    int retryDelayMs{1000};
    
    // Performance settings
    size_t maxConcurrentTransfers{1};
    size_t chunkSize{8192};
    bool enableCompression{false};
    
    // Security settings
    bool verifyIntegrity{true};
    bool useEncryption{true};
    std::string expectedHash;
    
    // Progress and logging
    bool enableProgressReporting{true};
    int progressUpdateIntervalMs{500};
    std::vector<std::shared_ptr<ITransferObserver>> observers;
    
    // Provider settings
    std::shared_ptr<i2p::transport::IStreamingMessageProvider> messageProvider;
    std::string protocolName{"simple"};
    
    // Advanced options
    std::map<std::string, std::string> customOptions;
    bool resumeTransfer{false};
    std::string resumeCheckpointPath;
    
    // Validation
    [[nodiscard]] bool isValid() const {
        return !filename.empty() && !serverB32.empty() && !strategyName.empty();
    }
    
    // Helper to get total estimated time
    [[nodiscard]] int getTotalEstimatedTimeMs() const {
        return connectionTimeoutMs + transferTimeoutMs + (maxRetries * retryDelayMs);
    }
};

/**
 * @brief Builder for creating complex transfer configurations
 * Implements Builder Pattern with method chaining
 */
class TransferConfigBuilder
{
private:
    AdvancedTransferConfig m_config;
    std::string m_validationErrors;
    
    void validateAndUpdateErrors();
    
public:
    TransferConfigBuilder();
    
    // Basic configuration
    TransferConfigBuilder& forFile(const std::string& filename);
    TransferConfigBuilder& fromServer(const std::string& serverB32);
    TransferConfigBuilder& usingStrategy(const std::string& strategyName);
    TransferConfigBuilder& saveToPath(const std::string& outputPath);
    
    // Network configuration
    TransferConfigBuilder& withConnectionTimeout(int timeoutMs);
    TransferConfigBuilder& withTransferTimeout(int timeoutMs);
    TransferConfigBuilder& withMaxRetries(int retries);
    TransferConfigBuilder& withRetryDelay(int delayMs);
    
    // Performance configuration
    TransferConfigBuilder& withMaxConcurrentTransfers(size_t count);
    TransferConfigBuilder& withChunkSize(size_t size);
    TransferConfigBuilder& enableCompression(bool enable = true);
    
    // Security configuration
    TransferConfigBuilder& verifyIntegrity(bool verify = true);
    TransferConfigBuilder& withExpectedHash(const std::string& hash);
    TransferConfigBuilder& useEncryption(bool encrypt = true);
    
    // Progress and monitoring
    TransferConfigBuilder& enableProgressReporting(bool enable = true);
    TransferConfigBuilder& withProgressInterval(int intervalMs);
    TransferConfigBuilder& addObserver(std::shared_ptr<ITransferObserver> observer);
    
    // Protocol configuration
    TransferConfigBuilder& withMessageProvider(std::shared_ptr<i2p::transport::IStreamingMessageProvider> provider);
    TransferConfigBuilder& usingProtocol(const std::string& protocolName);
    
    // Advanced options
    TransferConfigBuilder& withCustomOption(const std::string& key, const std::string& value);
    TransferConfigBuilder& enableResume(const std::string& checkpointPath);
    
    // Preset configurations
    TransferConfigBuilder& useHighPerformancePreset();
    TransferConfigBuilder& useSecurePreset();

    
    // Validation and building
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::string getValidationErrors() const { return m_validationErrors; }
    
    /**
     * @brief Build the final configuration
     * @throws std::runtime_error if configuration is invalid
     */
    AdvancedTransferConfig build();
    
    /**
     * @brief Build configuration without throwing on invalid state
     */
    AdvancedTransferConfig buildUnsafe() const { return m_config; }
    
    /**
     * @brief Reset builder to default state
     */
    TransferConfigBuilder& reset();
    
    /**
     * @brief Create a copy of current builder
     */
    [[nodiscard]] TransferConfigBuilder copy() const;
    
    // Static factory methods
    static TransferConfigBuilder forDownload(const std::string& filename, const std::string& serverB32);
    static TransferConfigBuilder forUpload(const std::string& filename, const std::string& serverB32);
    static TransferConfigBuilder fromDefaults();
};

/**
 * @brief Specialized builder for batch transfer configurations
 */
class BatchTransferConfigBuilder
{
private:
    std::vector<AdvancedTransferConfig> m_configs;
    TransferConfigBuilder m_templateBuilder;
    
public:
    BatchTransferConfigBuilder();
    
    /**
     * @brief Set template configuration for all transfers
     */
    BatchTransferConfigBuilder& withTemplate(const TransferConfigBuilder& templateBuilder);
    
    /**
     * @brief Add single file transfer
     */
    BatchTransferConfigBuilder& addFile(const std::string& filename, const std::string& serverB32 = "");
    
    /**
     * @brief Add multiple files from list
     */
    BatchTransferConfigBuilder& addFiles(const std::vector<std::string>& filenames, const std::string& serverB32 = "");
    
    /**
     * @brief Add files with custom configurations
     */
    BatchTransferConfigBuilder& addFileWithConfig(const std::string& filename, 
                                                 const std::function<void(TransferConfigBuilder&)>& configFunc);
    
    /**
     * @brief Build all configurations
     */
    std::vector<AdvancedTransferConfig> build();
    
    /**
     * @brief Get count of configured transfers
     */
    [[nodiscard]] size_t getCount() const { return m_configs.size(); }
    
    /**
     * @brief Clear all configurations
     */
    BatchTransferConfigBuilder& clear();
};

/**
 * @brief Configuration validator for ensuring transfer settings are optimal
 */
class TransferConfigValidator
{
public:
    enum class ValidationLevel {
        BASIC,      // Only check required fields
        STANDARD,   // Check common issues
        STRICT      // Comprehensive validation
    };
    
    struct ValidationResult {
        bool isValid{false};
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        std::vector<std::string> suggestions;
        
        [[nodiscard]] bool hasErrors() const { return !errors.empty(); }
        [[nodiscard]] bool hasWarnings() const { return !warnings.empty(); }
        [[nodiscard]] std::string getSummary() const;
    };
    
    /**
     * @brief Validate transfer configuration
     */
    static ValidationResult validate(const AdvancedTransferConfig& config, 
                                   ValidationLevel level = ValidationLevel::STANDARD);
    
    /**
     * @brief Get recommended settings for specific use cases
     */
    static AdvancedTransferConfig getRecommendedConfig(const std::string& useCase);
    
    /**
     * @brief Auto-optimize configuration based on file size and network conditions
     */
    static void optimizeConfig(AdvancedTransferConfig& config, size_t estimatedFileSize = 0);
};

} // namespace i2p::filetransfer