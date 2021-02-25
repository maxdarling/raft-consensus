#include "Server.h"
#include <array>
#include <thread>

/* In milliseconds */
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2000;

using std::optional, std::string, std::lock_guard, std::mutex;

/**
 * Construct server
 */
Server::Server(const int _server_no, const string cluster_file, bool restarting)
  : server_no(_server_no),
    // TODO(ali): additional error checking on these?
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
        if (!persistent_storage.recover()) {
            string err_msg = "S" + std::to_string(server_no) 
                + " could not open recovery file";
            LOG_F(ERROR, "%s", err_msg.c_str());
            throw std::runtime_error(err_msg);
        }

        LOG_F(INFO, "S%d recovering state from file", server_no);
        ServerPersistentState &sps = persistent_storage.state();
        current_term = sps.current_term();
        if (sps.voted_for() != 0) vote = {sps.term_voted(), sps.voted_for()};
        last_applied = sps.last_applied();
        LOG_F(INFO, "term: %d | vote term: %d | "
                    "voted for: S%d | last applied: %d", 
            current_term, vote.term_voted, vote.voted_for, last_applied);
    }
}

/**
 * run server
 */
void Server::run()
{
    LOG_F(INFO, "S%d now running @ %s", 
        server_no, server_addrs[server_no].c_str());
    election_timer.start();

    std::thread([this] {
        loguru::set_thread_name("request listener");

        for (;;) {
            optional<Messenger::Request> req_opt = messenger.getNextRequest();
            if (req_opt) request_handler(*req_opt);
        }
    }).detach();

    std::thread([this] {
        loguru::set_thread_name("response listener");

        for (;;) {
            optional<string> res_opt = messenger.getNextResponse();
            if (res_opt) {
                RAFTmessage msg;
                msg.ParseFromString(*res_opt);
                response_handler(msg);
            }
        }
    }).detach();

    std::thread(&Server::apply_log_entries_task, this).join();
}

/**
 * dispatch RPC message to correct handler
 */
void Server::request_handler(Messenger::Request &req)
{
    RAFTmessage msg;
    msg.ParseFromString(req.message);
    process_RAFTmessage(msg);

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

/**
 * dispatch RPC message to correct handler
 */
void Server::response_handler(const RAFTmessage &msg)
{
    process_RAFTmessage(msg);

    if (msg.has_requestvote_message()) {
        handler_RequestVote_response(msg.requestvote_message());
    } 
    else if (msg.has_appendentries_message()) {
        handler_AppendEntries_response(msg.appendentries_message());
    }
}

void Server::process_RAFTmessage(const RAFTmessage &msg)
{
    lock_guard<mutex> lock(m);
    if (msg.term() > current_term) {
        current_term = msg.term();
        persistent_storage.state().set_current_term(current_term);
        persistent_storage.save();
        server_state = FOLLOWER;
        heartbeat_timer.stop();
    } 
}

/**
 * Process and reply to AppendEntries RPCs from leader.
 */
void Server::handler_AppendEntries(Messenger::Request &req, 
    const AppendEntries &ae)
{
    RAFTmessage response;
    AppendEntries *ae_response = new AppendEntries();
    // TODO(ali): does this work and if so should i do this for all msgs?
    response.set_allocated_appendentries_message(ae_response);
    ae_response->set_follower_no(server_no);
    lock_guard<mutex> lock(m);
    response.set_term(current_term);
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

        LOG_F(INFO, "S%d replicating cmd: %s", server_no, 
            new_entry.command.c_str());
        log.push_back(new_entry);
    }

    if (ae.leader_commit() > commit_index) {
        commit_index = MIN(ae.leader_commit(), new_entry_idx - 1);
        new_commits_cv.notify_one();
    }

    ae_response->set_follower_next_idx(new_entry_idx);
    ae_response->set_success(true);
    req.sendResponse(response.SerializeAsString());
}

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
        match_index[server_no - 1] = log.size() - 1;
        // potentially update commit index
        auto mi_copy(match_index);
        std::sort(mi_copy.begin(), mi_copy.end());
        int greatest_committed_idx = mi_copy[mi_copy.size() / 2];
        if (greatest_committed_idx > commit_index &&
            log[greatest_committed_idx].term == current_term) {
            // LOG_F(INFO, "UPDATING COMMIT IDX");
            commit_index = greatest_committed_idx;
            new_commits_cv.notify_one();        
        }
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
    response.set_term(current_term);
    rv_response->set_term(current_term);
    rv_response->set_vote_granted(false);
    if (vote.term_voted < current_term || vote.voted_for == rv.candidate_no()) {
        if (rv.term() >= current_term) {
            LOG_F(INFO, "S%d voting for S%d", server_no, rv.candidate_no());
            election_timer.start();
            rv_response->set_vote_granted(true);
            vote = {current_term, rv.candidate_no()};
            persistent_storage.state().set_term_voted(current_term);
            persistent_storage.state().set_voted_for(rv.candidate_no());
            persistent_storage.save();
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
    lock_guard<mutex> lock(m);
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
    
    log.push_back({cr.command(), current_term});
    pending_requests.push(
        {std::move(req), static_cast<int>(log.size() - 1), current_term}
    );

    // Send new entry to followers
    for (int peer_no = 1; peer_no <= server_addrs.size(); peer_no++) {
        replicate_log(peer_no);
    }
}

void Server::replicate_log(int peer_no) {
    int peer_idx = peer_no - 1;
    if (peer_no == server_no || log.size() <= next_index[peer_idx]) return;

    RAFTmessage msg;
    AppendEntries* ae = new AppendEntries();
    ae->set_leader_no(server_no);
    msg.set_term(current_term);
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
 * send election
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
    msg.set_term(current_term);
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
    if (server_state != LEADER) return;
    heartbeat_timer.start();
    RAFTmessage heartbeat_msg;
    AppendEntries *ae_heartbeat = new AppendEntries();
    ae_heartbeat->set_leader_no(server_no);
    heartbeat_msg.set_term(current_term);
    ae_heartbeat->set_term(current_term);
    heartbeat_msg.set_allocated_appendentries_message(ae_heartbeat);
    broadcast_msg(heartbeat_msg);
}

/**
 * broadcast
 */
void Server::broadcast_msg(const RAFTmessage &msg)
{
    string msg_str = msg.SerializeAsString();
    for (auto const &[peer_no, peer_addr] : server_addrs) {
        if (peer_no == server_no) continue;
        messenger.sendRequest(peer_addr, msg_str);
    }
}

/* */
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
        string bash_cmd = "bash -c \"" + cmd + "\"";
        std::unique_ptr<FILE, decltype(&pclose)> pipe(
            popen(bash_cmd.c_str(), "r"), pclose
        );
        string result;
        std::array<char, 128> buf;
        if (!pipe) {
            LOG_F(ERROR, "S%d: popen() failed!", server_no);
            result = "ERROR: popen() failed";
        }
        else {
            while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr) {
                result += buf.data();
            }
        }

        m.lock();
        if (server_state == LEADER) {
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
