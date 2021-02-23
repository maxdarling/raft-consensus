#include "Server.h"
#include <array>
#include <fstream>   // for ifstream, ofstream
#include <thread>

/* In milliseconds */
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2000;

/**
 * Construct server
 */
Server::Server(const int _server_no, const std::string cluster_file)
  : server_no(_server_no),
    // TODO(ali): additional error checking on these?
    server_addrs(parseClusterInfo(cluster_file)),
    messenger(parsePort(server_addrs[server_no])),
    election_timer(ELECTION_TIMEOUT_LOWER_BOUND, ELECTION_TIMEOUT_UPPER_BOUND, 
                   std::bind(&Server::start_election, this)),
    heartbeat_timer(HEARTBEAT_TIMEOUT, std::bind(&Server::send_heartbeats, this)) {}

/**
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

/**
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

/**
 * dispatch RPC message to correct handler
 */
void Server::request_handler(Messenger::Request &req)
{
    RAFTmessage msg;
    msg.ParseFromString(req.message);

    m.lock();
    if (msg.term() > current_term) {
        current_term = msg.term();
        server_state = FOLLOWER;
    }
    m.unlock();


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

/**
 * dispatch RPC message to correct handler
 */
void Server::response_handler(const RAFTmessage &msg)
{
    m.lock();
    if (msg.term() > current_term) {
        current_term = msg.term();
        server_state = FOLLOWER;
    }
    m.unlock();

    if (msg.has_requestvote_message()) {
        handler_RequestVote_response(msg.requestvote_message());
    } 
    else if (msg.has_appendentries_message()) {
        handler_AppendEntries_response(msg.appendentries_message());
    }
}

/**
 * Process and reply to AppendEntries RPCs from leader.
 */
void Server::handler_AppendEntries(Messenger::Request &req, 
    const AppendEntries &ae)
{
    // LOG_F(INFO, "S%d received AE req from S%d", server_no, ae.leader_no());

    RAFTmessage response;
    AppendEntries *ae_response = new AppendEntries();
    // TODO(ali): does this work and if so should i do this for all msgs?
    response.set_allocated_appendentries_message(ae_response);
    ae_response->set_follower_no(server_no);
    std::lock_guard<std::mutex> lock(m);
    ae_response->set_term(current_term);

    // CASE: reject stale request
    if (ae.term() < current_term) {    
        LOG_F(INFO, "S%d rejects stale AE from S%d", server_no, ae.leader_no());
        ae_response->set_success(false);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    // CASE: different leader been elected
    if (server_state == CANDIDATE) {
        LOG_F(INFO, "S%d reverting from candidate to follower", server_no);
        server_state = FOLLOWER;
    }
    last_observed_leader_no = ae.leader_no();
    election_timer.start();

    // CASE: heartbeat received, no log entries to process
    if (ae.log_entries_size() == 0) return;

    if (log[ae.prev_log_idx()].term != ae.prev_log_term()) {
        LOG_F(INFO, "S%d prev log entry doesn't match AE req from S%d", 
            server_no, ae.leader_no());
        ae_response->set_success(false);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    // append any new entries not already in log
    int new_entry_idx = ae.prev_log_idx() + 1;
    for (int i = 0; i < ae.log_entries_size(); i++, new_entry_idx++) {
        LogEntry new_entry = {ae.log_entries(i).command(), 
            ae.log_entries(i).term()};
        // CASE: an entry at this index already exists in log
        if (new_entry_idx < log.size()) {
            if (log[new_entry_idx].term != new_entry.term) {
                LOG_F(INFO, "S%d received conflicting log entry: "
                            "\"%s\" replaced with \"%s\"", 
                    server_no, 
                    log[new_entry_idx].command.c_str(), 
                    new_entry.command.c_str()
                );

                log.resize(new_entry_idx);
            }
            else {
                LOG_F(INFO, "S%d received entry it already has from S%d",
                    server_no, ae.leader_no());
                continue;
            }
        }

        LOG_F(INFO, "S%d replicating %s", server_no, new_entry.command.c_str());
        log.push_back(new_entry);
    }

    if (ae.leader_commit() > commit_index) {
        commit_index = MIN(ae.leader_commit(), new_entry_idx - 1);
    }

    ae_response->set_follower_next_idx(new_entry_idx);
    ae_response->set_success(true);
    req.sendResponse(response.SerializeAsString());
}

void Server::handler_AppendEntries_response(const AppendEntries &ae)
{
    std::lock_guard<std::mutex> lock(m);
    if (server_state != LEADER) return;
    if (ae.success()) {
        next_index[ae.follower_no() - 1] = ae.follower_next_idx();
        match_index[ae.follower_no() - 1] = ae.follower_next_idx() - 1;
    }
    else {
        next_index[ae.follower_no() - 1]--;
        replicate_log(ae.follower_no());
    }
}

/**
 * Process and respond to RequestVote RPCs from candidates.
 */
void Server::handler_RequestVote(Messenger::Request &req, const RequestVote &rv)
{
    RAFTmessage response;
    RequestVote *rv_response = new RequestVote();
    rv_response->set_voter_no(server_no);
    m.lock();
    rv_response->set_term(current_term);
    rv_response->set_vote_granted(false);
    if (vote.term_voted < current_term || vote.voted_for == rv.candidate_no()) {
        if (rv.term() >= current_term) {
            LOG_F(INFO, "S%d voting for S%d", server_no, rv.candidate_no());
            rv_response->set_vote_granted(true);
            vote = {current_term, rv.candidate_no()};
            election_timer.start();
        }
        else {
            LOG_F(INFO, "S%d not voting for S%d: stale request", 
                server_no, rv.candidate_no());
        }
    }
    else {
        LOG_F(INFO, "S%d not voting for S%d: already voted for S%d", 
            server_no, rv.candidate_no(), vote.voted_for);
    }
    m.unlock();
    response.set_allocated_requestvote_message(rv_response);
    req.sendResponse(response.SerializeAsString());
}

void Server::handler_RequestVote_response(const RequestVote &rv)
{
    std::lock_guard<std::mutex> lock(m);
    if (server_state != CANDIDATE) return;
    if (rv.vote_granted()) votes_received.insert(rv.voter_no());

    // CASE: election won
    if (votes_received.size() > server_addrs.size() / 2) {
        LOG_F(INFO, "S%d won election for term %d", server_no, current_term);
        server_state = LEADER;
        last_observed_leader_no = server_no;
        election_timer.stop();
        send_heartbeats();
        heartbeat_timer.start();
        next_index.assign(server_addrs.size(), log.size());
        match_index.assign(server_addrs.size(), 0);
    }
}

/**
 * process client request
 */
void Server::handler_ClientRequest(Messenger::Request &req, const ClientRequest &cr)
{
    std::lock_guard<std::mutex> lock(m);
    if (server_state != LEADER) {
        LOG_F(INFO, "S%d re-routing CR to S%d", 
            server_no, last_observed_leader_no);
        RAFTmessage msg;
        ClientRequest *cr_response = new ClientRequest();
        cr_response->set_success(false);
        cr_response->set_leader_no(last_observed_leader_no);
        msg.set_allocated_clientrequest_message(cr_response);
        req.sendResponse(msg.SerializeAsString());
        return;
    }

    LOG_F(INFO, "S%d logging CR: %s", server_no, cr.command().c_str());
    
    log.push_back({cr.command(), current_term});
    pending_client_requests.push(req);

    // Send new entry to followers
    for (int peer_no = 1; peer_no <= server_addrs.size(); peer_no++) {
        replicate_log(peer_no);
    }
}

void Server::replicate_log(int peer_no) {
    int peer_idx = peer_no - 1;
    std::lock_guard<std::mutex> lock(m);
    if (peer_no == server_no || log.size() <= next_index[peer_idx]) return;

    RAFTmessage msg;
    AppendEntries* ae = new AppendEntries();
    ae->set_leader_no(server_no);
    ae->set_term(current_term);
    ae->set_prev_log_idx(next_index[peer_idx] - 1);
    ae->set_prev_log_term(log[next_index[peer_idx] - 1].term);
    ae->set_leader_commit(commit_index);

    for (int new_entry_idx = next_index[peer_idx]; 
            new_entry_idx < log.size();
            new_entry_idx++) {
        AppendEntries::LogEntry *entry = ae->add_log_entries();
        entry->set_command(log[new_entry_idx].command);
        entry->set_term(log[new_entry_idx].term);
    }

    msg.set_allocated_appendentries_message(ae);
    messenger.sendRequest(server_addrs[peer_no], msg.SerializeAsString());
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

/**
 * send election
 */
void Server::start_election()
{
    LOG_F(INFO, "S%d starting election", server_no);

    std::lock_guard<std::mutex> lock(m);
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

/**
 * send heartbeats
 */
void Server::send_heartbeats()
{
    LOG_F(INFO, "S%d sending heartbeats", server_no);
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

/**
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
