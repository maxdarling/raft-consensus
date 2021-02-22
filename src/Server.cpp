#include "Server.h"
#include <array>
#include <fstream>   // for ifstream, ofstream
#include <thread>

/* In milliseconds */
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2000;

/* V2
 * Construct server
 */
Server::Server(const int _server_no, const std::string cluster_file)
  : server_no(_server_no),
    server_addrs(parseClusterInfo(cluster_file)),
    // TODO(ali): tell max to accept "addr:port" string in messenger constructor API
    messenger(std::stoi(server_addrs[server_no].substr(server_addrs[server_no].find(":") + 1))),
    election_timer(ELECTION_TIMEOUT_LOWER_BOUND, ELECTION_TIMEOUT_UPPER_BOUND, 
                   std::bind(&Server::start_election, this)),
    heartbeat_timer(HEARTBEAT_TIMEOUT, std::bind(&Server::send_heartbeats, this))
{
    // TODO(ali): error checking here
    server_addrs = parseClusterInfo(cluster_file);
} 

/** V1
 * Construct one server instance that will receive connections at the address
 * and port specified by the cluster_info entry corresponding to the given
 * server ID.
 */
/*
Server::Server(const int server_id, 
    const unordered_map<int, sockaddr_in>& cluster_info) 
    : _messenger(server_id, cluster_info), 
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
*/

/* V2
 * run server
 */
void Server::run()
{
    LOG_F(INFO, "S%d now running @ %s", server_no, server_addrs[server_no].c_str());
    election_timer.start();

    // TODO(ali): better way of doing this? at least reorder
    std::thread(&Server::response_listener, this).detach();
    std::thread(&Server::request_listener,  this).join();
}

/** V1
 * Start the server, so that it may respond to requests from clients and other
 * servers.
 */
/*
void Server::run() {
    _election_timer.start();

    for (;;) {
        if (!_leader && _election_timer.has_expired()) while (!try_election());
        if (_leader) leader_tasks();
        std::optional<RPC> rpc_received_opt = receive_RPC();
        if (rpc_received_opt) RPC_handler(*rpc_received_opt);
    }
}
*/

/* V2
 * rl task
 */
void Server::request_listener()
{
    loguru::set_thread_name("request listener");

    for (;;) {
        std::optional<Messenger::Request> req_opt = messenger.getNextRequest();
        if (req_opt) request_handler(*req_opt);
    }
}

/* V2
 * dispatch RPC message to correct handler
 */
void Server::request_handler(Messenger::Request &req)
{
    RAFTmessage msg;
    msg.ParseFromString(req.message);

    {
        std::lock_guard<std::mutex> lock(m);
        if (msg.term() > current_term) {
            current_term = msg.term();
            server_state = FOLLOWER;
            //
        }
        if (msg.term() == current_term) election_timer.start();
    }

    if (msg.has_requestvote_message()) {
        handler_RequestVote(req, msg.requestvote_message());
    } 
    else if (msg.has_appendentries_message()) {
        handler_AppendEntries(req, msg.appendentries_message());
    } 
    else if (msg.has_clientrequest_message()) {
        handler_ClientRequest(req, msg.clientrequest_message());
    }
}

void Server::response_listener()
{
    loguru::set_thread_name("response listener");

    for (;;) {
        std::optional<std::string> res_opt = messenger.getNextResponse();
        if (res_opt) {
            RAFTmessage msg;
            msg.ParseFromString(*res_opt);
            response_handler(msg);
        }
    }
}

/* V2
 * dispatch RPC message to correct handler
 */
void Server::response_handler(const RAFTmessage &msg)
{
    {
        std::lock_guard<std::mutex> lock(m);
        if (msg.term() > current_term) {
            current_term = msg.term();
            server_state = FOLLOWER;
        }
        if (msg.term() == current_term) election_timer.start();
    }

    if (msg.has_requestvote_message()) {
        handler_RequestVote_response(msg.requestvote_message());
    } 
    else if (msg.has_appendentries_message()) {
        handler_AppendEntries_response(msg.appendentries_message());
    }
}

/** V1
 * Updates persistent state variables on disk, then routes RPCs to their 
 * correct handler.
 */
/*
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
*/

/**
 *  Execute the RAFT server rules for a leader:
 * - Sending heartbeats to followers
 * - Sending AppendEntries RPCs to followers (proj2)
 * - Updating _commit_index (proj2)
 */
/*
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
*/

/* v2
 * Process and reply to AppendEntries RPCs from leader.
 */
void Server::handler_AppendEntries(Messenger::Request &req, 
    const AppendEntries &ae)
{
    LOG_F(INFO, "S%d received AE req from S%d", server_no, ae.leader_no());

    // CASE: reject stale request
    if (ae.term() < current_term) {
        RAFTmessage response;
        AppendEntries *ae_response = new AppendEntries();
        ae_response->set_term(current_term);
        response.set_allocated_appendentries_message(ae_response);
        req.sendResponse(response.SerializeAsString());
    }

    last_observed_leader_no = ae.leader_no();
}

