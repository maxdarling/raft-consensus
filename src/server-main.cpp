#include "Server.h"
#include <iostream>

int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if (argc < 2) {
        std::cerr << "Please specify the server number of this instance (i.e. "
            "which line of the server address list file corresponds to the "
            "instance you wish to launch?). See README for details.\n";
        return EXIT_FAILURE;
    }

    std::string serverFilePath = argc == 3? argv[2] : DEFAULT_SERVER_FILE_PATH;

    // unordered_map<int, sockaddr_in> clusterInfo = 
    //     parseClusterInfo(serverFilePath);
    unordered_map<int, std::string> clusterInfo = 
        parseClusterInfo(serverFilePath);
    if (clusterInfo.empty()) {
        std::cerr << "Invalid server address list! Either the file is "
            "improperly formatted, or the custom path to the file is wrong, or "
            "the default server_list has been deleted/moved/corrupted. See "
            "README for details.\n";
        return EXIT_FAILURE;
    }

    int serverNumber;
    try { serverNumber = std::stoi(argv[1]); }
    catch (const std::exception &exc) {
        std::cerr << "Invalid server number: " << exc.what();
        return EXIT_FAILURE;
    }

    std::cout << "SERVER #" << serverNumber << " NOW RUNNING\n";
    std::string myHostAndPort = clusterInfo[serverNumber];
    Server s(serverNumber, myHostAndPort, clusterInfo);
    s.run();

    return 0;
}
