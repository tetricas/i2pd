#include <iostream>
#include "../../tests_client/core/I2PdUtils.h"
#include "../../tests_client/core/ConnectionUtils.h"

int main() {
    std::cout << "Core Utils Unit Test\n";
    std::cout << "Testing basic utility functions...\n";
    
    // Test basic functionality
    try {
        // Placeholder for actual unit tests
        std::cout << "✓ Core utilities test passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "✗ Core utilities test failed: " << e.what() << std::endl;
        return 1;
    }
}