#include "Client.h"
#include "RaftRPC.pb.h"
#include <iostream>

// only used in constructor. behavior: "127.0.0.95:8000" -> 8000
int parsePort(std::string hostAndPort) {
    return std::stoi(hostAndPort.substr(hostAndPort.find(":") + 1));
}


/**
 * Construct a client instance at the given address to be serviced by the
 * given cluster.
 */
Client::Client(std::string myHostAndPort, 
    const unordered_map<int, std::string>& cluster_map)
    : _messenger(parsePort(myHostAndPort)),
      _myHostAndPort(myHostAndPort),
      _cluster_map(cluster_map) {}

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
        cr->set_client_hostandport(_myHostAndPort);
        rpc.set_allocated_clientrequest_message(cr);
        serializedRequest = rpc.SerializeAsString();
    }

    RPC serverResponse;
    do {
        // Cycle through servers until we find one that's not down
        while (!_messenger.sendMessage(_cluster_map[_leaderID], serializedRequest)) {
            _leaderID = (_leaderID + 1) % _cluster_map.size();
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
