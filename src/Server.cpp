#include "Server.h"
#include <array>
#include <thread>

/* In milliseconds. */
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2000;

using std::optional, std::string, std::lock_guard, std::mutex;

/*****************************************************************************
 *                              PUBLIC INTERFACE                             *
 *****************************************************************************/

/**
 * Construct a RAFT server instance that will receive connections at the net
 * address located in `cluster_file` at line number `server_no`. If this server
 * is `restarting` following a crash, its state and log will be recovered from
 * disk. If a recovery file is missing or corrupted, an error will be thrown.
 * If the cluster file is missing or corrupted, an error will be thrown.
 */
Server::Server(const int _server_no, const string cluster_file, 
               StateMachine *_state_machine, bool restarting)
  : state_machine(_state_machine), 
    log_file("log" + std::to_string(_server_no)),
    log(log_file, 
        /* serialize LogEntry to string */
        [](const LogEntry &entry) {
            return std::to_string(entry.term) + " " + entry.command;
        },
        /* deserialize LogEntry from string */
        [](string entry_str) {
            size_t delimiter_idx = entry_str.find(" ");
            int term = std::stoi(entry_str.substr(0, delimiter_idx));
            string command = entry_str.substr(delimiter_idx + 1);
            return LogEntry {command, term};
        }
    ),
    server_no(_server_no),
    server_addrs(parseClusterInfo(cluster_file)),
    recovery_file("server" + std::to_string(server_no) + "_state"),
    persistent_storage(recovery_file),
    messenger(parsePort(server_addrs[server_no])),
    election_timer(ELECTION_TIMEOUT_LOWER_BOUND, ELECTION_TIMEOUT_UPPER_BOUND, 
                   std::bind(&Server::start_election, this)),
    heartbeat_timer(HEARTBEAT_TIMEOUT, 
                    std::bind(&Server::send_heartbeats, this)) 
{
    if (restarting) {
        // recover persistent state
        LOG_F(INFO, "S%d recovering state from file", server_no);
        persistent_storage.recover();
        ServerPersistentState &sps = persistent_storage.state();
        current_term = sps.current_term();
        if (sps.voted_for() != 0) vote = {sps.term_voted(), sps.voted_for()};
        last_applied = sps.last_applied();
        LOG_F(INFO, "term: %d | vote term: %d | "
                    "voted for: S%d | last applied: %d", 
            current_term, vote.term_voted, vote.voted_for, last_applied);
        // recover log
        log.recover(last_applied);
    }

    // If server is starting from scratch, delete any log artifacts.
    else log.clear();
}

/**
 * Start the server, so that it may respond to requests from clients and other
 * servers.
 */
void Server::run()
{
    LOG_F(INFO, "S%d now running @ %s", 
        server_no, server_addrs[server_no].c_str());
    election_timer.start();

    // Listen for RPC requests sent to this instance.
    std::thread([this] {
        loguru::set_thread_name("request listener");
        for (;;) {
            Messenger::Request req = messenger.getNextRequest().value();
            RAFTmessage msg;
            msg.ParseFromString(req.message);
            handler_RAFTmessage(msg, std::move(req));
        }
    }).detach();

    // Listen for RPC responses returned to this instance.
    std::thread([this] {
        loguru::set_thread_name("response listener");
        for (;;) {
            RAFTmessage msg;
            msg.ParseFromString(messenger.getNextResponse().value());
            handler_RAFTmessage(msg);
        }
    }).detach();

    std::thread(&Server::apply_log_entries_task, this).join();
}

/*****************************************************************************
 *                             PRIVATE INTERFACE                             *
 *****************************************************************************/

/**
 * Process the term of the incoming RPC message, then route to its appropriate 
 * handler.
 */
