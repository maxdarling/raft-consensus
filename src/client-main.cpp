#include "Client.h"
#include <iostream>

const int CLIENT_PORT = 3030;

int main(int argc, char* argv[]) {
    std::string serverFilePath = argc == 2? argv[1] : DEFAULT_SERVER_FILE_PATH;

    unordered_map<int, sockaddr_in> clusterInfo = 
        parseClusterInfo(serverFilePath);
    if (clusterInfo.empty()) {
        std::cerr << "Invalid server address list! Either the file is "
            "improperly formatted, or the custom path to the file is wrong, or "
            "the default server_list has been deleted/moved/corrupted. See "
            "README for details.\n";
        return EXIT_FAILURE;
    }

    sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family      = AF_INET;     // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY;  // use local IP
    addr.sin_port        = htons(CLIENT_PORT);

    Client c(addr, clusterInfo);
    c.run();

    return 0;
}