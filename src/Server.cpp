#include "Server.h"
#include <array>
#include <fstream>   // for ifstream, ofstream

/* In milliseconds */
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2500;

/**
 * Construct one server instance that will receive connections at the address
 * and port specified by the cluster_info entry corresponding to the given
 * server ID.
 */
Server::Server(const int server_id, std::string myHostAndPort,
    const unordered_map<int, std::string>& cluster_info) 
    : _messenger(server_id, myHostAndPort), 
      _myHostAndPort(myHostAndPort),
      _serverIdToHostAndPort(cluster_info),
      _election_timer(ELECTION_TIMEOUT_LOWER_BOUND,
                      ELECTION_TIMEOUT_UPPER_BOUND), 
      _heartbeat_timer(HEARTBEAT_TIMEOUT),
      _server_id(server_id),
      _recovery_fname("server" + std::to_string(_server_id) + "_state"),
      _cluster_list(cluster_info.size()) {
    // Populate _cluster_list with IDs of servers in cluster
    transform(cluster_info.begin(), cluster_info.end(), _cluster_list.begin(), 
        [] (auto pair) { return pair.first; }
    );

    // If a server state recovery file exists, assume we have crashed and must
    // now recover persistent state variables from this file
    std::ifstream ifs(_recovery_fname, std::ios::binary);
    if (ifs) {
        ServerPersistentState sps;
        sps.ParseFromIstream(&ifs);
        _current_term = sps.current_term();
        if (sps.voted_for() != 0) _vote = {sps.term_voted(), sps.voted_for()};
        ifs.close();
    }
}

/**
 * Start the server, so that it may respond to requests from clients and other
 * servers.
 */
void Server::run() {
    _election_timer.start();

    for (;;) {
        if (!_leader && _election_timer.has_expired()) while (!try_election());
        if (_leader) leader_tasks();
        std::optional<RPC> rpc_received_opt = receive_RPC();
        if (rpc_received_opt) RPC_handler(*rpc_received_opt);
    }
}

/** 
 * Updates persistent state variables on disk, then routes RPCs to their 
 * correct handler.
 */
void Server::RPC_handler(const RPC &rpc) {
    // Update persistent state variables on disk
    std::ofstream ofs(_recovery_fname, std::ios::trunc | std::ios::binary);
    ServerPersistentState sps;
    sps.set_current_term(_current_term);
    if (_vote) {
        sps.set_term_voted(_vote->term_voted);
        sps.set_voted_for(_vote->voted_for);
    }
    sps.SerializeToOstream(&ofs);
    ofs.close();

    if (rpc.has_requestvote_message()) {
        const RequestVote &rv = rpc.requestvote_message();
        if (rv.term() > _current_term) {
            _current_term = rv.term();
            _leader = false;
        }
        if (rv.term() == _current_term) _election_timer.start();
        handler_RequestVote(rv);
    }

    else if (rpc.has_appendentries_message()) {
        const AppendEntries &ae = rpc.appendentries_message();
        if (ae.term() > _current_term) {
            _current_term = ae.term();
            _leader = false;
        }
        if (ae.term() == _current_term) _election_timer.start();
        handler_AppendEntries(ae);
    }

    else if (rpc.has_clientrequest_message()) {
        const ClientRequest &cr = rpc.clientrequest_message();
        handler_ClientCommand(cr);
    }
}

/**
 *  Execute the RAFT server rules for a leader:
 * - Sending heartbeats to followers
 * - Sending AppendEntries RPCs to followers (proj2)
 * - Updating _commit_index (proj2)
 */
void Server::leader_tasks() {
    if (_heartbeat_timer.has_expired()) {
        _heartbeat_timer.start();
        RPC rpc;
        AppendEntries *ae = new AppendEntries();
        ae->set_is_request(true);
        ae->set_term(_current_term);
        ae->set_leader_id(_server_id);
        rpc.set_allocated_appendentries_message(ae);
        send_RPC(rpc);
    }
}

/**
 * Process and reply to AppendEntries RPCs from leader.
 */
void Server::handler_AppendEntries(const AppendEntries &ae) {
    // CASE: reject stale request
    if (ae.term() < _current_term) {
        RPC rpc;
        AppendEntries *ae_reply = new AppendEntries();
        ae_reply->set_is_request(false);
        ae_reply->set_term(_current_term);
        rpc.set_allocated_appendentries_message(ae_reply);
        send_RPC(rpc, ae.leader_id());
        return;
    }

    _last_observed_leader_id = ae.leader_id();
}

/**
 * Process and reply to RequestVote RPCs from leader.
 */
void Server::handler_RequestVote(const RequestVote &rv) {
    // CASE: received RV response outside of election
    if (!rv.is_request()) return;

    RPC rpc;
    RequestVote *rv_reply = new RequestVote();
    rv_reply->set_term(_current_term);
    rv_reply->set_is_request(false);
    rv_reply->set_vote_granted(false);
    if (!_vote || _vote->term_voted < _current_term 
               || _vote->voted_for == rv.candidate_id()) {
        if (rv.term() >= _current_term) {
            rv_reply->set_vote_granted(true);
            _vote = {_current_term, rv.candidate_id()};
        }
    }
    rpc.set_allocated_requestvote_message(rv_reply);
    send_RPC(rpc, rv.candidate_id());
}

