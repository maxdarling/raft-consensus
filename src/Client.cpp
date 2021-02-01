#include "Client.h"
#include "RaftRPC.pb.h"
#include <iostream>

Client::Client(const sockaddr_in &clientAddr, const unordered_map<int, sockaddr_in>& clusterInfo)
    : _messenger(-1, clusterInfo, true, ntohs(clientAddr.sin_port)),
      _addr(ntohs(clientAddr.sin_addr.s_addr)),
      _port(ntohs(clientAddr.sin_port)) {}

std::string Client::executeCommand(std::string command) {
    std::string serializedRequest;
    {
        RPC rpc;
        ClientRequest *cr = new ClientRequest();
        cr->set_command(command);
        cr->set_client_addr(_addr);
        cr->set_client_port(_port);
        rpc.set_allocated_clientrequest_message(cr);
        serializedRequest = rpc.SerializeAsString();
    }

    RPC server_response;
    do {
        _messenger.sendMessageToServer(_leaderID, serializedRequest);

        std::optional<std::string> msg_opt;
        // TODO(ali): timeout and try a different server if this one crashes
        // maybe pull the Timer class into its own file and use it?
        while (!msg_opt) msg_opt = _messenger.getNextMessage();
        server_response.ParseFromString(*msg_opt);

        if (!server_response.has_clientrequest_message()) {
            std::cout << "received ill-formed msg from server\n";
            return {};
        }

        _leaderID = server_response.clientrequest_message().leader_id();
        std::cout << "we should go to " << _leaderID << "\n";
    } while (!server_response.clientrequest_message().success());

    // server_response.clientrequest_message().output() stores the output here
    std::cout << "GREAT SUCCESS!\n";

    return {};
}