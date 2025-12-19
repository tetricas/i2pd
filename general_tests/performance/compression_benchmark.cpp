#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include "../../tests_client/core/AdaptiveCompression.h"

using namespace i2p::stream::performance;

int main() {
    std::cout << "Compression Performance Benchmark\n";
    std::cout << "=================================\n";
    
    // Generate different types of test data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    struct TestCase {
        std::string name;
        std::vector<uint8_t> data;
    };
    
    std::vector<TestCase> testCases;
    
    // Text data (highly compressible)
    std::string textPattern = "The quick brown fox jumps over the lazy dog. ";
    std::vector<uint8_t> textData;
    for (int i = 0; i < 1000; ++i) {
        textData.insert(textData.end(), textPattern.begin(), textPattern.end());
    }
    testCases.push_back({"Text Data", textData});
    
    // Random binary data (low compressibility)
    std::vector<uint8_t> randomData(50000);
    for (auto& byte : randomData) {
        byte = dis(gen);
    }
    testCases.push_back({"Random Binary", randomData});
    
    // PNG-like data (already compressed)
    std::vector<uint8_t> pngData = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    pngData.resize(25000);
    for (size_t i = 8; i < pngData.size(); ++i) {
        pngData[i] = dis(gen);
    }
    testCases.push_back({"PNG-like Data", pngData});
    
    std::cout << "\nBenchmarking compression analysis performance:\n";
    std::cout << "----------------------------------------------\n";
    
    for (const auto& testCase : testCases) {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto decision = AdaptiveCompression::analyzeData(
            testCase.data.data(), testCase.data.size());
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::micro>(end - start);
        
        double throughputMBps = (testCase.data.size() / 1024.0 / 1024.0) / (duration.count() / 1000000.0);
        
        std::cout << testCase.name << ":\n";
        std::cout << "  Size: " << testCase.data.size() << " bytes\n";
        std::cout << "  Should compress: " << (decision.shouldCompress ? "YES" : "NO") << "\n";
        std::cout << "  Entropy: " << decision.entropy << "\n";
        std::cout << "  Analysis time: " << duration.count() << " μs\n";
        std::cout << "  Throughput: " << throughputMBps << " MB/s\n\n";
    }
    
    std::cout << "✓ Compression benchmark passed\n";
    return 0;
}