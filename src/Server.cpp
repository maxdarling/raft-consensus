#include "Server.h"
#include <iostream>
#include <array>
#include <fstream>   // for fstream

// in milliseconds
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2500;

// SERVER CONSTRUCTOR
Server::Server(const int server_id, const unordered_map<int, sockaddr_in>& cluster_info) 
    : _messenger(server_id, cluster_info), 
      _election_timer(ELECTION_TIMEOUT_LOWER_BOUND, ELECTION_TIMEOUT_UPPER_BOUND), 
      _heartbeat_timer(HEARTBEAT_TIMEOUT),
      _server_id(server_id),
      _cluster_list(cluster_info.size()) {
    // FOR DEBUG LOGGING -- route STDERR to file
    freopen("server_log.txt", "w", stderr);

    // Populate _cluster_list with IDs of servers in cluster
    transform(cluster_info.begin(), cluster_info.end(), _cluster_list.begin(), 
        [] (auto pair) { return pair.first; }
    );

    std::string fname = "server" + std::to_string(_server_id) + "_state";
    std::ifstream ifs(fname, std::ios::binary);
    // if a server state recovery file exists, recover persistent state from this file
    if (ifs) {
        ServerPersistentState sps;
        sps.ParseFromIstream(&ifs);
        _current_term = sps.current_term();
        if (sps.voted_for() != 0) _vote = {sps.term_voted(), sps.voted_for()};
        ifs.close();
        std::cerr << _server_id << ": recovering state {" << sps.current_term() << ", " << sps.term_voted() << ", " << sps.voted_for() << "}\n";
    }
}

// Start the server, so that it may respond to requests from clients and other
// servers.
void Server::run() {
    std::cerr << _server_id << ": server now running\n";
    _election_timer.start();

    for (;;) {
        if (!_leader && _election_timer.has_expired()) while (!try_election());
        if (_leader) leader_tasks();
        std::optional<RPC> rpc_received_opt = receive_RPC();
        if (rpc_received_opt) RPC_handler(*rpc_received_opt);
    }
}

// Updates persistent state variables on disk, then routes RPCs to their 
// correct handler.
void Server::RPC_handler(const RPC &rpc) {
    // update persistent state variables on disk
    std::string fname = "server" + std::to_string(_server_id) + "_state";
    std::ofstream ofs(fname, std::ios::trunc | std::ios::binary);
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
        // TODO(ali): duplicate term argument in RPC?
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

// Execute the RAFT server rules for a leader:
// - Sending heartbeats to followers
// - Sending AppendEntries RPCs to followers
// - Updating _commit_index
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

    // for (int server_no = 0; server_no < _next_index.size(); server_no++) {
        // if (_log.size() > _next_index[server_no]) {
            // send AppendEntries RPC with log entries starting at _next_index[server_no]
            // if successful, update nextIndex and matchIndex for follower
            // if failure due to log inconsistency, decrement nextIndex and retry
        // }
    // }

    // for (int server_no = 0; server_no < _match_index.size(); server_no++) {
        // if there exists an N such that N > commitIndex, a majority of matchIndex[i] (?) >= N, and log[N].term == currentTerm, 
        // then set commitIndex = N
    // }
}

