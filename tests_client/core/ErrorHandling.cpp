#include "ErrorHandling.h"
#include "FileTransferLogging.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <random>
#include <iomanip>

namespace i2p::filetransfer
{

// ErrorInfo Implementation

std::string ErrorInfo::getFormattedMessage() const
{
    std::ostringstream oss;
    
    // Add timestamp - simplified approach
    auto timeT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    oss << "[" << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S") << "] ";
    
    // Add severity
    switch (severity) {
        case ErrorSeverity::TRACE:    oss << "TRACE"; break;
        case ErrorSeverity::DEBUG:    oss << "DEBUG"; break;
        case ErrorSeverity::INFO:     oss << "INFO"; break;
        case ErrorSeverity::WARNING:  oss << "WARNING"; break;
        case ErrorSeverity::ERROR:    oss << "ERROR"; break;
        case ErrorSeverity::CRITICAL: oss << "CRITICAL"; break;
        case ErrorSeverity::FATAL:    oss << "FATAL"; break;
    }
    
    // Add category
    oss << " [";
    switch (category) {
        case ErrorCategory::NETWORK:        oss << "NETWORK"; break;
        case ErrorCategory::PROTOCOL:       oss << "PROTOCOL"; break;
        case ErrorCategory::FILE_SYSTEM:    oss << "FILE_SYSTEM"; break;
        case ErrorCategory::VALIDATION:     oss << "VALIDATION"; break;
        case ErrorCategory::AUTHENTICATION: oss << "AUTHENTICATION"; break;
        case ErrorCategory::RESOURCE:       oss << "RESOURCE"; break;
        case ErrorCategory::TIMEOUT:        oss << "TIMEOUT"; break;
        case ErrorCategory::CANCELLED:      oss << "CANCELLED"; break;
        case ErrorCategory::INTERNAL:       oss << "INTERNAL"; break;
        case ErrorCategory::UNKNOWN:        oss << "UNKNOWN"; break;
    }
    oss << "] ";
    
    // Add code and message
    oss << code << ": " << message;
    
    // Add context if available
    if (!context.empty()) {
        oss << " (Context: " << context << ")";
    }
    
    // Add metadata if available
    if (!metadata.empty()) {
        oss << " {";
        bool first = true;
        for (const auto& [key, value] : metadata) {
            if (!first) oss << ", ";
            oss << key << "=" << value;
            first = false;
        }
        oss << "}";
    }
    
    return oss.str();
}

bool ErrorInfo::isRetryable() const
{
    switch (category) {
        case ErrorCategory::NETWORK:
        case ErrorCategory::TIMEOUT:
        case ErrorCategory::RESOURCE:
            return true;
            
        case ErrorCategory::PROTOCOL:
            // Some protocol errors might be retryable (temporary server issues)
            return code == ErrorCodes::INVALID_RESPONSE;
            
        case ErrorCategory::FILE_SYSTEM:
            // Only certain filesystem errors are retryable
            return code == ErrorCodes::DISK_FULL || code == ErrorCodes::IO_ERROR;
            
        case ErrorCategory::VALIDATION:
        case ErrorCategory::AUTHENTICATION:
        case ErrorCategory::CANCELLED:
        case ErrorCategory::INTERNAL:
        case ErrorCategory::UNKNOWN:
        default:
            return false;
    }
}

int ErrorInfo::getRetryDelayMs() const
{
    switch (category) {
        case ErrorCategory::NETWORK:
            return 2000; // 2 seconds for network issues
        case ErrorCategory::TIMEOUT:
            return 5000; // 5 seconds for timeouts
        case ErrorCategory::RESOURCE:
            return 10000; // 10 seconds for resource issues
        case ErrorCategory::PROTOCOL:
            return 3000; // 3 seconds for protocol issues
        case ErrorCategory::FILE_SYSTEM:
            return 1000; // 1 second for filesystem issues
        default:
            return 1000; // Default 1 second
    }
}

// ErrorFactory Implementation

