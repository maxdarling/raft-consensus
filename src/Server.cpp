#include "Server.h"
#include "RaftRPC.pb.h"
#include <array>
#include <filesystem>
#include <string>
#include <thread>
#include <fstream>
#include <unistd.h>

/* In milliseconds. */
const int ELECTION_TIMEOUT_LOWER_BOUND = 5000;
const int ELECTION_TIMEOUT_UPPER_BOUND = 10000;
const int HEARTBEAT_TIMEOUT = 2000;

/* The maximum allowed size of the physical log. */
const int MAX_LOG_SIZE = 2;

/* The size to which snapshots sent via InstallSnapshot should be segemented */
const int CHUNK_SIZE = 2;

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
    log(new Log<RaftLog::LogEntry>(log_file, 
        /* serialize LogEntry to string */
        [](const RaftLog::LogEntry &entry) {
            return std::to_string(entry.term) + " " + entry.command;
        },
        /* deserialize LogEntry from string */
        [](string entry_str) {
            size_t delimiter_idx = entry_str.find(" ");
            int term = std::stoi(entry_str.substr(0, delimiter_idx));
            string command = entry_str.substr(delimiter_idx + 1);
            return RaftLog::LogEntry {command, term};
        }
    ),persistent_storage),
    server_no(_server_no),
    server_addrs(parseClusterInfo(cluster_file)),
    recovery_file("server" + std::to_string(server_no) + "_state"),
    snapshot_filename_options(
        {"server_" + std::to_string(server_no) + "_snapshot_a",
        "server_" + std::to_string(server_no) + "_snapshot_b"}),
    partially_installed_snapshot_filename(
        "server_" + std::to_string(server_no) + "_partial_snapshot"),
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

        // new (snapshotting):
        if (!state_machine->importState(sps.snapshot_filename())) {
            throw "Server::Server(): could not recover state machine: " +
            string(strerror(errno));
        }
        /* note: while formerly we could reset to 0, it's now necessary to
         * recover last_applied. can't get away with setting it to zero or we'll
         * have out of bounds errors in the snapshot. */
        last_applied = sps.last_included_index(); 
        if (persistent_storage.state().can_safely_recover_log()) {
            log.recover();
        } else {
            log.clear();
        }
        LOG_F(INFO, "Recovery successsful!");
        LOG_F(INFO, "term: %d | vote term: %d | "
                    "voted for: S%d | last applied: %d", 
            current_term, vote.term_voted, vote.voted_for, last_applied);
    }

    // If server is starting from scratch, delete any log artifacts.
    else {
        log.clear();
        persistent_storage.state().set_can_safely_recover_log(true);
    }
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

        else if (msg.has_installsnapshot_message())
            handler_InstallSnapshot(*req, msg.installsnapshot_message());

        else if (msg.has_clientrequest_message())
            handler_ClientRequest(*req, msg.clientrequest_message());
    }

    /* RPC RESPONSE RECEIVED */
    else if (msg.has_requestvote_message())
        handler_RequestVote_response(msg.requestvote_message());

    else if (msg.has_appendentries_message())
        handler_AppendEntries_response(msg.appendentries_message());

    else if (msg.has_installsnapshot_message()) 
        handler_InstallSnapshot_response(msg.installsnapshot_message());
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
        LOG_F(INFO, "follower: received req. has stale term\n"
                     "mine: %d, leader's: %d", current_term, ae.term());
        ae_response->set_success(false);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    // CASE: different leader been elected
    if (server_state == CANDIDATE) server_state = FOLLOWER;
    last_observed_leader_no = ae.leader_no();
    election_timer.start();

    /* bug (perhaps not covered by raft): it's possible for ae.prev_log_idx() to
    be out of range (too large) for 'log'. This happens when a server joins
    late when the leader's log has some entries.
    
    To replicate the bug, start with 2 servers, append at least 1 log entry,
    force a re-election (this updates and increases next_index, which
    'prev_log_idx' derives from), join a new server, and append another log
    entry. */

    /* assumption for snapshotting project: no need to worry about this, as 
       the fix for this bug isn't reltaed to snapshotting */

    /* new (snapshotting): follower can also dip back into it's log. 
        -if the follower is trying to index at physical 0, it's correct protocol
        to use last_included_term
        - it can't be earlier than that (last_included_index), because that's 
        applied, so by the "State Machine Safety" property (fig 3), a valid 
        leader would have that entry, and invalid leaders are ruled out above
    */
    bool fine_by_last_incl_term = 
        ae.prev_log_idx() == persistent_storage.state().last_included_index() && 
        ae.prev_log_term() == persistent_storage.state().last_included_term();

    /* impossible for it to be lower by the comment above */
    assert(ae.prev_log_idx() >= persistent_storage.state().last_included_index());

    if (!fine_by_last_incl_term && ae.prev_log_idx() > 0 && 
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
        RaftLog::LogEntry new_entry = {ae.log_entries(i).command(), 
            ae.log_entries(i).term()};
        // CASE: an entry at this index already exists in log
        if (new_entry_idx <= log.size()) {
            if (log[new_entry_idx].term != new_entry.term) {
                LOG_F(INFO, "S%d received conflicting log entry: "
                            "\"%s\" replaced with \"%s\"", 
                    server_no, 
                    /* almost seems like a snapshotting bug, since .commanand
                       isn't defined for last_included_index, but in that case
                       we never get here because the terms must agree */
                    log[new_entry_idx].command.c_str(),
                    new_entry.command.c_str()
                );

                log.trunc(new_entry_idx - 1);
            }
            else continue;
        }

        LOG_F(INFO, "S%d replicating cmd: %s", server_no, 
            new_entry.command.c_str());
        // new (snapshotting): snapshot guard on appends
        if (log.physical_size() >= MAX_LOG_SIZE) {
            write_snapshot();
        }
        log.append(new_entry);
    }

    if (ae.leader_commit() > commit_index) {
        commit_index = MIN(ae.leader_commit(), new_entry_idx - 1);
        new_commits_cv.notify_one();
    }

    ae_response->set_follower_next_idx(new_entry_idx);
    ae_response->set_success(true);
    req.sendResponse(response.SerializeAsString());
    LOG_F(INFO, "follower: sent next index of %d to leader", new_entry_idx);
}