// Process and reply to AppendEntries RPCs from leader.
void Server::handler_AppendEntries(const AppendEntries &ae) {
    std::cerr << _server_id << ": received AppendEntries from " << ae.leader_id() << "\n";
    
    // Reject stale requests.
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

// Process and reply to RequestVote RPCs from leader.
void Server::handler_RequestVote(const RequestVote &rv) {
    if (!rv.is_request()) {
        std::cerr << _server_id << ": received RequestVote response outside of election\n";
        return;
    }

    RPC rpc;
    RequestVote *rv_reply = new RequestVote();
    rv_reply->set_term(_current_term);
    rv_reply->set_is_request(false);
    rv_reply->set_vote_granted(false);
    if (!_vote || _vote->term_voted < _current_term || _vote->voted_for == rv.candidate_id()) {
        if (rv.term() >= _current_term) {
            std::cerr << _server_id << ": voting for " << rv.candidate_id() << "\n";
            rv_reply->set_vote_granted(true);
            _vote = {_current_term, rv.candidate_id()};
        } else std::cerr << _server_id << ": term more up-do-date than " << rv.candidate_id() << ", declining vote\n";
    } else std::cerr << _server_id << ": voted for " << _vote->voted_for << " this term, declining vote for " << rv.candidate_id() << "\n";
    rpc.set_allocated_requestvote_message(rv_reply);
    send_RPC(rpc, rv.candidate_id());
}

// Append a command from the client to the local log, then send RPC response
// with the result of executing the command.
void Server::handler_ClientCommand(const ClientRequest &cr) {
    sockaddr_in client_addr;
    memset(&client_addr, '0', sizeof(client_addr));
    client_addr.sin_family = AF_INET; // use IPv4
    client_addr.sin_addr.s_addr = htons(cr.client_addr()); // use local IP
    client_addr.sin_port = htons(cr.client_port());

    if (!_leader) {
        RPC rpc;
        ClientRequest *cr_reply = new ClientRequest();
        cr_reply->set_success(false);
        cr_reply->set_leader_id(_last_observed_leader_id);
        rpc.set_allocated_clientrequest_message(cr_reply);
        _messenger.sendMessageToClient(client_addr, rpc.SerializeAsString());
        std::cerr << _server_id << ": RECEIVED CLIENT COMMAND, routing to " << _last_observed_leader_id << "\n";
        return;
    }

    std::string command = cr.command();
    std::cerr << _server_id << ": LEADER RECEIVED CLIENT COMMAND -> " << command << "\n";



    std::thread th(&Server::process_command_routine, this, command, client_addr);
    th.detach();

    // append entry to local log
    // execute bash command, send RPC response to client w/ result
}

// https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
void Server::process_command_routine(std::string cmd, sockaddr_in client_addr) {
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

// Trigger an election by sending RequestVote RPCs to all other servers.
// RETURN: true if a new leader is elected, false otherwise
bool Server::try_election() {
    std::cerr << _server_id << ": starting election\n";
    ++_current_term;
    int votes = 1; // vote for self
    _vote = {_current_term, _server_id};
    
    _election_timer.start();

    // send RequestVote RPC to all other servers
    RPC rpc;
    RequestVote *rv = new RequestVote();
    rv->set_is_request(true);
    rv->set_term(_current_term);
    rv->set_candidate_id(_server_id);
    rpc.set_allocated_requestvote_message(rv);
    send_RPC(rpc);

    for (;;) {
        if (_election_timer.has_expired()) {
            std::cerr << _server_id << ": election timer expired; trying again\n";
            return false;
        }

        std::optional<RPC> rpc_received_opt = receive_RPC();
        if (!rpc_received_opt) continue;

        if (rpc_received_opt->has_requestvote_message()) {
            const RequestVote &rv_received = rpc_received_opt->requestvote_message();
            // CASE: different server is also running an election; respond to their request
            if (rv_received.is_request()) {
                std::cerr << _server_id << ": received RequestVote from " << rv_received.candidate_id() << " in election\n";
                RPC_handler(*rpc_received_opt);
            }
            else if (rv_received.vote_granted()) {
                std::cerr << _server_id << ": received vote\n";
                votes++;
            }
            else std::cerr << _server_id << ": received declined vote\n";
        }

        // CASE: received an RPC message other than RequestVote
        else {
            std::cerr << _server_id << ": received non-RequestVote RPC in election\n";
            RPC_handler(*rpc_received_opt);
            // CASE: different server has been elected leader and sent AppendEntries RPC
            if (rpc_received_opt->has_appendentries_message()) return true;
        }
        
        // CASE: election won
        if (votes > _cluster_list.size() / 2) {
            std::cerr << _server_id << ": election won\n";
            _leader = true;
            _last_observed_leader_id = _server_id;
            // reinit nextIndex[] and matchIndex[]
            _heartbeat_timer.mark_as_expired();
            return true;
        }
    }
}

// Execute commands from log until _last_applied == _commit_index.
void Server::apply_log_entries() {}

// Send RPC to all other servers in the cluster.
void Server::send_RPC(const RPC &rpc) {
    for (int server_id : _cluster_list) {
        if (server_id == _server_id) continue;
        send_RPC(rpc, server_id);
    }
}

// Send RPC to a specific server in the cluster.
void Server::send_RPC(const RPC &rpc, int server_id) {
    std::string rpc_str = rpc.SerializeAsString();
    _messenger.sendMessageToServer(server_id, rpc_str);
}

// Return an RPC if one has been received.
std::optional<RPC> Server::receive_RPC() {
    std::optional<std::string> msg_opt = _messenger.getNextMessage();
    if (!msg_opt) return std::nullopt;
    RPC rpc;
    rpc.ParseFromString(*msg_opt);
    return rpc;
}