namespace ErrorFactory
{

ErrorInfo networkError(const std::string& message, const std::string& context)
{
    ErrorInfo error(ErrorCodes::CONNECTION_FAILED, message, ErrorCategory::NETWORK, ErrorSeverity::ERROR);
    if (!context.empty()) {
        error.withContext(context);
    }
    return error;
}

ErrorInfo protocolError(const std::string& message, const std::string& context)
{
    ErrorInfo error(ErrorCodes::INVALID_RESPONSE, message, ErrorCategory::PROTOCOL, ErrorSeverity::ERROR);
    if (!context.empty()) {
        error.withContext(context);
    }
    return error;
}

ErrorInfo fileSystemError(const std::string& message, const std::string& context)
{
    ErrorInfo error(ErrorCodes::IO_ERROR, message, ErrorCategory::FILE_SYSTEM, ErrorSeverity::ERROR);
    if (!context.empty()) {
        error.withContext(context);
    }
    return error;
}

ErrorInfo validationError(const std::string& message, const std::string& context)
{
    ErrorInfo error(ErrorCodes::INVALID_CONFIG, message, ErrorCategory::VALIDATION, ErrorSeverity::ERROR);
    if (!context.empty()) {
        error.withContext(context);
    }
    return error;
}

ErrorInfo timeoutError(const std::string& operation, int timeoutMs)
{
    std::string message = "Operation '" + operation + "' timed out after " + std::to_string(timeoutMs) + "ms";
    ErrorInfo error(ErrorCodes::TRANSFER_TIMEOUT, message, ErrorCategory::TIMEOUT, ErrorSeverity::ERROR);
    error.withMetadata("operation", operation)
         .withMetadata("timeout_ms", std::to_string(timeoutMs));
    return error;
}

ErrorInfo configurationError(const std::string& parameter, const std::string& issue)
{
    std::string message = "Configuration error in parameter '" + parameter + "': " + issue;
    ErrorInfo error(ErrorCodes::INVALID_CONFIG, message, ErrorCategory::VALIDATION, ErrorSeverity::ERROR);
    error.withMetadata("parameter", parameter)
         .withMetadata("issue", issue);
    return error;
}

ErrorInfo transferError(const std::string& transferId, const std::string& message)
{
    ErrorInfo error(ErrorCodes::TRANSFER_CANCELLED, message, ErrorCategory::INTERNAL, ErrorSeverity::ERROR);
    error.withMetadata("transfer_id", transferId);
    return error;
}

} // namespace ErrorFactory

// ExponentialBackoffRetryPolicy Implementation

RetryPolicy::RetryDecision ExponentialBackoffRetryPolicy::shouldRetry(const ErrorInfo& error, int attemptNumber) const
{
    RetryDecision decision;
    
    // Check if error is retryable
    if (!error.isRetryable()) {
        decision.shouldRetry = false;
        decision.delayMs = 0;
        decision.reason = "Error category " + std::to_string(static_cast<int>(error.category)) + " is not retryable";
        return decision;
    }
    
    // Check if we've exceeded max retries
    if (attemptNumber >= m_maxRetries) {
        decision.shouldRetry = false;
        decision.delayMs = 0;
        decision.reason = "Maximum retry attempts (" + std::to_string(m_maxRetries) + ") exceeded";
        return decision;
    }
    
    // Calculate exponential backoff delay
    int delay = static_cast<int>(m_baseDelayMs * std::pow(m_multiplier, attemptNumber - 1));
    delay = std::min(delay, m_maxDelayMs);
    
    // Add some jitter (±10%) to avoid thundering herd
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> jitter(0.9, 1.1);
    delay = static_cast<int>(delay * jitter(gen));
    
    decision.shouldRetry = true;
    decision.delayMs = delay;
    decision.reason = "Retry attempt " + std::to_string(attemptNumber) + "/" + std::to_string(m_maxRetries);
    
    FT_LOG_DEBUG("ExponentialBackoffRetryPolicy", "Retry decision: " << decision.reason 
                 << ", delay: " << delay << "ms");
    
    return decision;
}

} // namespace i2p::filetransfer