#include "util.h"
#include <fstream>
#include <iostream> // debugging

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


/**
 * Read 'count' bytes from 'filename' at the specified offset into the buffer
 * starting at 'buf'. Less than 'count' bytes will be read if the end of the 
 * file is reached. 
 * 
 * Returns true if the end of the file was reached, false otheriwse.  
 * 
 * If 'count' + 'offset' is past the end of the file, this function will read 
 * to the end of the file instead. If 'offset' is past the end of the file, or 
 * the file cannot be opened, an exception is thrown.
 */
bool readFileChunk(string filename, char *buf, size_t offset, size_t count) {
    std::ifstream is (filename, std::ios::binary);
    if (!is) {
        throw "readFileChunk(): " + string(strerror(errno));
    };
    
    // first, get the length of the file
    is.seekg(0, is.end);
    int file_len = is.tellg();
    std::cout << "file is " << file_len << " bytes long" << std::endl; 
    if (offset >= file_len) {
        throw "readFileChunk(): 'offset' is past the end of the file";
    }

    is.seekg (offset, is.beg);
    is.read (buf, count);
    // ifstream::read API quirk: no return value on # of bytes read, must calc
    size_t bytesRead = (is.eof() ? file_len - offset : count);
    is.close();

    std::cout << "read " << bytesRead << " of " << count << " bytes from file, "
    "starting at offset " << offset << std::endl;
    return is.eof();
}