/**
 * Append a command from the client to the local log (proj2), then send RPC
 * response with the result of executing the command.
 */  
void Server::handler_ClientCommand(const ClientRequest &cr) {
    sockaddr_in client_addr;
    memset(&client_addr, '0', sizeof(client_addr));
    client_addr.sin_family = AF_INET; // use IPv4
    client_addr.sin_addr.s_addr = htons(cr.client_addr()); // use local IP
    client_addr.sin_port = htons(cr.client_port());

    // CASE: we're not the leader, so send response to client with ID of the
    // last leader we observed
    if (!_leader) {
        RPC rpc;
        ClientRequest *cr_reply = new ClientRequest();
        cr_reply->set_success(false);
        cr_reply->set_leader_id(_last_observed_leader_id);
        rpc.set_allocated_clientrequest_message(cr_reply);
        _messenger.sendMessageToClient(client_addr, rpc.SerializeAsString());
        return;
    }

    // Execute command on its own thread so we don't block
    std::thread th(&Server::process_command_routine, 
        this, cr.command(), client_addr);
    th.detach();
}

/**
 * Execute cmd string via bash and send response to client with the output.
 */
void Server::process_command_routine(std::string cmd, sockaddr_in client_addr) {
    // See README for a citation for this code snippet
    std::string result;
    std::array<char, 128> buf;
    std::string bash_cmd = "bash -c \"" + cmd + "\"";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(bash_cmd.c_str(), "r"), pclose);
    if (!pipe) {
        std::cerr << "popen() failed!\n";
        return;
    }
    while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr) {
        result += buf.data();
    }

    // Send reply to client with result of running command
    RPC rpc;
    ClientRequest *cr_reply = new ClientRequest();
    cr_reply->set_success(true);
    cr_reply->set_leader_id(_last_observed_leader_id);
    cr_reply->set_output(result);
    rpc.set_allocated_clientrequest_message(cr_reply);
    _messenger.sendMessageToClient(client_addr, rpc.SerializeAsString());
}

/**
 * Trigger an election by sending RequestVote RPCs to all other servers.
 *  Return true if a new leader is elected, false otherwise.
 */
bool Server::try_election() {
    ++_current_term;
    int votes = 1; // vote for self
    _vote = {_current_term, _server_id};
    
    _election_timer.start();

    // Send RequestVote RPC to all other servers
    RPC rpc;
    RequestVote *rv = new RequestVote();
    rv->set_is_request(true);
    rv->set_term(_current_term);
    rv->set_candidate_id(_server_id);
    rpc.set_allocated_requestvote_message(rv);
    send_RPC(rpc);

    for (;;) {
        if (_election_timer.has_expired()) return false;

        std::optional<RPC> rpc_received_opt = receive_RPC();
        if (!rpc_received_opt) continue;

        if (rpc_received_opt->has_requestvote_message()) {
            const RequestVote &rv_received = 
                rpc_received_opt->requestvote_message();

            // CASE: different server is also running an election; 
            // respond to their request
            if (rv_received.is_request()) RPC_handler(*rpc_received_opt);
            else if (rv_received.vote_granted()) votes++;
        }

        // CASE: received an RPC message other than RequestVote
        else {
            RPC_handler(*rpc_received_opt);
            // CASE: different server has been elected leader and sent 
            // AppendEntries RPC
            if (rpc_received_opt->has_appendentries_message()) return true;
        }
        
        // CASE: election won
        if (votes > _cluster_list.size() / 2) {
            _leader = true;
            _last_observed_leader_id = _server_id;
            _heartbeat_timer.mark_as_expired();
            return true;
        }
    }
}

/**
 * Execute commands from log until _last_applied == _commit_index (proj2).
 */ 
void Server::apply_log_entries() {}

/**
 * Send RPC to all other servers in the cluster.
 */
void Server::send_RPC(const RPC &rpc) {
    for (int server_id : _cluster_list) {
        if (server_id == _server_id) continue;
        send_RPC(rpc, server_id);
    }
}

/**
 * Send RPC to a specific server in the cluster.
 */ 
void Server::send_RPC(RPC rpc, int server_id) {
    rpc.set_senderhostandport(_myHostAndPort);
    std::string rpc_str = rpc.SerializeAsString();
    //_messenger.sendMessageToServer(server_id, rpc_str);
    _messenger.sendMessage(_serverIdToHostAndPort[server_id], rpc_str);
}

/**
 * Returns an RPC if one has been received.
 */ 
std::optional<RPC> Server::receive_RPC() {
    std::optional<std::string> msg_opt = _messenger.getNextMessage();
    if (!msg_opt) return std::nullopt;
    RPC rpc;
    rpc.ParseFromString(*msg_opt);
    return rpc;
}
