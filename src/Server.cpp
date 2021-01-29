#include "Server.h"
#include <iostream>
#include <random>    // for random_device
#include <cstdlib>   // for rand()

// in milliseconds
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2500;

Timer::Timer(int duration_ms) : _timer_duration(duration_ms) {}
Timer::Timer(int duration_ms_lower_bound, int duration_ms_upper_bound) 
    : _lower_bound(duration_ms_lower_bound), _upper_bound(duration_ms_upper_bound) {
    // TODO(ali): better RNG?
    std::random_device rd;
    srand(rd());
}

void Timer::start() {
    _start_time = std::chrono::steady_clock::now();
    if (_lower_bound) {
        _timer_duration = std::chrono::milliseconds {
            rand() % (*_upper_bound - *_lower_bound + 1) + *_lower_bound
        };
    }
}

bool Timer::is_expired() {
    if (!_start_time) return false;
    if (std::chrono::steady_clock::now() - *_start_time >= _timer_duration) {
        _start_time.reset();
        return true;
    } return false;
}

// Constructor
Server::Server(const int server_id, const unordered_map<int, struct sockaddr_in>& cluster_info) 
    : _messenger(server_id, cluster_info), 
      _election_timer(ELECTION_TIMEOUT_LOWER_BOUND, ELECTION_TIMEOUT_UPPER_BOUND), 
      _heartbeat_timer(HEARTBEAT_TIMEOUT),
      _server_id(server_id) {}

// Start the server, so that it may respond to requests from clients and other
// servers.
void Server::run() {
    _election_timer.start();

    for (;;) {
        if (!_leader && _election_timer.is_expired()) while (!try_election());
        // if (_leader) leader_tasks();
        // std::optional<RPC::container> msg_option = _messenger.getNextMessage();
        // if (msg_option) RPC_handler(*msg_option);
    }
}

// Update persistent state variables on disk and route RPCs to the correct 
// handler.
void Server::RPC_handler(/* RPC::container msg */) {
    // update persistent state variables on disk
    // parse request from msg
    // if request term T > currentTerm, currentTerm := T, set leader = false
    // route request to correct handler
}

// Execute the RAFT server rules for a leader:
// - Sending heartbeats to followers
// - Sending AppendEntries RPCs to followers
// - Updating _commit_index
void Server::leader_tasks() {
    // if heartbeat timer expired, reset heartbeat timer & send empty AppendEntries RPCs to each server

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
void Server::handler_AppendEntries() {}

// Process and reply to RequestVote RPCs from leader.
void Server::handler_RequestVote() {}

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
    std::cout << "Server " << _server_id << " starting election\n";
    ++_current_term;
    int votes = 1; // vote for self
    
    _election_timer.start();
    // send RequestVote RPC to all other servers
    // for (;;) {
        // if (election timer has expired) return false;
        // std::optional<RPC::container> msg_option = _messenger.getNextMessage();
        // if (msg_option) {
            // if (AppendEntries RPC received) _leader = false, forward request to RPC_handler, return true
            // if (RequestVote RPC reply received granting request) votes++
            // if (votes > # of servers / 2) {
                // _leader = true;
                // reinit heartbeat timer to expired state
                // reinit nextIndex[] and matchIndex[]
                // return true;
            // }
        // }
    // }
    return true; // stifle compiler warning for now
}

// Execute commands from log until _last_applied == _commit_index.
void Server::apply_log_entries() {}
