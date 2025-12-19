#pragma once

#include "TransferResult.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <functional>

namespace i2p::filetransfer
{

/**
 * @brief Error category enumeration for systematic error classification
 */
enum class ErrorCategory
{
    NETWORK,          // Connection, timeout, network-related errors
    PROTOCOL,         // Protocol parsing, format errors
    FILE_SYSTEM,      // File I/O, permission errors
    VALIDATION,       // Input validation, configuration errors
    AUTHENTICATION,   // Security, authentication errors  
    RESOURCE,         // Memory, disk space, resource exhaustion
    TIMEOUT,          // Operation timeout errors
    CANCELLED,        // User or system cancelled operations
    INTERNAL,         // Internal logic errors, bugs
    UNKNOWN           // Unclassified errors
};

/**
 * @brief Error severity levels for prioritizing error handling
 */
enum class ErrorSeverity
{
    TRACE,            // Detailed debugging information
    DEBUG,            // Debug information
    INFO,             // Informational messages
    WARNING,          // Warning conditions that don't prevent operation
    ERROR,            // Error conditions that prevent operation
    CRITICAL,         // Critical errors that may cause system instability
    FATAL             // Fatal errors requiring immediate shutdown
};

/**
 * @brief Structured error information with context and metadata
 */
struct ErrorInfo
{
    std::string code;                    // Machine-readable error code
    std::string message;                 // Human-readable error message
    ErrorCategory category;              // Error classification
    ErrorSeverity severity;              // Error severity level
    std::chrono::steady_clock::time_point timestamp;
    std::string context;                 // Additional context information
    std::map<std::string, std::string> metadata; // Additional metadata
    
    ErrorInfo(std::string errorCode, std::string errorMessage, 
              ErrorCategory cat = ErrorCategory::UNKNOWN,
              ErrorSeverity sev = ErrorSeverity::ERROR)
        : code(std::move(errorCode))
        , message(std::move(errorMessage))
        , category(cat)
        , severity(sev)
        , timestamp(std::chrono::steady_clock::now())
    {}
    
    /**
     * @brief Add contextual information to the error
     */
    ErrorInfo& withContext(const std::string& ctx) {
        context = ctx;
        return *this;
    }
    
    /**
     * @brief Add metadata key-value pair
     */
    ErrorInfo& withMetadata(const std::string& key, const std::string& value) {
        metadata[key] = value;
        return *this;
    }
    
    /**
     * @brief Get formatted error string for logging
     */
    std::string getFormattedMessage() const;
    
    /**
     * @brief Check if error should be retried based on category
     */
    bool isRetryable() const;
    
    /**
     * @brief Get suggested retry delay in milliseconds
     */
    int getRetryDelayMs() const;
};

/**
 * @brief Enhanced Result type with structured error information
 */
template<typename T>
class EnhancedResult
{
private:
    T m_value{};
    bool m_success{false};
    std::vector<ErrorInfo> m_errors;
    std::vector<ErrorInfo> m_warnings;
    
public:
    EnhancedResult() = default;
    
    /**
     * @brief Create successful result
     */
    static EnhancedResult Success(T&& value) {
        EnhancedResult result;
        result.m_value = std::move(value);
        result.m_success = true;
        return result;
    }
    
    static EnhancedResult Success(const T& value) {
        EnhancedResult result;
        result.m_value = value;
        result.m_success = true;
        return result;
    }
    
    /**
     * @brief Create error result with structured error information
     */
    static EnhancedResult Error(ErrorInfo error) {
        EnhancedResult result;
        result.m_success = false;
        result.m_errors.push_back(std::move(error));
        return result;
    }
    
    /**
     * @brief Create error result with multiple errors
     */
    static EnhancedResult Error(std::vector<ErrorInfo> errors) {
        EnhancedResult result;
        result.m_success = false;
        result.m_errors = std::move(errors);
        return result;
    }
    
    /**
     * @brief Create result with warnings but successful outcome
     */
    static EnhancedResult SuccessWithWarnings(T&& value, std::vector<ErrorInfo> warnings) {
        EnhancedResult result;
        result.m_value = std::move(value);
        result.m_success = true;
        result.m_warnings = std::move(warnings);
        return result;
    }
    
    // Status checking
    explicit operator bool() const { return m_success; }
    bool isSuccess() const { return m_success; }
    bool isError() const { return !m_success; }
    bool hasWarnings() const { return !m_warnings.empty(); }
    
