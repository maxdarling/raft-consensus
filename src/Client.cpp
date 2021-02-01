#include "Client.h"
#include "RaftRPC.pb.h"
#include <iostream>

Client::Client(const sockaddr_in &clientAddr, const unordered_map<int, sockaddr_in>& clusterInfo)
    : _messenger(-1, clusterInfo, true, ntohs(clientAddr.sin_port)),
      _addr(ntohs(clientAddr.sin_addr.s_addr)),
      _port(ntohs(clientAddr.sin_port)) {}

void Client::run() {
    std::cout << "---WELCOME TO RASH (RAFT SHELL)---\n";
    for (;;) {
        std::string cmd;
        std::cout << "> ";
        std::getline(std::cin, cmd);
        std::cout << executeCommand(cmd) << "\n";
    }
}

std::string Client::executeCommand(std::string cmd) {
    std::string serializedRequest;
    {
        RPC rpc;
        ClientRequest *cr = new ClientRequest();
        cr->set_command(cmd);
        cr->set_client_addr(_addr);
        cr->set_client_port(_port);
        rpc.set_allocated_clientrequest_message(cr);
        serializedRequest = rpc.SerializeAsString();
    }

    RPC serverResponse;
    do {
        _messenger.sendMessageToServer(_leaderID, serializedRequest);

        std::optional<std::string> msgOpt;
        // TODO(ali): timeout and try a different server if this one crashes
        // maybe pull the Timer class into its own file and use it?
        while (!msgOpt) msgOpt = _messenger.getNextMessage();
        serverResponse.ParseFromString(*msgOpt);

        if (!serverResponse.has_clientrequest_message()) {
            std::cout << "received ill-formed msg from server\n";
            return {};
        }

        _leaderID = serverResponse.clientrequest_message().leader_id();
        // std::cout << "we should go to " << _leaderID << "\n";
    } while (!serverResponse.clientrequest_message().success());

    return serverResponse.clientrequest_message().output();
}