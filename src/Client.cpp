#include "Client.h"
#include "RaftRPC.pb.h"
#include <iostream>
#include <thread> 

/* How long the client should wait before trying another server, in ms. */
const int REQUEST_TIMEOUT = 3000;

using std::string, std::optional;

/**
 * Construct a client instance at the given address to be serviced by the
 * given cluster.
 */
RaftClient::RaftClient(const std::string cluster_file)
  : server_addrs(parseClusterInfo(cluster_file)) {}

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

    ClientRequest cr_response;
    // rate limit retries so we don't exhaust all open files in system
    for (; !cr_response.success(); std::this_thread::sleep_for(2s)) {
        // send a request to the presumed leader
        messenger.sendRequest(server_addrs[leader_no], serialized_request);

        // wait for a response
        optional<string> msg_opt = messenger.getNextResponse(REQUEST_TIMEOUT);
        if (!msg_opt) {
            // if no response, try a new server
            leader_no = (leader_no + 1) % server_addrs.size();
            if (leader_no == 0) leader_no++;
        }

        RAFTmessage msg;
        msg.ParseFromString(*msg_opt);
        if (!msg.has_clientrequest_message()) {
            return "ERROR: ill-formed response from server";
        }
        cr_response = msg.clientrequest_message();
        leader_no = cr_response.leader_no();
    }

    return cr_response.output();
}
