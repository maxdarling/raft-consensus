#include "Client.h"
#include <iostream>

Client::Client(const sockaddr_in &clientAddr, const unordered_map<int, sockaddr_in>& clusterInfo)
    : _messenger(-1, clusterInfo, true, ntohs(clientAddr.sin_port)) {}

std::string Client::executeCommand(std::string command) {
    return {};
}