/**
 *
 */
void Server::handler_InstallSnapshot(Messenger::Request &req, 
                             const InstallSnapshot& is) {
    LOG_F(INFO, "Called InstallSnapshot request handler");
    /* 
    implementation notes: 
    -we always restart on offset 0 because it indicates a newer snapshot, 
    whether the same or different leader as the current snapshot
    */

    RAFTmessage response;
    InstallSnapshot *is_response = new InstallSnapshot();
    response.set_allocated_installsnapshot_message(is_response);

    lock_guard<mutex> lock(m);
    response.set_term(current_term);
    is_response->set_follower_no(server_no);
    is_response->set_term(current_term);
    is_response->set_done(is.done());

    if (is.term() < current_term) {
        LOG_F(INFO, "handler_InstallSnapshot: follower: received req. has stale"
                    "term\n" "mine: %d, leader's: %d", current_term, is.term());
        is_response->set_success(false);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    // my deviation: reject if the snapshot is a prefix of the log
    if (is.last_included_index() <= log.size()) {
        is_response->set_success(false);
        req.sendResponse(response.SerializeAsString());
        return;
    }    

    /* handle checkup messages. the leader sends us the offset of the most 
       recent chunk it sent us. if we haven't gotten that chunk yet, something
       went went wrong, so reset. if we already got that chunk and are on a new
       one, something also went wrong. note: both of these could be handled a 
       little more intricately, but failing and retrying is simplest. 
       
       note: we're allowed to reason this way because of the TCP guarantee 
       that messages are delivered in order sent, AND same with Messenger
       (messages returned are in order of received) */
    if (is.is_checkup()) {
        bool success = (partially_installed_snapshot_offset == is.offset());
        LOG_F(INFO, "handler_InstallSnapshot: received checkup message.\n"
        "my ofs: %d, leader ofs: %d, success = %d", 
        partially_installed_snapshot_offset, is.offset(), success);
        is_response->set_success(success);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    /* now we have a snapshot w/ chunk. bad cases: 
       -we don't get the correct # snapshot. assuming correct TCP order, this
       should only be possible if we crash midway through an install. */
    if (is.offset() != 0 && partially_installed_snapshot_offset == -1) {
        is_response->set_success(false);
        req.sendResponse(response.SerializeAsString());
        return;
    }

    // create a new file if it's the first chunk
    int ofs_flags = std::ios::binary | 
                    (is.offset() == 0 ? std::ios::trunc : std::ios::app);
    std::ofstream ofs(partially_installed_snapshot_filename, ofs_flags);
    if (!ofs) {
        throw "handler_InstallSnapshot(): " + string(strerror(errno));
    }
    ofs << is.data();
    ofs.close();
    // update for future checkups
    partially_installed_snapshot_offset = is.offset();

    // if we're done, do finalizing stuff
    if (is.done()) {
        // discard the entire log
        log.clear();
        // note: must clear log first, or else can crash -> become inconsistent. 

        string current_filename = persistent_storage.state().snapshot_filename(); 
        string new_filename = (current_filename == snapshot_filename_options[0] 
                               ? snapshot_filename_options[1] 
                               : snapshot_filename_options[0]);

        std::filesystem::path p = std::filesystem::current_path();
        std::filesystem::rename(p/partially_installed_snapshot_filename, 
                                p/new_filename);
        persistent_storage.state().set_snapshot_filename(new_filename); 
        persistent_storage.state().set_last_included_index(is.last_included_index());
        persistent_storage.state().set_last_included_term(is.last_included_term());
        persistent_storage.save();
        last_applied = is.last_included_index();
        commit_index = last_applied;
        
        if (current_filename != "") {
            std::filesystem::remove(p/current_filename);
        }

        // apply the changes to the state machine (import)
        if (!state_machine->importState(new_filename)) {
            throw "handler_InstallSnapshot: importState failed: " 
                  + string(strerror(errno));
        }

        // mark that we're done with install
        partially_installed_snapshot_offset = -1;
        LOG_F(INFO, "handler_InstallSnapshot: done with Install!");
    } 

    is_response->set_success(true);
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
    if (log.physical_size() >= MAX_LOG_SIZE) {
        write_snapshot();
    }
    log.append({cr.command(), current_term});
    pending_requests.push(
        {std::move(req), log.size(), current_term}
    );

    // Send new entry to followers
    for (int peer_no = 1; peer_no <= server_addrs.size(); peer_no++) {
        replicate_log(peer_no);
    }
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
        next_index.assign(server_addrs.size(), log.size() + 1);
        match_index.assign(server_addrs.size(), 0);
        last_sent_snapshot_offset.assign(server_addrs.size(), -1);
        send_heartbeats();
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
        LOG_F(INFO, "append entires failed for follower #%d", ae.follower_no());
        next_index[ae.follower_no() - 1]--;
        replicate_log(ae.follower_no());
    }
}


/**
* If leader, continue sending snapshots to follower. 
*/
void Server::handler_InstallSnapshot_response(const InstallSnapshot& is) {
    /** 
     * Implementation notes: 
     * - philosophy: happily fail and retry if something's off. no intricate 
     *   handling. 
     */

    LOG_F(INFO, "Called InstallSnapshot response handler");
    lock_guard<mutex> lock(m);
    if (server_state != LEADER) return;
    int peer_idx = is.follower_no() - 1;
    if (!is.success()) {
        LOG_F(INFO, "InstallSnapshot: follower returned unsuccessfully");
        // there was a problem, so abort the current install.
        last_sent_snapshot_offset[peer_idx] = -1;
        return;
    }
    if (is.done()) {
        LOG_F(INFO, "InstallSnapshot: follower is DONE with install!");
        // install complete, erase from map
        last_sent_snapshot_offset[peer_idx] = -1;
        // update their next_index and match_index
        int lii = persistent_storage.state().last_included_index(); 
        next_index[peer_idx] = lii + 1;
        match_index[peer_idx] = lii;
        return;
    }

    // send the next chunk
    int offset = last_sent_snapshot_offset[peer_idx] + CHUNK_SIZE;    
    last_sent_snapshot_offset[peer_idx] = offset;
    RAFTmessage msg = construct_InstallSnapshot(offset, false);
    messenger.sendRequest(server_addrs[peer_idx + 1], msg.SerializeAsString());
    LOG_F(INFO, "InstallSnapshot: sent chunk w/ offset %d", offset);
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
    /* todo (after 191): heartbeats should contain missing log entires */
    if (server_state != LEADER) return;
    heartbeat_timer.start();
    for (int peer_no = 1; peer_no <= server_addrs.size(); peer_no++) {
        replicate_log(peer_no);
    }
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
    if (peer_no == server_no /*|| log.size() < next_index[peer_idx]*/) return;

    /* new (snapshotting) */
    LOG_F(INFO, "replicate log: peer #%d: next idx: %d, LII: %d", peer_no, 
        next_index[peer_idx], persistent_storage.state().last_included_index());
    /* InstallSnapshot send condition */
    if (next_index[peer_idx] <= 
        persistent_storage.state().last_included_index()) {

        // either initiate the installation process, or send a checkup
        bool checkup = true;
        if (last_sent_snapshot_offset[peer_idx] == -1) {
            checkup = false;
            last_sent_snapshot_offset[peer_idx] = 0;
        }
        int offset = last_sent_snapshot_offset[peer_idx];
        RAFTmessage msg = construct_InstallSnapshot(offset, checkup);
        messenger.sendRequest(server_addrs[peer_no], 
                              msg.SerializeAsString()); 
        string log_msg = (checkup ?  
                          "replicate_log: sent InstallSnapshot checkup" : 
                          "replicate_log: sent initial InstallSnapshot");
        LOG_F(INFO, "%s", log_msg.c_str()); 
        return;
    }


    RAFTmessage msg;
    AppendEntries* ae = new AppendEntries();
    msg.set_allocated_appendentries_message(ae);
    msg.set_term(current_term);
    ae->set_leader_no(server_no);
    ae->set_term(current_term);
    ae->set_leader_commit(commit_index);
    ae->set_prev_log_idx(next_index[peer_idx] - 1);
    if (next_index[peer_idx] - 1 > 0) {
        /* new (snapshotting): use last_included_term if needed. */
        int last_incl_index = persistent_storage.state().last_included_index();
        assert(next_index[peer_idx] - 1 >= last_incl_index);
        if (next_index[peer_idx] - 1 == last_incl_index) { 
            int term = persistent_storage.state().last_included_term();
            LOG_F(INFO, "replicate_log(): reached special case, sending term %d",
                  term);
            ae->set_prev_log_term(term);
        } else {
            ae->set_prev_log_term(log[next_index[peer_idx] - 1].term);
        }
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


/*****************************************************************************
 *                             SNAPSHOT-RELATED                              *
 *****************************************************************************/


 /** 
  * Update RAFT's current snapshot with a new one.  The state lock must be held
  * before call.
  *
  * The steps are performed in a fault-tolerant way, such 
  * that the worst case is that a new snapshot was written but the system 
  * crashes before RAFT gets a chance to bookkeep this and upgrade it's snapshot.
  * Importantly, snapshots are never deleted unsafely. 
  */
  void Server::write_snapshot() {
    if (server_state == LEADER) {
        // special case: if we snapshot while other installs are happening, wipe
        // all of them out and start over. this is simpler than waiting to
        // finish the installs in some way, although less efficient. however,
        // it's such a rare ocurrence that I prefer the simplicity tradeoff 
        std::fill(last_sent_snapshot_offset.begin(), 
                  last_sent_snapshot_offset.end(), 
                  -1);
    }
    // Edge case: this can happen during recovery. 
    if (last_applied == persistent_storage.state().last_included_index()) {
        return;
    }
    // create alt snapshot
    string current_filename = persistent_storage.state().snapshot_filename(); 
    string new_filename = (current_filename == snapshot_filename_options[0] 
                            ? snapshot_filename_options[1] 
                            : snapshot_filename_options[0]);

    /* Above, I've chosen an naming scheme that alternates between two names.
       The most natural solution is to choose a temporary name for
       the new snapshot, and then once it's written, delete the old snapshot 
       and update the new snapshot to the main name. However, you run into 
       issues. If you update p_store to temp, then delete and rename, then 
       you can crash before updating p_store. If you don't update p_store at 
       all, then you can crash with just temp on disk, which seems okay, but 
       then that's a check you have to do at recovery. 

       Overall, this seems to be the only way to guarantee the persistent
       storage filename always points to a valid snapshot (valid, but not
       always new).  */
    
    if (!state_machine->exportState(new_filename)) {
        string msg = "write_snapshot(): " + string(strerror(errno));
        LOG_F(INFO, "%s", msg.c_str());
        throw "write_snapshot(): " + string(strerror(errno));
    }

    //write metadata
    // note: must do last_applied instead of commit_index because we can only
    // snapshot safely once something is applied
    int idx = last_applied;
    int term = log[idx].term;
    int prev_snapshot_size = persistent_storage.state().last_included_index();
    persistent_storage.state().set_last_included_index(idx);
    persistent_storage.state().set_last_included_term(term);
    persistent_storage.state().set_snapshot_filename(new_filename);
    persistent_storage.state().set_can_safely_recover_log(false);
    persistent_storage.save();
    LOG_F(INFO, "write_snapshot:() last included index is now %d, term is %d", 
          idx, term);

    // delete the old snapshot
    if (current_filename != "" && std::remove(current_filename.c_str()) != 0) {
        string msg = "write_snapshot(): " + string(strerror(errno));
        LOG_F(INFO, "%s", msg.c_str());
        throw 5;
    }

    LOG_F(INFO, "write_snapshot(): old phys log size: %d", log.physical_size());
    // delete log up through end of snapshot
    log.clip_front(idx - prev_snapshot_size); // note: this is a physical index
    persistent_storage.state().set_can_safely_recover_log(true);
    LOG_F(INFO, "write_snapshot: new phys log size: %d", log.physical_size());
  }


/**
 * Construct an InstallSnapshot RPC, setting all fields except 'data', which is 
 * only done when 'checkup' is false.  
 *
 * The state lock should be held during this call. 
 */
RAFTmessage Server::construct_InstallSnapshot(int offset, bool is_checkup) {
    RAFTmessage msg;
    InstallSnapshot *is_req = new InstallSnapshot();
    msg.set_allocated_installsnapshot_message(is_req);
    msg.set_term(current_term);
    is_req->set_term(current_term);
    is_req->set_leader_no(server_no);
    auto& ps = persistent_storage.state();
    is_req->set_last_included_index(ps.last_included_index());
    is_req->set_last_included_term(ps.last_included_term());
    
    char buffer[CHUNK_SIZE];
    bool done = false;
    int bytesRead = 
        readFileChunk(ps.snapshot_filename(), buffer, offset, CHUNK_SIZE);
    if (bytesRead < CHUNK_SIZE) {
        LOG_F(INFO, "construct_InstSnap: sending last chunk");
        done = true;
    }

    is_req->set_offset(offset);
    is_req->set_data(string(buffer, bytesRead));
    is_req->set_done(done);
    is_req->set_is_checkup(false);
    return msg;
}