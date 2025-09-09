#pragma once

#include <string>
#include <utility>

namespace i2p {
namespace filetransfer {

/**
 * @brief Simple result wrapper for consistent error handling without heavy dependencies
 * @tparam T The type of value this result contains
 */
template<typename T>
struct TransferResult {
    T value{};
    bool success{false};
    std::string error;
    
    /**
     * @brief Create a successful result
     * @param val The value to wrap
     * @return TransferResult with success=true and the given value
     */
    static TransferResult Success(T&& val) {
        TransferResult result;
        result.value = std::move(val);
        result.success = true;
        result.error.clear();
        return result;
    }
    
    /**
     * @brief Create a successful result (copy version)
     * @param val The value to wrap
     * @return TransferResult with success=true and the given value
     */
    static TransferResult Success(const T& val) {
        TransferResult result;
        result.value = val;
        result.success = true;
        result.error.clear();
        return result;
    }
    
    /**
     * @brief Create an error result
     * @param err Error description
     * @return TransferResult with success=false and the given error
     */
    static TransferResult Error(const std::string& err) {
        TransferResult result;
        result.value = T{};
        result.success = false;
        result.error = err;
        return result;
    }
    
    /**
     * @brief Check if this result represents success
     * @return true if successful, false if error
     */
    explicit operator bool() const { 
        return success; 
    }
    
    /**
     * @brief Check if this result represents success
     * @return true if successful, false if error  
     */
    bool isSuccess() const { 
        return success; 
    }
    
    /**
     * @brief Check if this result represents an error
     * @return true if error, false if successful
     */
    bool isError() const { 
        return !success; 
    }
    
    /**
     * @brief Get the wrapped value (only valid if isSuccess())
     * @return Reference to the wrapped value
     */
    const T& getValue() const { 
        return value; 
    }
    
    /**
     * @brief Get the wrapped value (only valid if isSuccess())
     * @return Reference to the wrapped value
     */
    T& getValue() { 
        return value; 
    }
    
    /**
     * @brief Get the error message (only valid if isError())
     * @return Reference to the error string
     */
    const std::string& getError() const { 
        return error; 
    }
    
    /**
     * @brief Get the value or a default if this is an error
     * @param defaultValue Value to return if this is an error result
     * @return The wrapped value if successful, defaultValue otherwise
     */
    T getValueOr(const T& defaultValue) const {
        return success ? value : defaultValue;
    }
};

/**
 * @brief Specialization for void results (operations that don't return a value)
 */
template<>
struct TransferResult<void> {
    bool success{false};
    std::string error;
    
    /**
     * @brief Create a successful void result
     * @return TransferResult with success=true
     */
    static TransferResult Success() {
        TransferResult result;
        result.success = true;
        result.error.clear();
        return result;
    }
    
    /**
     * @brief Create an error void result
     * @param err Error description
     * @return TransferResult with success=false and the given error
     */
    static TransferResult Error(const std::string& err) {
        TransferResult result;
        result.success = false;
        result.error = err;
        return result;
    }
    
    /**
     * @brief Check if this result represents success
     * @return true if successful, false if error
     */
    explicit operator bool() const { 
        return success; 
    }
    
    /**
     * @brief Check if this result represents success
     * @return true if successful, false if error
     */
    bool isSuccess() const { 
        return success; 
    }
    
    /**
     * @brief Check if this result represents an error
     * @return true if error, false if successful
     */
    bool isError() const { 
        return !success; 
    }
    
    /**
     * @brief Get the error message (only valid if isError())
     * @return Reference to the error string
     */
    const std::string& getError() const { 
        return error; 
    }
};

// Convenience type aliases
using VoidResult = TransferResult<void>;

} // namespace filetransfer  
} // namespace i2p