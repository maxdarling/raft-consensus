#include "Server.h"
#include <iostream>

// Constructor
Server::Server() : _messenger(1, {}) {}

// Start the server, so that it may respond to requests from clients and other
// servers.
void Server::run() {
    // start election timer

    // for (;;) {
        // if !leader & election timer has expire, run try_election() until it returns true
        // if (_leader) leader_tasks();
        // std::optional<RPC::container> msg_option = _messenger.getNextMessage();
        // if (msg_option) RPC_handler(*msg_option);
    // }
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
    ++_current_term;
    int votes = 1; // vote for self
    // reset election timer
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
