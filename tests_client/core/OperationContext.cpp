#include "OperationContext.h"
#include "FileTransferLogging.h"
#include <sstream>
#include <thread>
#include <algorithm>

namespace i2p::filetransfer
{

// OperationContext Implementation

OperationContext::OperationContext(std::string operationId, std::string description)
    : m_operationId(std::move(operationId))
    , m_description(std::move(description))
    , m_startTime(std::chrono::steady_clock::now())
{
    FT_LOG_INFO("OperationContext", "Started operation " << m_operationId << ": " << m_description);
}

OperationContext::~OperationContext()
{
    auto elapsed = getElapsedTime();
    
    if (m_cancelled) {
        FT_LOG_INFO("OperationContext", "Operation " << m_operationId << " was cancelled after " 
                   << elapsed.count() << "ms: " << m_cancellationReason);
    } else {
        FT_LOG_INFO("OperationContext", "Operation " << m_operationId << " completed after " 
                   << elapsed.count() << "ms");
    }
}

void OperationContext::cancel(const std::string& reason)
{
    if (m_cancelled) {
        return; // Already cancelled
    }
    
    m_cancelled = true;
    m_cancellationReason = reason;
    
    FT_LOG_WARNING("OperationContext", "Cancelling operation " << m_operationId << ": " << reason);
    
    if (m_cancellationCallback) {
        try {
            m_cancellationCallback(reason);
        } catch (const std::exception& e) {
            FT_LOG_ERROR("OperationContext", "Exception in cancellation callback: " << e.what());
        }
    }
}

void OperationContext::throwIfCancelled() const
{
    if (m_cancelled) {
        throw OperationCancelledException(m_operationId, m_cancellationReason);
    }
}

void OperationContext::updateProgress(double progress, const std::string& status)
{
    m_progress = std::clamp(progress, 0.0, 1.0);
    
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        if (!status.empty()) {
            m_currentStatus = status;
        }
    }
    
    if (m_progressCallback) {
        try {
            m_progressCallback(m_progress, getCurrentStatus());
        } catch (const std::exception& e) {
            FT_LOG_ERROR("OperationContext", "Exception in progress callback: " << e.what());
        }
    }
    
    FT_LOG_DEBUG("OperationContext", "Operation " << m_operationId << " progress: " 
                << static_cast<int>(progress * 100) << "% - " << getCurrentStatus());
}

std::string OperationContext::getCurrentStatus() const
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_currentStatus;
}

RetryPolicy::RetryDecision OperationContext::recordErrorAndCheckRetry(const ErrorInfo& error)
{
    m_previousErrors.push_back(error);
    
    FT_LOG_WARNING("OperationContext", "Operation " << m_operationId << " failed on attempt " 
                  << m_attemptNumber << ": " << error.message);
    
    if (!m_retryPolicy) {
        return {false, 0, "No retry policy configured"};
    }
    
    return m_retryPolicy->shouldRetry(error, m_attemptNumber);
}

std::unique_ptr<OperationContext> OperationContext::createSubOperation(const std::string& subId, const std::string& subDescription) const
{
    std::string fullSubId = m_operationId + "." + subId;
    std::string fullDescription = m_description + " -> " + subDescription;
    
    auto subContext = std::make_unique<OperationContext>(fullSubId, fullDescription);
    
    // Inherit cancellation state
    if (m_cancelled) {
        subContext->cancel("Parent operation cancelled: " + m_cancellationReason);
    }
    
    return subContext;
}

void OperationContext::logMilestone(const std::string& milestone)
{
    auto elapsed = getElapsedTime();
    FT_LOG_INFO("OperationContext", "Operation " << m_operationId << " milestone (" 
               << elapsed.count() << "ms): " << milestone);
}

std::function<bool()> OperationContext::createTimeoutChecker(int timeoutMs) const
{
    auto timeoutPoint = m_startTime + std::chrono::milliseconds(timeoutMs);
    
    return [this, timeoutPoint]() -> bool {
        if (m_cancelled) {
            return true; // Consider cancelled as timed out
        }
        return std::chrono::steady_clock::now() >= timeoutPoint;
    };
}

