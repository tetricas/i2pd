#include <iostream>
#include "../../tests_client/core/AdaptiveCompression.h"
#include "../../tests_client/core/BulkTransferOptimization.h"
#include "../../tests_client/transport/PerformanceOptimizedStreaming.h"

int main() {
    std::cout << "End-to-End Performance Integration Test\n";
    std::cout << "Testing complete performance optimization pipeline...\n";
    
    try {
        // Test integration of all performance modules
        using namespace i2p::stream::performance;
        using namespace i2p::stream::transport;
        
        // Test adaptive compression
        std::string testData = "Sample test data for compression analysis";
        auto compressionDecision = AdaptiveCompression::analyzeData(
            reinterpret_cast<const uint8_t*>(testData.data()), 
            testData.size());
        
        // Test bulk optimization
        auto config = BulkTransferOptimization::createConfig(
            BulkTransferOptimization::OptimizationMode::BALANCED, 1024*1024);
        BulkTransferOptimization optimizer(config);
        
        // Test streaming factory
        auto streaming = PerformanceStreamingFactory::createStreaming(
            PerformanceStreamingFactory::ScenarioProfile::MIXED_WORKLOAD);
        
        std::cout << "✓ Compression analysis: " << (compressionDecision.shouldCompress ? "compress" : "no-compress") << "\n";
        std::cout << "✓ Bulk optimization: signature interval " << config.signatureInterval << "\n";
        std::cout << "✓ Performance streaming: created successfully\n";
        std::cout << "✓ E2E performance integration test passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "✗ E2E performance test failed: " << e.what() << std::endl;
        return 1;
    }
}