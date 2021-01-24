#include "Server.h"

// Start the server, so that it may respond to requests from clients and other
// servers.
void Server::run() {}

// Update persistent state variables on disk and route RPCs to the correct 
// handler.
void Server::RPC_handler() {}

// Execute the RAFT server rules for a leader:
// - Sending heartbeats to followers
// - Sending AppendEntries RPCs to followers
// - Updating _commit_index
void Server::leader_tasks() {}

// Process and reply to AppendEntries RPCs from leader.
void Server::handler_AppendEntries() {}

// Process and reply to RequestVote RPCs from leader.
void Server::handler_RequestVote() {}

// Append a command from the client to the local log, then send RPC response
// with the result of executing the command.
void Server::handler_ClientCommand() {}

// Trigger an election by sending RequestVote RPCs to all other servers.
// RETURN: true if a new leader is elected, false otherwise
bool Server::try_election() {}

// Execute commands from log until _last_applied == _commit_index.
void Server::apply_log_entries() {}