    // Value access
    const T& getValue() const { return m_value; }
    T& getValue() { return m_value; }
    T getValueOr(const T& defaultValue) const { return m_success ? m_value : defaultValue; }
    
    // Error access
    const std::vector<ErrorInfo>& getErrors() const { return m_errors; }
    const std::vector<ErrorInfo>& getWarnings() const { return m_warnings; }
    
    /**
     * @brief Get the primary error (first error in list)
     */
    const ErrorInfo& getPrimaryError() const {
        static const ErrorInfo defaultError("UNKNOWN", "No error information available");
        return m_errors.empty() ? defaultError : m_errors[0];
    }
    
    /**
     * @brief Get all error messages concatenated
     */
    std::string getAllErrorMessages() const {
        std::string result;
        for (const auto& error : m_errors) {
            if (!result.empty()) result += "; ";
            result += error.message;
        }
        return result;
    }
    
    /**
     * @brief Add warning to successful result
     */
    void addWarning(ErrorInfo warning) {
        m_warnings.push_back(std::move(warning));
    }
    
    /**
     * @brief Transform result to different type
     */
    template<typename U>
    EnhancedResult<U> transform(std::function<U(const T&)> transformer) const {
        if (!m_success) {
            return EnhancedResult<U>::Error(m_errors);
        }
        
        try {
            U newValue = transformer(m_value);
            if (hasWarnings()) {
                return EnhancedResult<U>::SuccessWithWarnings(std::move(newValue), m_warnings);
            }
            return EnhancedResult<U>::Success(std::move(newValue));
        } catch (const std::exception& e) {
            ErrorInfo transformError("TRANSFORM_ERROR", "Failed to transform result: " + std::string(e.what()),
                                   ErrorCategory::INTERNAL, ErrorSeverity::ERROR);
            return EnhancedResult<U>::Error(std::move(transformError));
        }
    }
    
    /**
     * @brief Chain operations that might fail
     */
    template<typename U>
    EnhancedResult<U> flatMap(std::function<EnhancedResult<U>(const T&)> mapper) const {
        if (!m_success) {
            return EnhancedResult<U>::Error(m_errors);
        }
        
        return mapper(m_value);
    }
    
    /**
     * @brief Convert to legacy TransferResult for backwards compatibility
     */
    TransferResult<T> toLegacy() const {
        if (m_success) {
            return TransferResult<T>::Success(m_value);
        } else {
            return TransferResult<T>::Error(getAllErrorMessages());
        }
    }
};

/**
 * @brief Void specialization for operations that don't return values
 */
template<>
class EnhancedResult<void>
{
private:
    bool m_success{false};
    std::vector<ErrorInfo> m_errors;
    std::vector<ErrorInfo> m_warnings;
    
public:
    EnhancedResult() = default;
    
    static EnhancedResult Success() {
        EnhancedResult result;
        result.m_success = true;
        return result;
    }
    
    static EnhancedResult Error(ErrorInfo error) {
        EnhancedResult result;
        result.m_success = false;
        result.m_errors.push_back(std::move(error));
        return result;
    }
    
    static EnhancedResult Error(std::vector<ErrorInfo> errors) {
        EnhancedResult result;
        result.m_success = false;
        result.m_errors = std::move(errors);
        return result;
    }
    
    static EnhancedResult SuccessWithWarnings(std::vector<ErrorInfo> warnings) {
        EnhancedResult result;
        result.m_success = true;
        result.m_warnings = std::move(warnings);
        return result;
    }
    
    explicit operator bool() const { return m_success; }
    bool isSuccess() const { return m_success; }
    bool isError() const { return !m_success; }
    bool hasWarnings() const { return !m_warnings.empty(); }
    
    const std::vector<ErrorInfo>& getErrors() const { return m_errors; }
    const std::vector<ErrorInfo>& getWarnings() const { return m_warnings; }
    
    const ErrorInfo& getPrimaryError() const {
        static const ErrorInfo defaultError("UNKNOWN", "No error information available");
        return m_errors.empty() ? defaultError : m_errors[0];
    }
    
    std::string getAllErrorMessages() const {
        std::string result;
        for (const auto& error : m_errors) {
            if (!result.empty()) result += "; ";
            result += error.message;
        }
        return result;
    }
    
    void addWarning(ErrorInfo warning) {
        m_warnings.push_back(std::move(warning));
    }
    
