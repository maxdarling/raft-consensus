#include "loguru/loguru.hpp"
#include "RaftRPC.pb.h"
#include "Messenger.h"
#include "TimedCallback.h"
#include "util.h"
#include <queue>

/**  
 * This class implements a RAFT server instance. Each server in a RAFT cluster
 * is disambiguated by its unique server ID.
 */
class Server {
  public:
  /*
    Server(const int server_id, 
        const unordered_map<int, std::string>& cluster_map);
  */
    Server(const int _server_no, const std::string cluster_file);
    void run();

  private:
    enum ServerState {
      FOLLOWER,
      CANDIDATE,
      LEADER
    };

    struct LogEntry {
      std::string command;
      int term;
    };

    struct PendingRequest {
      Messenger::Request req;
      int log_idx;
      int term;
    };

    struct Vote {
      int term_voted;
      int voted_for;
    };
  
    /* LOCK AROUND STATE */
    std::mutex m;
    std::condition_variable new_commits_cv;

    /* PERSISTENT STATE */
    int current_term {0};
    Vote vote {-1, -1};
    std::vector<LogEntry> log {{"head", -1}}; // 1-INDEXED

    /* VOLATILE STATE ON ALL SERVERS */
    int commit_index {0};
    int last_applied {0};

    /* VOLATILE STATE ON LEADERS */
    std::vector<int> next_index;
    std::vector<int> match_index;

    /* ADDITIONAL STATE */
    int server_no;
    ServerState server_state {FOLLOWER};
    std::set<int> votes_received;
    int last_observed_leader_no {1};
    std::queue<PendingRequest> pending_requests;

    // std::string _recovery_fname;      // name of instance's state recovery file
    // { server number -> net address }
    unordered_map<int, std::string> server_addrs;

    /* UTIL */
    Messenger messenger;
    TimedCallback election_timer;
    TimedCallback heartbeat_timer;

    void request_listener();
    void response_listener();

    void request_handler(Messenger::Request &req);
    void response_handler(const RAFTmessage &msg);

    void handler_AppendEntries(Messenger::Request &req, 
        const AppendEntries &ae);
    void handler_AppendEntries_response(const AppendEntries &ae);
    void handler_RequestVote(Messenger::Request &req, 
        const RequestVote &rv);
    void handler_RequestVote_response(const RequestVote &rv);
    void handler_ClientRequest(Messenger::Request &req, 
        const ClientRequest &cr);

    void replicate_log(int peer_no);
    void start_election();
    void send_heartbeats();

    // void process_command_routine(std::string command, std::string clientHostAndPort);
    void apply_log_entries_task();
    void broadcast_msg(const RAFTmessage &msg);
};
