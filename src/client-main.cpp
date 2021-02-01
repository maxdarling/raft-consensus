#include "Client.h"
#include <iostream>

const int CLIENT_PORT = 3030;
const int SERVER_PORT_BASE = 5000;

int main(int argc, char* argv[]) {
    // TODO(ali): error checking here
    // define the server list
    sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(CLIENT_PORT);

    // explicit initialization because loop version is less easy to visualize
    unordered_map<int, sockaddr_in> clusterInfo {
        {1, addr},
        {2, addr},
        {3, addr}
    };

    // overwrite with correct server port numbers
    for (auto it = clusterInfo.begin(); it != clusterInfo.end(); ++it) {
        it->second.sin_port = htons(SERVER_PORT_BASE + it->first);
    }

    Client c(addr, clusterInfo);
    c.run();

    return 0;
}