    TransferResult<void> toLegacy() const {
        if (m_success) {
            return TransferResult<void>::Success();
        } else {
            return TransferResult<void>::Error(getAllErrorMessages());
        }
    }
};

// Type aliases
using VoidEnhancedResult = EnhancedResult<void>;

/**
 * @brief Error code constants for common file transfer errors
 */
namespace ErrorCodes
{
    // Network errors
    constexpr const char* CONNECTION_FAILED = "NETWORK_CONNECTION_FAILED";
    constexpr const char* CONNECTION_TIMEOUT = "NETWORK_CONNECTION_TIMEOUT";
    constexpr const char* NETWORK_UNREACHABLE = "NETWORK_UNREACHABLE";
    
    // Protocol errors
    constexpr const char* INVALID_RESPONSE = "PROTOCOL_INVALID_RESPONSE";
    constexpr const char* PROTOCOL_MISMATCH = "PROTOCOL_MISMATCH";
    constexpr const char* MESSAGE_TOO_LARGE = "PROTOCOL_MESSAGE_TOO_LARGE";
    
    // File system errors
    constexpr const char* FILE_NOT_FOUND = "FS_FILE_NOT_FOUND";
    constexpr const char* PERMISSION_DENIED = "FS_PERMISSION_DENIED";
    constexpr const char* DISK_FULL = "FS_DISK_FULL";
    constexpr const char* IO_ERROR = "FS_IO_ERROR";
    
    // Validation errors
    constexpr const char* INVALID_CONFIG = "VALIDATION_INVALID_CONFIG";
    constexpr const char* INVALID_FILENAME = "VALIDATION_INVALID_FILENAME";
    constexpr const char* INVALID_SERVER = "VALIDATION_INVALID_SERVER";
    
    // Transfer errors
    constexpr const char* TRANSFER_CANCELLED = "TRANSFER_CANCELLED";
    constexpr const char* TRANSFER_TIMEOUT = "TRANSFER_TIMEOUT";
    constexpr const char* CHUNK_VERIFICATION_FAILED = "TRANSFER_CHUNK_VERIFICATION_FAILED";
    constexpr const char* INTEGRITY_CHECK_FAILED = "TRANSFER_INTEGRITY_CHECK_FAILED";
    
    // Resource errors
    constexpr const char* OUT_OF_MEMORY = "RESOURCE_OUT_OF_MEMORY";
    constexpr const char* TOO_MANY_CONNECTIONS = "RESOURCE_TOO_MANY_CONNECTIONS";
    
    // Internal errors
    constexpr const char* STRATEGY_NOT_FOUND = "INTERNAL_STRATEGY_NOT_FOUND";
    constexpr const char* PROVIDER_NOT_READY = "INTERNAL_PROVIDER_NOT_READY";
    constexpr const char* UNEXPECTED_ERROR = "INTERNAL_UNEXPECTED_ERROR";
}

/**
 * @brief Factory functions for creating common errors
 */
namespace ErrorFactory
{
    ErrorInfo networkError(const std::string& message, const std::string& context = "");
    ErrorInfo protocolError(const std::string& message, const std::string& context = "");
    ErrorInfo fileSystemError(const std::string& message, const std::string& context = "");
    ErrorInfo validationError(const std::string& message, const std::string& context = "");
    ErrorInfo timeoutError(const std::string& operation, int timeoutMs);
    ErrorInfo configurationError(const std::string& parameter, const std::string& issue);
    ErrorInfo transferError(const std::string& transferId, const std::string& message);
}

/**
 * @brief Retry policy for handling retryable errors
 */
class RetryPolicy
{
public:
    struct RetryDecision {
        bool shouldRetry;
        int delayMs;
        std::string reason;
    };
    
    virtual ~RetryPolicy() = default;
    virtual RetryDecision shouldRetry(const ErrorInfo& error, int attemptNumber) const = 0;
};

/**
 * @brief Exponential backoff retry policy
 */
class ExponentialBackoffRetryPolicy : public RetryPolicy
{
private:
    int m_maxRetries;
    int m_baseDelayMs;
    double m_multiplier;
    int m_maxDelayMs;
    
public:
    ExponentialBackoffRetryPolicy(int maxRetries = 3, int baseDelayMs = 1000, 
                                 double multiplier = 2.0, int maxDelayMs = 30000)
        : m_maxRetries(maxRetries), m_baseDelayMs(baseDelayMs), 
          m_multiplier(multiplier), m_maxDelayMs(maxDelayMs) {}
    
    RetryDecision shouldRetry(const ErrorInfo& error, int attemptNumber) const override;
};

} // namespace i2p::filetransfer