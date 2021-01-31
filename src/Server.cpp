#include "Server.h"
#include <iostream>
#include <fstream>   // for fstream
#include <random>    // for random_device
#include <cstdlib>   // for rand()

// in milliseconds
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2500;

// Construct a timer that, once started, expires after duration_ms milliseconds.
Timer::Timer(int duration_ms) : _timer_duration(duration_ms) {}

// Construct a timer that, once started, expires after a random duration within 
// the interval [duration_ms_lower_bound, duration_ms_upper_bound].
Timer::Timer(int duration_ms_lower_bound, int duration_ms_upper_bound) 
    : _lower_bound(duration_ms_lower_bound),
      _upper_bound(duration_ms_upper_bound) {
    // seed pseudorandom generator with truly random value
    std::random_device rd;
    srand(rd());
}

// Start the timer. If the timer is already running, it is restarted.
void Timer::start() {
    _start_time = std::chrono::steady_clock::now();
    if (_lower_bound) {
        _timer_duration = std::chrono::milliseconds {
            rand() % (*_upper_bound - *_lower_bound + 1) + *_lower_bound
        };
    }
}

// Returns true when the timer has expired, or if the timer has been marked as
// expired.
bool Timer::has_expired() {
    if (_marked_as_expired) {
        _marked_as_expired = false;
        _start_time.reset();
        return true;
    }

    if (!_start_time) return false;
    
    if (std::chrono::steady_clock::now() - *_start_time >= _timer_duration) {
        _start_time.reset();
        return true;
    } 
    return false;
}

// SERVER CONSTRUCTOR
Server::Server(const int server_id, const unordered_map<int, struct sockaddr_in>& cluster_info) 
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
    // if a server state file exists, recover persistent state from this file
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
    }
}

// Process and reply to RequestVote RPCs from leader.
void Server::handler_RequestVote(const RequestVote &rv) {
    if (!rv.is_request()) {
        std::cerr << "Received RequestVote response outside of try_election()\n";
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
            _vote = {_current_term, rv.candidate_id()};
            rv_reply->set_vote_granted(true);
        } else std::cerr << _server_id << ": term more up-do-date than " << rv.candidate_id() << ", declining vote\n";
    } else std::cerr << _server_id << ": voted for " << _vote->voted_for << " this term, declining vote for " << rv.candidate_id() << "\n";
    rpc.set_allocated_requestvote_message(rv_reply);

    send_RPC(rpc, rv.candidate_id());
}

// Append a command from the client to the local log, then send RPC response
// with the result of executing the command.
void Server::handler_ClientCommand() {
    // if (!_leader) {
        // send RPC response to client w/ address of leader
        // return;
    // }

    // append entry to local log

    // execute bash command, send RPC response to client w/ result
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
            // CASE: different server is also running an election; reply to their request
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
    _messenger.sendMessage(server_id, rpc_str);
}

// Return an RPC if one has been received.
std::optional<RPC> Server::receive_RPC() {
    std::optional<std::string> msg_opt = _messenger.getNextMessage();
    if (!msg_opt) return std::nullopt;
    RPC rpc;
    rpc.ParseFromString(*msg_opt);
    return rpc;
}
