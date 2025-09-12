#!/bin/bash

# i2pd Test Build Script
# Creates organized cmake builds for different test types

set -e  # Exit on any error

echo "i2pd Test Build Script"
echo "====================="

# Function to build tests in a specific directory
build_test_category() {
    local category=$1
    local build_dir="build/${category}"
    
    echo "Building ${category} tests..."
    
    # Create and enter build directory
    mkdir -p "${build_dir}"
    cd "${build_dir}"
    
    # Run cmake from the general_tests directory
    cmake ../../
    
    # Build all targets for this test category
    make ${category}_tests
    
    # Return to general_tests root
    cd ../../
    
    echo "✓ ${category} tests built successfully"
}

# Function to run a quick validation build
quick_build() {
    echo "Quick validation build..."
    
    mkdir -p build/quick
    cd build/quick
    
    # Build just the unit tests for validation
    cmake ../../
    make quick_test
    
    echo "Running quick validation..."
    ./unit/quick_test
    
    cd ../../
    echo "✓ Quick build validation passed"
}

# Main build logic
case "${1:-quick}" in
    "unit")
        build_test_category "unit"
        ;;
    "integration") 
        build_test_category "integration"
        ;;
    "performance")
        build_test_category "performance"
        ;;
    "all")
        echo "Building all test categories..."
        build_test_category "unit"
        build_test_category "integration" 
        build_test_category "performance"
        echo "✓ All tests built successfully"
        ;;
    "clean")
        echo "Cleaning build artifacts..."
        rm -rf build/*
        rm -rf CMakeFiles/
        rm -f CMakeCache.txt
        rm -f cmake_install.cmake
        rm -f Makefile
        echo "✓ Build artifacts cleaned"
        ;;
    "quick")
        quick_build
        ;;
    *)
        echo "Usage: $0 [unit|integration|performance|all|clean|quick]"
        echo ""
        echo "Categories:"
        echo "  unit        - Build unit tests (individual component tests)"
        echo "  integration - Build integration tests (component interaction tests)"
        echo "  performance - Build performance benchmarks"
        echo "  all         - Build all test categories"
        echo "  clean       - Clean all build artifacts"
        echo "  quick       - Quick validation build (default)"
        exit 1
        ;;
esac

echo ""
echo "Build completed successfully!"
echo ""
echo "Test structure:"
echo "  unit/         - Unit tests (fast, isolated)" 
echo "  integration/  - Integration tests (component interaction)"
echo "  performance/  - Performance benchmarks"
echo "  build/        - Build artifacts (ignored by git)"
echo "  data/         - Test data files"