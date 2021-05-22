#include "Messenger.h"
#include "TimedCallback.h"
#include "PersistentStorage.h"
#include "RaftLog.h"
#include "util.h"
#include "loguru/loguru.hpp"
#include "RaftRPC.pb.h"
#include "StateMachines/StateMachine.h"
#include <queue>

/**  
 * This class implements a RAFT server instance. See the README and RAFT paper
 * for details.
 */
class Server {
  public:
    Server(const int _server_no, const std::string cluster_file, 
      StateMachine *state_machine, bool restarting);
    void run();

  private:
    enum ServerState {
      FOLLOWER,
      CANDIDATE,
      LEADER
    };

    /* Client requests only receive responses from the leader after the
     * corresponding log entry has been replicated in a majority of the servers
     * in the cluster. */
    struct PendingRequest {
      Messenger::Request req;
      int log_idx;
      int term;
    };

    /* Keep track of which node this instance last voted for. */
    struct Vote {
      int term_voted;
      int voted_for;
    };

    /* State machine instance */
    StateMachine *state_machine;

    /* Log file name. */
    std::string log_file;
  
    /* Lock around server state. */
    std::mutex m;

    /* This CV is notified when new log entries are ready to be applied. */
    std::condition_variable new_commits_cv;

    /* See RAFT paper for a description of these state variables. */
    /* PERSISTENT STATE */
    int current_term {0};
    Vote vote {-1, -1};
    RaftLog log;

    /* VOLATILE STATE ON ALL SERVERS */
    int commit_index {0};
    int last_applied {0};

    /* VOLATILE STATE ON LEADERS */
    std::vector<int> next_index;
    std::vector<int> match_index;
    /* snapshotting: stores the offset of the most recent snapshot chunk that
     * should have been received by each peer. -1 if no install in progress. */ 
    std::vector<int> last_sent_snapshot_offset;

    /* ADDITIONAL STATE */
    int server_no;
    ServerState server_state {FOLLOWER};
    /* Track which nodes have voted for this instance during an election. */
    std::set<int> votes_received;
    /* Our best guess of the current RAFT leader to send to clients. */
    int last_observed_leader_no {1};
    /* Client requests are executed and responded to in order. Hence, a queue. */
    std::queue<PendingRequest> pending_requests;

    /* Maps { server number -> net address }. */
    unordered_map<int, std::string> server_addrs;
    /* Name of this instance's state recovery file. */
    std::string recovery_file;
    /* snapshotting: snapshot filenames choices for this particular server */
    std::vector<string> snapshot_filename_options;
    /* snapshotting: designated filename for in-progress InstallSnapshots */
    std::string partially_installed_snapshot_filename;
    /* snapshotting: current progress in an InstallSnapshot sequence */
    int partially_installed_snapshot_offset {-1};

    /* UTIL. See the files that implement these classes for descriptions. */
    PersistentStorage persistent_storage;
    Messenger messenger;
    TimedCallback election_timer;
    TimedCallback heartbeat_timer;

    void handler_RAFTmessage(const RAFTmessage &msg, 
      std::optional<Messenger::Request> req = std::nullopt);

    /* RPC REQUEST HANDLERS */
    void handler_RequestVote(Messenger::Request &req, 
        const RequestVote &rv);
    void handler_AppendEntries(Messenger::Request &req, 
        const AppendEntries &ae);
    void handler_InstallSnapshot(Messenger::Request &req, 
        const InstallSnapshot& is);
    void handler_ClientRequest(Messenger::Request &req, 
        const ClientRequest &cr);

    /* RPC RESPONSE HANDLERS */
    void handler_RequestVote_response(const RequestVote &rv);
    void handler_AppendEntries_response(const AppendEntries &ae);
    void handler_InstallSnapshot_response(const InstallSnapshot& is);

    /* TIMED CALLBACKS */
    void start_election();
    void send_heartbeats();

    /* LOG ENTRY APPLICATION TASK */
    void apply_log_entries_task();

    /* HELPER FUNCTIONS */
    void replicate_log(int peer_no);
    void broadcast_msg(const RAFTmessage &msg);
    RAFTmessage construct_InstallSnapshot(int offset, bool is_checkup);

    /* SNAPSHOTTING */
    void write_snapshot();
};
