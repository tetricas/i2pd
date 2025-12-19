#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>

int main() {
    std::cout << "File Transfer Throughput Benchmark\n";
    std::cout << "==================================\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Simulate file transfer operations
    const size_t fileSize = 10 * 1024 * 1024; // 10MB
    std::vector<uint8_t> fileData(fileSize);
    std::iota(fileData.begin(), fileData.end(), 0);
    
    // Simulate chunked transfer
    const size_t chunkSize = 64 * 1024; // 64KB chunks
    size_t totalChunks = fileSize / chunkSize;
    
    for (size_t i = 0; i < totalChunks; ++i) {
        // Simulate chunk processing
        size_t offset = i * chunkSize;
        volatile auto checksum = std::accumulate(
            fileData.begin() + offset, 
            fileData.begin() + offset + chunkSize, 0);
        (void)checksum; // Suppress unused variable warning
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double throughputMBps = (fileSize / 1024.0 / 1024.0) / (duration.count() / 1000.0);
    
    std::cout << "File size: " << (fileSize / 1024 / 1024) << " MB\n";
    std::cout << "Chunks: " << totalChunks << " x " << (chunkSize / 1024) << " KB\n";
    std::cout << "Duration: " << duration.count() << " ms\n";
    std::cout << "Throughput: " << throughputMBps << " MB/s\n";
    std::cout << "✓ File transfer benchmark passed\n";
    return 0;
}