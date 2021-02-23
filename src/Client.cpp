#include "Client.h"
#include "RaftRPC.pb.h"
#include <iostream>

/**
 * Construct a client instance at the given address to be serviced by the
 * given cluster.
 */
RaftClient::RaftClient(int client_port, const std::string cluster_file)
  : messenger(client_port),
    server_addrs(parseClusterInfo(cluster_file)) {}

/**
 * Send a BASH cmd string to be run on the RAFT cluster, await a response, 
 * and return the output of the command.
 */
std::string RaftClient::execute_command(std::string cmd) {
    std::string serialized_request;
    {
        RAFTmessage msg;
        ClientRequest *cr = new ClientRequest();
        msg.set_allocated_clientrequest_message(cr);
        cr->set_command(cmd);
        serialized_request = msg.SerializeAsString();
    }

    RAFTmessage server_response;
    do {
        // Cycle through servers until we find one that's not down
        while (!messenger.sendRequest(server_addrs[leader_no], 
            serialized_request)) {
            leader_no = (leader_no + 1) % server_addrs.size();
            if (leader_no == 0) leader_no++;
        }

        return "sent"; // DEBUG

        // TODO(ali): incorporate the timeout feature
        std::optional<std::string> msg_opt = messenger.getNextResponse();
        if (msg_opt) server_response.ParseFromString(*msg_opt);
        else continue;

        // TODO(ali): fix dis
        // CASE: ill-formed response from server (should never happen)
        if (!server_response.has_clientrequest_message()) return {};

        leader_no = server_response.clientrequest_message().leader_no();
    } while (!server_response.clientrequest_message().success());

    return server_response.clientrequest_message().output();
}
