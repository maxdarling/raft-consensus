#include "Client.h"
#include <iostream>

const int CLIENT_PORT = 3030;

int main(int argc, char* argv[]) {
    std::string serverFilePath = argc == 2? argv[1] : DEFAULT_SERVER_FILE_PATH;

    unordered_map<int, std::string> clusterInfo = 
        parseClusterInfo(serverFilePath);
    if (clusterInfo.empty()) {
        std::cerr << "Invalid server address list! Either the file is "
            "improperly formatted, or the custom path to the file is wrong, or "
            "the default server_list has been deleted/moved/corrupted. See "
            "README for details.\n";
        return EXIT_FAILURE;
    }


    // note: this won't be needed once socket architecture is changed
    int clientIP = INADDR_ANY; // 0
    std::string myHostAndPort = std::to_string(clientIP) + ":" +  
                                std::to_string(CLIENT_PORT);

    Client c(myHostAndPort, clusterInfo);
    c.run();

    return 0;
}