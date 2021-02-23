#include "Client.h"
#include "RaftRPC.pb.h"
#include <iostream>

/* in milliseconds */
const int REQUEST_TIMEOUT = 5000;

using std::string, std::optional;

/**
 * Construct a client instance at the given address to be serviced by the
 * given cluster.
 */
RaftClient::RaftClient(int client_port, const std::string cluster_file)
  : messenger(client_port),
    server_addrs(parseClusterInfo(cluster_file)) {
        RAFTmessage msg;
        ClientRequest *cr = new ClientRequest();
        cr->set_echo_request(true);
        serialized_echo = msg.SerializeAsString();
}

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
    do {
        // cycle through servers until we find one that's not down
        while (!messenger.sendRequest(server_addrs[leader_no],
            serialized_request)) {
            leader_no = (leader_no + 1) % server_addrs.size();
            if (leader_no == 0) leader_no++;
        }

        optional<string> msg_opt;
        bool sent_echo = false;
        do {
            // If no response is received from the server within half the
            // timeout duration, an echo request is sent. If no echo response
            // is received within another half timeout duration, the server is
            // considered unresponsive, and the original request is resent to
            // a different server in the cluster.
            msg_opt = messenger.getNextResponse(REQUEST_TIMEOUT / 2);

            // CASE: received some response from server
            if (msg_opt) {
                RAFTmessage msg;
                msg.ParseFromString(*msg_opt);
                if (!msg.has_clientrequest_message()) {
                    return "ERROR: ill-formed response from server!";
                }
                cr_response = msg.clientrequest_message();
                LOG_F(INFO, "receiving smthing....");

                // CASE: received an echo, so allow ourself to send another
                if (cr_response.echo()) {
                    LOG_F(INFO, "client received echo");
                    sent_echo = false;
                }
            } 

            // CASE: no response received, so send an echo request
            else if (!sent_echo) {
                LOG_F(INFO, "client sending echo to leader");
                if (!messenger.sendRequest(server_addrs[leader_no], 
                    serialized_echo)) break;
                sent_echo = true;
            }
            else LOG_F(INFO, "wtf??");
        } while (sent_echo || (msg_opt && cr_response.echo()));

        if (!msg_opt) {
            LOG_F(INFO, "client request timed out: trying new server");
            continue;
        }

        leader_no = cr_response.leader_no();
    } while (!cr_response.success());

    return cr_response.output();
}
