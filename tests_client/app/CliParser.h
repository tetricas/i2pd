#pragma once

#include <string>

namespace i2p {
namespace embed {

/**
 * @brief Command line argument parsing utility
 */
struct CliArgs {
    std::string mode;        // server | client
    bool useSimpleProtocol{true};  // --simple (default) | --normal
    std::string datadir;     // --datadir DIR
    std::string conf;        // --conf FILE
    std::string server_b32;  // (client) --server-b32 <b32>
    std::string message{"hello"};  // (client) --message TEXT
};

class CliParser {
public:
    /**
     * @brief Parse command line arguments
     * @param argc Argument count
     * @param argv Argument values
     * @return Parsed arguments
     * @throws std::runtime_error on invalid arguments
     */
    static CliArgs parse(int argc, char** argv);
    
    /**
     * @brief Print usage information
     */
    static void printUsage();
};

} // namespace embed
} // namespace i2p