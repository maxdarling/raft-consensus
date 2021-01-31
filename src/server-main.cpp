// TODO(ali): get protobufs to work
#include <iostream>
#include "RaftRPC.pb.h"
#include "Server.h"

const int PORT_BASE = 5000;

int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    // TODO(ali): error checking here
    int server_number = std::stoi(argv[1]);
    
    std::cout << "Server #" << server_number << " has started\n";

    // define the server list
    struct sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(PORT_BASE);

    // explicit initialization because loop version is less easy to visualize
    unordered_map<int, struct sockaddr_in> cluster_info {
        {1, addr},
        {2, addr},
        {3, addr},
        {4, addr},
        {5, addr}
    };
    for (auto it = cluster_info.begin(); it != cluster_info.end(); ++it) {
        it->second.sin_port = htons(PORT_BASE + it->first);
        std::cout << "created server list entry. ID = "<< it->first << ",  port = " << it->second.sin_port << "\n";
    }

    Server s(server_number, cluster_info);
    s.run();

    return 0;
}
