#include "BinaryFileProtocol.h"
#include "FileTransferProtocol.h"
#include "Log.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace i2p::filetransfer
{

BinaryFileMetadata BinaryProtocolUtils::createMetadata(const std::string& filename,
                                                      const std::vector<uint8_t>& fileData,
                                                      uint32_t chunkSize)
{
    BinaryFileMetadata metadata{};
    
    metadata.totalSize = fileData.size();
    metadata.chunkSize = chunkSize;
    metadata.chunkCount = (fileData.size() + chunkSize - 1) / chunkSize; // Ceiling division
    metadata.checksumType = 1; // SHA-256
    metadata.checksumLength = 32;
    
    // Calculate SHA-256 checksum
    auto checksumHex = ProtocolUtils::calculateSHA256(fileData);
    auto checksumBinary = hexToBinary(checksumHex);
    
    if (checksumBinary.size() >= 32) {
        std::copy(checksumBinary.begin(), checksumBinary.begin() + 32, metadata.checksum);
    }
    
    // Store filename (truncate if too long)
    size_t nameLen = std::min(filename.size(), size_t(31)); // Leave space for null terminator
    metadata.filenameLength = static_cast<uint8_t>(nameLen);
    std::memcpy(metadata.filename, filename.c_str(), nameLen);
    metadata.filename[nameLen] = '\0';
    
    LogPrint(eLogInfo, "BinaryProtocolUtils: Created metadata - size=", metadata.totalSize, 
             " chunks=", metadata.chunkCount, " chunkSize=", metadata.chunkSize);
    
    return metadata;
}

std::vector<uint8_t> BinaryProtocolUtils::serializeMetadata(const BinaryFileMetadata& metadata)
{
    std::vector<uint8_t> data(sizeof(BinaryFileMetadata));
    uint8_t* ptr = data.data();
    
    // Use little-endian encoding for cross-platform compatibility
    writeLE(ptr, metadata.totalSize);
    writeLE(ptr, metadata.chunkSize);
    writeLE(ptr, metadata.chunkCount);
    *ptr++ = metadata.checksumType;
    *ptr++ = metadata.checksumLength;
    
    // Copy checksum and filename as-is
    std::memcpy(ptr, metadata.checksum, 32);
    ptr += 32;
    *ptr++ = metadata.filenameLength;
    std::memcpy(ptr, metadata.filename, 32);
    ptr += 32;
    std::memset(ptr, 0, 13); // Clear reserved bytes
    
    return data;
}

bool BinaryProtocolUtils::parseMetadata(const uint8_t* data, size_t dataSize, BinaryFileMetadata& metadata)
{
    if (dataSize < sizeof(BinaryFileMetadata)) {
        LogPrint(eLogError, "BinaryProtocolUtils: Invalid metadata size: ", dataSize);
        return false;
    }
    
    const uint8_t* ptr = data;
    
    // Parse little-endian fields
    metadata.totalSize = readLE<uint64_t>(ptr);
    metadata.chunkSize = readLE<uint32_t>(ptr);
    metadata.chunkCount = readLE<uint32_t>(ptr);
    metadata.checksumType = *ptr++;
    metadata.checksumLength = *ptr++;
    
    // Copy checksum and filename as-is
    std::memcpy(metadata.checksum, ptr, 32);
    ptr += 32;
    metadata.filenameLength = *ptr++;
    std::memcpy(metadata.filename, ptr, 32);
    
    // Ensure filename is null-terminated
    metadata.filename[31] = '\0';
    
    // Basic validation
    if (metadata.checksumType != 1 || metadata.checksumLength != 32) {
        LogPrint(eLogError, "BinaryProtocolUtils: Unsupported checksum type/length");
        return false;
    }
    
    if (metadata.totalSize == 0 || metadata.chunkCount == 0) {
        LogPrint(eLogError, "BinaryProtocolUtils: Invalid file size or chunk count");
        return false;
    }
    
    LogPrint(eLogInfo, "BinaryProtocolUtils: Parsed metadata - size=", metadata.totalSize,
             " chunks=", metadata.chunkCount, " filename=", metadata.filename);
    
    return true;
}

std::vector<uint8_t> BinaryProtocolUtils::createFileRequest(const std::string& filename)
{
    if (filename.empty() || filename.size() > 255) {
        LogPrint(eLogError, "BinaryProtocolUtils: Invalid filename length: ", filename.size());
        return {};
    }
    
    std::vector<uint8_t> request(sizeof(BinaryFileRequest) + filename.size());
    
    BinaryFileRequest* req = reinterpret_cast<BinaryFileRequest*>(request.data());
    req->messageType = 0x01; // FILE_REQUEST
    req->filenameLength = static_cast<uint8_t>(filename.size());
    
    std::memcpy(request.data() + sizeof(BinaryFileRequest), filename.data(), filename.size());
    
    LogPrint(eLogInfo, "BinaryProtocolUtils: Created file request for: ", filename);
    return request;
}

std::string BinaryProtocolUtils::parseFileRequest(const uint8_t* data, size_t dataSize)
{
    if (dataSize < sizeof(BinaryFileRequest)) {
        LogPrint(eLogError, "BinaryProtocolUtils: Invalid file request size");
        return {};
    }
    
    const BinaryFileRequest* req = reinterpret_cast<const BinaryFileRequest*>(data);
    
    if (req->messageType != 0x01) {
        LogPrint(eLogError, "BinaryProtocolUtils: Invalid message type: ", static_cast<int>(req->messageType));
        return {};
    }
    
    if (dataSize < sizeof(BinaryFileRequest) + req->filenameLength) {
        LogPrint(eLogError, "BinaryProtocolUtils: Incomplete filename data");
        return {};
    }
    
    std::string filename(reinterpret_cast<const char*>(data + sizeof(BinaryFileRequest)), req->filenameLength);
    LogPrint(eLogInfo, "BinaryProtocolUtils: Parsed filename: ", filename);
    
    return filename;
}

std::array<uint8_t, 8> BinaryProtocolUtils::createChunkHeader(uint32_t chunkIndex, uint32_t chunkSize)
{
    std::array<uint8_t, 8> header;
    uint8_t* ptr = header.data();
    
    writeLE(ptr, chunkIndex);
    writeLE(ptr, chunkSize);
    
    return header;
}

bool BinaryProtocolUtils::parseChunkHeader(const uint8_t* data, BinaryChunkHeader& header)
{
    if (!data) return false;
    
    const uint8_t* ptr = data;
    header.chunkIndex = readLE<uint32_t>(ptr);
    header.chunkSize = readLE<uint32_t>(ptr);
    
    return true;
}

std::vector<uint8_t> BinaryProtocolUtils::hexToBinary(const std::string& hex)
{
    std::vector<uint8_t> binary;
    binary.reserve(hex.length() / 2);
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (i + 1 < hex.length()) {
            std::string byteString = hex.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byteString, nullptr, 16));
            binary.push_back(byte);
        }
    }
    
    return binary;
}

std::string BinaryProtocolUtils::binaryToHex(const uint8_t* data, size_t size)
{
    std::stringstream ss;
    ss << std::hex;
    
    for (size_t i = 0; i < size; ++i) {
        ss << std::setw(2) << std::setfill('0') << static_cast<unsigned>(data[i]);
    }
    
    return ss.str();
}

std::array<uint8_t, 32> BinaryProtocolUtils::calculateSHA256Binary(const std::vector<uint8_t>& data)
{
    // Use existing ProtocolUtils to calculate SHA-256, then convert to binary
    auto hexChecksum = ProtocolUtils::calculateSHA256(data);
    auto binaryChecksum = hexToBinary(hexChecksum);
    
    std::array<uint8_t, 32> result{};
    if (binaryChecksum.size() >= 32) {
        std::copy(binaryChecksum.begin(), binaryChecksum.begin() + 32, result.begin());
    }
    
    return result;
}

bool BinaryProtocolUtils::verifyChecksum(const std::vector<uint8_t>& data, const uint8_t* expectedChecksum)
{
    auto actualChecksum = calculateSHA256Binary(data);
    return std::memcmp(actualChecksum.data(), expectedChecksum, 32) == 0;
}

} // namespace i2p::filetransfer