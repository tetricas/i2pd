#pragma once

#include "Log.h"
#include <sstream>

/**
 * @file FileTransferLogging.h  
 * @brief Standardized logging macros for file transfer components using existing i2pd logging
 */

namespace i2p {
namespace filetransfer {

// Standardized logging macros using existing i2pd LogPrint
#define FT_LOG_DEBUG(component, msg) \
    do { \
        std::ostringstream oss; \
        oss << "FileTransfer::" << component << ": " << msg; \
        LogPrint(eLogDebug, oss.str()); \
    } while(0)

#define FT_LOG_INFO(component, msg) \
    do { \
        std::ostringstream oss; \
        oss << "FileTransfer::" << component << ": " << msg; \
        LogPrint(eLogInfo, oss.str()); \
    } while(0)

#define FT_LOG_WARNING(component, msg) \
    do { \
        std::ostringstream oss; \
        oss << "FileTransfer::" << component << ": " << msg; \
        LogPrint(eLogWarning, oss.str()); \
    } while(0)

#define FT_LOG_WARN(component, msg) \
    do { \
        std::ostringstream oss; \
        oss << "FileTransfer::" << component << ": " << msg; \
        LogPrint(eLogWarning, oss.str()); \
    } while(0)

#define FT_LOG_ERROR(component, msg) \
    do { \
        std::ostringstream oss; \
        oss << "FileTransfer::" << component << ": " << msg; \
        LogPrint(eLogError, oss.str()); \
    } while(0)

// Performance-aware debug logging (only evaluates if debug logging enabled)
#define FT_LOG_DEBUG_IF_ENABLED(component, msg) \
    do { \
        if (i2p::log::Logger().GetLogLevel() <= eLogDebug) { \
            FT_LOG_DEBUG(component, msg); \
        } \
    } while(0)

} // namespace filetransfer
} // namespace i2p