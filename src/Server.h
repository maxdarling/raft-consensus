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

    struct Vote {
      int term_voted;
      int voted_for;
    };
  
    std::mutex m;

    /* PERSISTENT STATE */
    int current_term {0};
    Vote vote {-1, -1};
    std::vector<LogEntry> log {{"", -1}}; // 1-INDEXED

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
    std::queue<Messenger::Request> pending_client_requests;

    // std::string _recovery_fname;      // name of instance's state recovery file
    unordered_map<int, std::string> server_addrs; // map each node ID to its net address

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

    void start_election();
    void send_heartbeats();

    // void process_command_routine(std::string command, std::string clientHostAndPort);
    void apply_log_entries();
    void broadcast_msg(const RAFTmessage &msg);
};