void Server::handler_RAFTmessage(const RAFTmessage &msg, 
    std::optional<Messenger::Request> req) 
{
    m.lock();
    if (msg.term() > current_term) {
        server_state = FOLLOWER;
        current_term = msg.term();
        persistent_storage.state().set_current_term(current_term);
        persistent_storage.save();
        heartbeat_timer.stop();
    }
    m.unlock();

    /* RPC REQUEST RECEIVED */
    if (req) {
        if (msg.has_requestvote_message())
            handler_RequestVote(*req, msg.requestvote_message());

        else if (msg.has_appendentries_message())
            handler_AppendEntries(*req, msg.appendentries_message());

        else if (msg.has_clientrequest_message())
            handler_ClientRequest(*req, msg.clientrequest_message());
    }

    /* RPC RESPONSE RECEIVED */
    else if (msg.has_requestvote_message())
        handler_RequestVote_response(msg.requestvote_message());

    else if (msg.has_appendentries_message())
        handler_AppendEntries_response(msg.appendentries_message());
}

/*****************************************************************************
 *                           RPC REQUEST HANDLERS                            *
 *****************************************************************************/

/**
 * Process and respond to RequestVote RPCs from candidates.
 */
void Server::handler_RequestVote(Messenger::Request &req, const RequestVote &rv)
{
    RAFTmessage response;
    RequestVote *rv_response = new RequestVote();
    response.set_allocated_requestvote_message(rv_response);

    m.lock();
    response.set_term(current_term);
    rv_response->set_voter_no(server_no);
    rv_response->set_term(current_term);
    rv_response->set_vote_granted(false);
    if (vote.term_voted < current_term || vote.voted_for == rv.candidate_no()) {
        if (rv.term() >= current_term) {
            // Election restriction described in Section 5.4.1 of the Raft paper
            if (log.empty() || 
                rv.last_log_term() > log[log.size()].term || 
                (rv.last_log_term() == log[log.size()].term && 
                 rv.last_log_idx() >= log.size())) {
                LOG_F(INFO, "S%d voting for S%d", server_no, rv.candidate_no());
                election_timer.start();
                rv_response->set_vote_granted(true);
                vote = {current_term, rv.candidate_no()};
                persistent_storage.state().set_term_voted(current_term);
                persistent_storage.state().set_voted_for(rv.candidate_no());
                persistent_storage.save();
            }
            else {
                LOG_F(INFO, "S%d not voting for S%d: log out-of-date", 
                    server_no, rv.candidate_no());            
            }
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

    req.sendResponse(response.SerializeAsString());
}

/**
 * Process and reply to AppendEntries RPCs from leader.
 */
void Server::handler_AppendEntries(Messenger::Request &req, 
    const AppendEntries &ae)
{
    RAFTmessage response;
    AppendEntries *ae_response = new AppendEntries();
    response.set_allocated_appendentries_message(ae_response);

    lock_guard<mutex> lock(m);
    response.set_term(current_term);
    ae_response->set_follower_no(server_no);
    ae_response->set_term(current_term);

    // CASE: reject stale request
    if (ae.term() < current_term) {
        ae_response->set_success(false);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    // CASE: different leader been elected
    if (server_state == CANDIDATE) server_state = FOLLOWER;
    last_observed_leader_no = ae.leader_no();
    election_timer.start();

    // CASE: heartbeat received, no log entries to process
    if (ae.log_entries_size() == 0) return;

    if (ae.prev_log_idx() > 0 && 
        log[ae.prev_log_idx()].term != ae.prev_log_term()) {
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
        if (new_entry_idx <= log.size()) {
            if (log[new_entry_idx].term != new_entry.term) {
                LOG_F(INFO, "S%d received conflicting log entry: "
                            "\"%s\" replaced with \"%s\"", 
                    server_no, 
                    log[new_entry_idx].command.c_str(), 
                    new_entry.command.c_str()
                );

                log.trunc(new_entry_idx - 1);
            }
            else continue;
        }

        LOG_F(INFO, "S%d replicating cmd: %s", server_no, 
            new_entry.command.c_str());
        log.append(new_entry);
    }

    if (ae.leader_commit() > commit_index) {
        commit_index = MIN(ae.leader_commit(), new_entry_idx - 1);
        new_commits_cv.notify_one();
    }

    ae_response->set_follower_next_idx(new_entry_idx);
    ae_response->set_success(true);
    req.sendResponse(response.SerializeAsString());
}

/**
 * If not leader, re-route client request to leader.
 * If leader, add client request to queue and send AppendEntries RPCs to peers.
 */
void Server::handler_ClientRequest(Messenger::Request &req, 
    const ClientRequest &cr)
{
    RAFTmessage response;
    ClientRequest *cr_response = new ClientRequest();
    response.set_allocated_clientrequest_message(cr_response);

    lock_guard<mutex> lock(m);
    if (server_state != LEADER) {
        LOG_F(INFO, "S%d re-routing CR to S%d", 
            server_no, last_observed_leader_no);
        cr_response->set_success(false);
        cr_response->set_leader_no(last_observed_leader_no);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    LOG_F(INFO, "S%d logging CR: %s", server_no, cr.command().c_str());
    log.append({cr.command(), current_term});
    pending_requests.push(
        {std::move(req), log.size(), current_term}
    );

    // Send new entry to followers
    for (int peer_no = 1; peer_no <= server_addrs.size(); peer_no++)
        replicate_log(peer_no);
}

/*****************************************************************************
 *                           RPC RESPONSE HANDLERS                           *
 *****************************************************************************/

/**
 * If candidate, process response to vote request.
 */
void Server::handler_RequestVote_response(const RequestVote &rv)
{
    lock_guard<mutex> lock(m);
    if (server_state != CANDIDATE) return;
    if (rv.vote_granted()) votes_received.insert(rv.voter_no());

    // CASE: election won
    if (votes_received.size() > server_addrs.size() / 2) {
        LOG_F(INFO, "S%d won election for term %d", server_no, current_term);
        server_state = LEADER;
        last_observed_leader_no = server_no;
        election_timer.stop();
        heartbeat_timer.start();
        send_heartbeats();
        next_index.assign(server_addrs.size(), log.size() + 1);
        match_index.assign(server_addrs.size(), 0);
    }
}

/**
 * If leader, process response to our AppendEntries RPC request.
 */
void Server::handler_AppendEntries_response(const AppendEntries &ae)
{
    lock_guard<mutex> lock(m);
    if (server_state != LEADER) return;
    if (ae.success()) {
        next_index[ae.follower_no() - 1] = ae.follower_next_idx();
        match_index[ae.follower_no() - 1] = ae.follower_next_idx() - 1;

        LOG_F(INFO, "match index of S%d noted as %d", 
            ae.follower_no(), ae.follower_next_idx() - 1);

        // update our own match index
        match_index[server_no - 1] = log.size();
        // potentially update commit index
        auto mi_copy(match_index);
        std::sort(mi_copy.begin(), mi_copy.end());
        int greatest_committed_idx = mi_copy[mi_copy.size() / 2];
        if (greatest_committed_idx > commit_index &&
            log[greatest_committed_idx].term == current_term) {
            commit_index = greatest_committed_idx;
            new_commits_cv.notify_one();        
        }
    }
    else {
        next_index[ae.follower_no() - 1]--;
        replicate_log(ae.follower_no());
    }
}

/*****************************************************************************
 *                              TIMED CALLBACKS                              *
 *****************************************************************************/

/**
 * Convert to candidate and send RequestVote RPCs to peers.
 */
void Server::start_election()
{
    LOG_F(INFO, "S%d starting election", server_no);

    lock_guard<mutex> lock(m);
    server_state = CANDIDATE;
    current_term++;
    persistent_storage.state().set_current_term(current_term);
    persistent_storage.save();
    votes_received = {server_no}; // vote for self
    vote = {current_term, server_no};
    election_timer.start();

    RAFTmessage msg;
    RequestVote *rv = new RequestVote();
    msg.set_allocated_requestvote_message(rv);
    msg.set_term(current_term);
    rv->set_term(current_term);
    rv->set_candidate_no(server_no);
    rv->set_last_log_idx(log.size());
    if (!log.empty()) rv->set_last_log_term(log[log.size()].term);
    broadcast_msg(msg);
}

/**
 * If leader, send empty AppendEntries RPC requests to peers.
 */
void Server::send_heartbeats()
{
    if (server_state != LEADER) return;
    heartbeat_timer.start();
    RAFTmessage heartbeat_msg;
    AppendEntries *ae_heartbeat = new AppendEntries();
    heartbeat_msg.set_allocated_appendentries_message(ae_heartbeat);
    heartbeat_msg.set_term(current_term);
    ae_heartbeat->set_term(current_term);
    ae_heartbeat->set_leader_no(server_no);
    broadcast_msg(heartbeat_msg);
}

/*****************************************************************************
 *                        LOG ENTRY APPLICATION TASK                         *
 *****************************************************************************/

/** 
 * This thread routine sleeps until notified that log entries are ready to be 
 * applied (i.e. commit_index > last_applied). Log entries are applied without
 * blocking. If this instance is the leader, the command output is returned to
 * the client, or an error message.
 */
void Server::apply_log_entries_task()
{
    loguru::set_thread_name("apply log entries task");
    for (;;) {
        string cmd;

        {
            std::unique_lock<mutex> lock(m);
            new_commits_cv.wait(lock, [this] { 
                return commit_index > last_applied; 
            });
            cmd = log[++last_applied].command;
        }

        LOG_F(INFO, "S%d applying cmd: %s", server_no, cmd.c_str());
        string result = state_machine->apply(cmd);

        m.lock();
        if (server_state == LEADER) {
            // Copious error checking here. The following errors have never
            // materialized while testing, but maybe one day they will.
            while (!pending_requests.empty() && 
                   pending_requests.front().log_idx < last_applied) {
                LOG_F(ERROR, "S%d has lost client request @ idx %d",
                    server_no, pending_requests.front().log_idx);
                pending_requests.pop();
            }

            if (!pending_requests.empty() && 
                pending_requests.front().log_idx == last_applied) {
                PendingRequest &client_req = pending_requests.front();
                if (client_req.term != log[last_applied].term) {
                    LOG_F(ERROR, "client request term doesn't match associated "
                                 "log term");
                }
                else {
                    LOG_F(INFO, "S%d sending result of cmd", server_no);
                    RAFTmessage response;
                    ClientRequest *cr = new ClientRequest();
                    response.set_allocated_clientrequest_message(cr);
                    cr->set_success(true);
                    cr->set_leader_no(server_no);
                    cr->set_output(result);
                    client_req.req.sendResponse(response.SerializeAsString());
                    pending_requests.pop();
                }
            }
        }
        m.unlock();
        persistent_storage.state().set_last_applied(last_applied);
        persistent_storage.save();
    }
}

/*****************************************************************************
 *                             HELPER FUNCTIONS                              *
 *****************************************************************************/

/**
 * Send AppendEntries RPC request to specified peer with as many log entries
 * as possible. Should be called while holding a lock on server state.
 */
void Server::replicate_log(int peer_no) {
    int peer_idx = peer_no - 1;
    if (peer_no == server_no || log.size() < next_index[peer_idx]) return;

    RAFTmessage msg;
    AppendEntries* ae = new AppendEntries();
    msg.set_allocated_appendentries_message(ae);
    msg.set_term(current_term);
    ae->set_leader_no(server_no);
    ae->set_term(current_term);
    ae->set_leader_commit(commit_index);
    ae->set_prev_log_idx(next_index[peer_idx] - 1);
    if (next_index[peer_idx] - 1 > 0) {
        ae->set_prev_log_term(log[next_index[peer_idx] - 1].term);
    }

    // add log entries to request
    for (int new_entry_idx = next_index[peer_idx]; 
            new_entry_idx <= log.size();
            new_entry_idx++) {
        AppendEntries::LogEntry *entry = ae->add_log_entries();
        entry->set_command(log[new_entry_idx].command);
        entry->set_term(log[new_entry_idx].term);
    }

    messenger.sendRequest(server_addrs[peer_no], msg.SerializeAsString());
}

/**
 * Broadcast the given RAFTmessage to all peers.
 */
void Server::broadcast_msg(const RAFTmessage &msg)
{
    string msg_str = msg.SerializeAsString();
    for (auto const &[peer_no, peer_addr] : server_addrs) {
        if (peer_no == server_no) continue;
        messenger.sendRequest(peer_addr, msg_str);
    }
}
