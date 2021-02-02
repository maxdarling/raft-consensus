#include "Client.h"
#include "RaftRPC.pb.h"
#include <iostream>

/**
 * Construct a client instance at the given address to be serviced by the
 * given cluster.
 */
Client::Client(const sockaddr_in &clientAddr, 
    const unordered_map<int, sockaddr_in>& clusterInfo)
    : _messenger(clusterInfo, ntohs(clientAddr.sin_port)),
      _clientAddr(ntohs(clientAddr.sin_addr.s_addr)),
      _clientPort(ntohs(clientAddr.sin_port)),
      _clusterSize(clusterInfo.size()) {}

/**
 * Launches a RAFT shell, which loops indefinitely, accepting commands to be
 * run on the RAFT cluster.
 */
void Client::run() {
    std::cout << "--- WELCOME TO RASH (THE RAFT SHELL) ---\n";
    for (;;) {
        std::string cmd;
        std::cout << "> ";
        std::getline(std::cin, cmd);
        std::cout << executeCommand(cmd) << "\n";
    }
}

/**
 * Send a BASH cmd string to be run on the RAFT cluster, await a response, 
 * and return the output of the command.
 */
std::string Client::executeCommand(std::string cmd) {
    std::string serializedRequest;
    {
        RPC rpc;
        ClientRequest *cr = new ClientRequest();
        cr->set_command(cmd);
        cr->set_client_addr(_clientAddr);
        cr->set_client_port(_clientPort);
        rpc.set_allocated_clientrequest_message(cr);
        serializedRequest = rpc.SerializeAsString();
    }

    RPC serverResponse;
    do {
        // Cycle through servers until we find one that's not down
        while (!_messenger.sendMessageToServer(_leaderID, serializedRequest)) {
            _leaderID = (_leaderID + 1) % _clusterSize;
            if (_leaderID == 0) ++_leaderID;
        }

        std::optional<std::string> msgOpt;
        while (!msgOpt) msgOpt = _messenger.getNextMessage();

        serverResponse.ParseFromString(*msgOpt);

        // CASE: ill-formed response from server (should never happen)
        if (!serverResponse.has_clientrequest_message()) return {};

        _leaderID = serverResponse.clientrequest_message().leader_id();
    } while (!serverResponse.clientrequest_message().success());

    return serverResponse.clientrequest_message().output();
}