void Server::handler_AppendEntries_response(const AppendEntries &ae)
{
    LOG_F(INFO, "S%d received AE res from S%d", server_no, ae.follower_no());
}

/** v1
 * Process and respond to AppendEntries RPCs from leader.
 */
/*
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
*/

/* v2
 * Process and respond to RequestVote RPCs from candidates.
 */
void Server::handler_RequestVote(Messenger::Request &req, const RequestVote &rv)
{
    LOG_F(INFO, "S%d received RV req from S%d", server_no, rv.candidate_no());

    RAFTmessage response;
    RequestVote *rv_response = new RequestVote();
    rv_response->set_term(current_term);
    rv_response->set_vote_granted(false);
    if (!vote || vote->term_voted < current_term
              || vote->voted_for == rv.candidate_no()) {
        if (rv.term() >= current_term) {
            rv_response->set_vote_granted(true);
            vote = {current_term, rv.candidate_no()};
        }
    }
    response.set_allocated_requestvote_message(rv_response);
    req.sendResponse(response.SerializeAsString());
}

void Server::handler_RequestVote_response(const RequestVote &rv)
{
    if (server_state != CANDIDATE) return;
    if (rv.vote_granted()) votes_received.insert(rv.voter_no());
    // CASE: election won
    if (votes_received.size() > server_addrs.size() / 2) {
        server_state = LEADER;
        last_observed_leader_no = server_no;
        heartbeat_timer.start();
    }
}

/** v1
 * Process and reply to RequestVote RPCs from leader.
 */
/*
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
*/

/* v2
 * process client request
 */
void Server::handler_ClientRequest(Messenger::Request &req, const ClientRequest &cr)
{
    LOG_F(INFO, "S%d received CR: %s", server_no, cr.command().c_str());
}

/**
 * Append a command from the client to the local log (proj2), then send RPC
 * response with the result of executing the command.
 */
/*
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
*/

/**
 * Execute cmd string via bash and send response to client with the output.
 */
/*
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
*/

/* v2
 * send election shit
 */
void Server::start_election()
{
    LOG_F(INFO, "S%d starting election", server_no);

    server_state = CANDIDATE;
    ++current_term;
    votes_received = { server_no }; // vote for self
    election_timer.start();

    RAFTmessage msg;
    RequestVote *rv = new RequestVote();
    rv->set_term(current_term);
    rv->set_candidate_no(server_no);
    msg.set_allocated_requestvote_message(rv);
    broadcast_msg(msg);
}

/** v1
 * Trigger an election by sending RequestVote RPCs to all other servers.
 *  Return true if a new leader is elected, false otherwise.
 */
/*
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
*/

/* 
 * send heartbeats
 */
void Server::send_heartbeats()
{
    if (server_state != LEADER) return;
    heartbeat_timer.start();
    RAFTmessage heartbeat_msg;
    AppendEntries *ae_heartbeat = new AppendEntries();
    ae_heartbeat->set_leader_no(server_no);
    ae_heartbeat->set_term(current_term);
    heartbeat_msg.set_allocated_appendentries_message(ae_heartbeat);
    broadcast_msg(heartbeat_msg);
}

/**
 * Execute commands from log until _last_applied == _commit_index (proj2).
 */
/*
void Server::apply_log_entries() {}
*/

/* v2
 * broadcast
 */
void Server::broadcast_msg(const RAFTmessage &msg)
{
    std::string msg_str = msg.SerializeAsString();
    for (auto const &[peer_no, peer_addr] : server_addrs) {
        if (peer_no == server_no) continue;
        messenger.sendRequest(peer_addr, msg_str);
    }
}

/** v1
 * Send RPC to all other servers in the cluster.
 */
/*
void Server::send_RPC(const RPC &rpc) {
    for (int server_id : _cluster_list) {
        if (server_id == _server_id) continue;
        send_RPC(rpc, server_id);
    }
}
*/

/** v1
 * Send RPC to a specific server in the cluster.
 */
/*
void Server::send_RPC(const RPC &rpc, int server_id) {
    std::string rpc_str = rpc.SerializeAsString();
    _messenger.sendMessageToServer(server_id, rpc_str);
}
*/

/**
 * Returns an RPC if one has been received.
 */
/*
std::optional<RPC> Server::receive_RPC() {
    std::optional<std::string> msg_opt = _messenger.getNextMessage();
    if (!msg_opt) return std::nullopt;
    RPC rpc;
    rpc.ParseFromString(*msg_opt);
    return rpc;
}
*/
