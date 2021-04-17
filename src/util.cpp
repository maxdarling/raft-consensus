#include "util.h"
#include <fstream>

/**
 * Parses a server address file into a map from server number to address.
 * See README for details about how this file should be formatted.
 */
std::unordered_map<int, std::string> parseClusterInfo(std::string serverFilePath) {
    std::unordered_map<int, std::string> clusterInfo;
    
    std::ifstream ifs(serverFilePath);
    std::string hostAndPort;
    for (int serverNum = 1; ifs >> hostAndPort; ++serverNum) {
        clusterInfo.emplace(serverNum, hostAndPort);
    }

    if (clusterInfo.empty()) {
        throw std::invalid_argument("Invalid (or empty) server address list! "
            "Either the file is improperly formatted, or the custom path to "
            "the file is wrong, or the default server_list has been "
            "deleted/moved/corrupted. See README for details.");
    }

    return clusterInfo;
}

/**
 * Takes a string of the format "IP:port" and returns the port as an integer.
 * Example behavior: "127.0.0.95:8000" -> 8000.
 */
int parsePort(std::string hostAndPort) {
    return std::stoi(hostAndPort.substr(hostAndPort.find(":") + 1));
}