std::unique_ptr<OperationContext> OperationContext::create(const std::string& operationId, const std::string& description)
{
    return std::make_unique<OperationContext>(operationId, description);
}

std::unique_ptr<OperationContext> OperationContext::createWithRetry(const std::string& operationId, const std::string& description, 
                                                                   std::unique_ptr<RetryPolicy> retryPolicy)
{
    auto context = std::make_unique<OperationContext>(operationId, description);
    context->setRetryPolicy(std::move(retryPolicy));
    return context;
}

// Template implementations

template<typename T>
OperationScope<T>::OperationScope(std::unique_ptr<OperationContext> context, 
                                 std::function<EnhancedResult<T>()> operation,
                                 std::function<void()> cleanup)
    : m_context(std::move(context))
    , m_operation(std::move(operation))
    , m_cleanup(std::move(cleanup))
{}

template<typename T>
OperationScope<T>::~OperationScope()
{
    if (m_cleanup && m_executed) {
        try {
            m_cleanup();
        } catch (const std::exception& e) {
            std::string error_msg = "Exception during cleanup: ";
            error_msg += e.what();
            FT_LOG_ERROR("OperationScope", error_msg);
        }
    }
}

template<typename T>
EnhancedResult<T> OperationScope<T>::execute()
{
    if (m_executed) {
        return EnhancedResult<T>::Error(
            ErrorFactory::validationError("Operation already executed"));
    }
    
    m_executed = true;
    EnhancedResult<T> result;
    
    while (true) {
        try {
            m_context->throwIfCancelled();
            
            std::string debug_msg = "Executing operation attempt ";
            debug_msg += std::to_string(m_context->getAttemptNumber());
            debug_msg += ": ";
            debug_msg += m_context->getDescription();
            FT_LOG_DEBUG("OperationScope", debug_msg);
            
            result = m_operation();
            
            if (result.isSuccess() || !result.getPrimaryError().isRetryable()) {
                break;
            }
            
            // Check if we should retry
            auto retryDecision = m_context->recordErrorAndCheckRetry(result.getPrimaryError());
            if (!retryDecision.shouldRetry) {
                std::string info_msg = "Not retrying: ";
                info_msg += retryDecision.reason;
                FT_LOG_INFO("OperationScope", info_msg);
                break;
            }
            
            std::string retry_msg = "Retrying in ";
            retry_msg += std::to_string(retryDecision.delayMs);
            retry_msg += "ms: ";
            retry_msg += retryDecision.reason;
            FT_LOG_INFO("OperationScope", retry_msg);
            
            // Wait for retry delay (respecting cancellation)
            auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(retryDecision.delayMs);
            while (std::chrono::steady_clock::now() < endTime) {
                if (m_context->isCancelled()) {
                    return EnhancedResult<T>::Error(
                        ErrorFactory::transferError(m_context->getOperationId(), 
                                                   "Operation cancelled during retry delay"));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            m_context->startRetryAttempt();
            
        } catch (const OperationCancelledException& e) {
            return EnhancedResult<T>::Error(
                ErrorFactory::transferError(e.getOperationId(), "Operation cancelled: " + e.getReason()));
        } catch (const std::exception& e) {
            ErrorInfo error(ErrorCodes::UNEXPECTED_ERROR, 
                          "Unexpected exception: " + std::string(e.what()),
                          ErrorCategory::INTERNAL, ErrorSeverity::ERROR);
            return EnhancedResult<T>::Error(std::move(error));
        }
    }
    
    if (result.isSuccess()) {
        std::string success_msg = "Operation completed successfully: ";
        success_msg += m_context->getDescription();
        FT_LOG_INFO("OperationScope", success_msg);
    } else {
        std::string error_msg = "Operation failed: ";
        error_msg += result.getAllErrorMessages();
        FT_LOG_ERROR("OperationScope", error_msg);
    }
    
    return result;
}

// Explicit template instantiations for common types
template class OperationScope<void>;
template class OperationScope<std::vector<uint8_t>>;
template class OperationScope<std::string>;

} // namespace i2p::filetransfer