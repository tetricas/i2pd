#pragma once

#include "ErrorHandling.h"
#include <string>
#include <chrono>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace i2p::filetransfer
{

/**
 * @brief Context for tracking long-running operations with cancellation support
 */
class OperationContext
{
public:
    using CancellationCallback = std::function<void(const std::string& reason)>;
    using ProgressCallback = std::function<void(double progress, const std::string& status)>;
    
private:
    std::string m_operationId;
    std::string m_description;
    std::chrono::steady_clock::time_point m_startTime;
    std::atomic<bool> m_cancelled{false};
    std::string m_cancellationReason;
    
    CancellationCallback m_cancellationCallback;
    ProgressCallback m_progressCallback;
    
    // Progress tracking
    std::atomic<double> m_progress{0.0};
    std::string m_currentStatus;
    mutable std::mutex m_statusMutex;
    
    // Retry state
    std::unique_ptr<RetryPolicy> m_retryPolicy;
    int m_attemptNumber{1};
    std::vector<ErrorInfo> m_previousErrors;
    
public:
    /**
     * @brief Create operation context
     * @param operationId Unique identifier for this operation
     * @param description Human-readable description
     */
    OperationContext(std::string operationId, std::string description);
    
    /**
     * @brief Destructor - automatically logs operation completion
     */
    ~OperationContext();
    
    // Basic properties
    [[nodiscard]] const std::string& getOperationId() const { return m_operationId; }
    [[nodiscard]] const std::string& getDescription() const { return m_description; }
    [[nodiscard]] std::chrono::steady_clock::time_point getStartTime() const { return m_startTime; }
    
    /**
     * @brief Get elapsed time since operation started
     */
    [[nodiscard]] std::chrono::milliseconds getElapsedTime() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_startTime);
    }
    
    // Cancellation support
    void cancel(const std::string& reason = "Operation cancelled");
    [[nodiscard]] bool isCancelled() const { return m_cancelled; }
    [[nodiscard]] const std::string& getCancellationReason() const { return m_cancellationReason; }
    
    void setCancellationCallback(CancellationCallback callback) { m_cancellationCallback = std::move(callback); }
    
    /**
     * @brief Check for cancellation and throw if cancelled
     * @throws OperationCancelledException if operation was cancelled
     */
    void throwIfCancelled() const;
    
    // Progress tracking
    void updateProgress(double progress, const std::string& status = "");
    [[nodiscard]] double getProgress() const { return m_progress; }
    [[nodiscard]] std::string getCurrentStatus() const;
    
    void setProgressCallback(ProgressCallback callback) { m_progressCallback = std::move(callback); }
    
    // Retry support
    void setRetryPolicy(std::unique_ptr<RetryPolicy> policy) { m_retryPolicy = std::move(policy); }
    
    /**
     * @brief Record an error and determine if operation should be retried
     */
    RetryPolicy::RetryDecision recordErrorAndCheckRetry(const ErrorInfo& error);
    
    [[nodiscard]] int getAttemptNumber() const { return m_attemptNumber; }
    [[nodiscard]] const std::vector<ErrorInfo>& getPreviousErrors() const { return m_previousErrors; }
    
    /**
     * @brief Start next retry attempt
     */
    void startRetryAttempt() { ++m_attemptNumber; }
    
    // Utility methods
    
    /**
     * @brief Create a scoped sub-operation
     */
    std::unique_ptr<OperationContext> createSubOperation(const std::string& subId, const std::string& subDescription) const;
    
    /**
     * @brief Log operation milestone
     */
    void logMilestone(const std::string& milestone);
    
    /**
     * @brief Create timeout checker that respects cancellation
     */
    std::function<bool()> createTimeoutChecker(int timeoutMs) const;
    
    // Static factory methods
    static std::unique_ptr<OperationContext> create(const std::string& operationId, const std::string& description);
    static std::unique_ptr<OperationContext> createWithRetry(const std::string& operationId, const std::string& description, 
                                                             std::unique_ptr<RetryPolicy> retryPolicy);
    
    // Disable copy and move (atomic members don't support move)
    OperationContext(const OperationContext&) = delete;
    OperationContext& operator=(const OperationContext&) = delete;
    OperationContext(OperationContext&&) = delete;
    OperationContext& operator=(OperationContext&&) = delete;
};

/**
 * @brief Exception thrown when operation is cancelled
 */
class OperationCancelledException : public std::exception
{
private:
    std::string m_message;
    std::string m_operationId;
    std::string m_reason;
    
public:
    OperationCancelledException(std::string operationId, std::string reason)
        : m_operationId(std::move(operationId)), m_reason(std::move(reason))
    {
        m_message = "Operation '" + m_operationId + "' was cancelled: " + m_reason;
    }
    
    const char* what() const noexcept override { return m_message.c_str(); }
    [[nodiscard]] const std::string& getOperationId() const { return m_operationId; }
    [[nodiscard]] const std::string& getReason() const { return m_reason; }
};

/**
 * @brief RAII wrapper for operations that provides automatic resource cleanup
 */
template<typename T>
class OperationScope
{
private:
    std::unique_ptr<OperationContext> m_context;
    std::function<EnhancedResult<T>()> m_operation;
    std::function<void()> m_cleanup;
    bool m_executed{false};
    
public:
    OperationScope(std::unique_ptr<OperationContext> context, 
                  std::function<EnhancedResult<T>()> operation,
                  std::function<void()> cleanup = nullptr);
    
    ~OperationScope();
    
    /**
     * @brief Execute the operation with automatic retry handling
     */
    EnhancedResult<T> execute();
    
    [[nodiscard]] OperationContext& getContext() { return *m_context; }
    [[nodiscard]] const OperationContext& getContext() const { return *m_context; }
    
    // Disable copy and move
    OperationScope(const OperationScope&) = delete;
    OperationScope& operator=(const OperationScope&) = delete;
    OperationScope(OperationScope&&) = delete;
    OperationScope& operator=(OperationScope&&) = delete;
};

/**
 * @brief Helper functions for creating operation scopes
 */
namespace OperationScopeFactory
{
    template<typename T>
    std::unique_ptr<OperationScope<T>> create(
        const std::string& operationId,
        const std::string& description,
        std::function<EnhancedResult<T>()> operation,
        std::function<void()> cleanup = nullptr)
    {
        auto context = OperationContext::create(operationId, description);
        return std::make_unique<OperationScope<T>>(std::move(context), std::move(operation), std::move(cleanup));
    }
    
    template<typename T>
    std::unique_ptr<OperationScope<T>> createWithRetry(
        const std::string& operationId,
        const std::string& description,
        std::function<EnhancedResult<T>()> operation,
        std::unique_ptr<RetryPolicy> retryPolicy,
        std::function<void()> cleanup = nullptr)
    {
        auto context = OperationContext::createWithRetry(operationId, description, std::move(retryPolicy));
        return std::make_unique<OperationScope<T>>(std::move(context), std::move(operation), std::move(cleanup));
    }
}

} // namespace i2p::filetransfer