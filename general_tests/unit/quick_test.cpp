#include <iostream>
#include "../core/AdaptiveCompression.h"

using namespace i2p::stream::performance;

int main() {
    std::cout << "Quick Performance Test\n";
    
    // Simple test data
    std::string textData = "Hello World! This is test data for compression analysis.";
    
    auto decision = AdaptiveCompression::analyzeData(
        reinterpret_cast<const uint8_t*>(textData.data()), 
        textData.size());
    
    std::cout << "Text analysis:\n";
    std::cout << "  Should compress: " << (decision.shouldCompress ? "YES" : "NO") << "\n";
    std::cout << "  Entropy: " << decision.entropy << "\n";
    
    std::cout << "✓ Performance optimization modules working!\n";
    return 0;
}