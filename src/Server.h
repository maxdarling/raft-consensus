#include "loguru/loguru.hpp"
#include "RaftRPC.pb.h"
#include "Messenger.h"
#include "TimedCallback.h"
#include "util.h"

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
    /* To track our most recent leader election vote. */
    struct Vote {
      int term_voted;
      int voted_for;
    };

    enum ServerState {
      FOLLOWER,
      CANDIDATE,
      LEADER
    };

    std::mutex m;

    /* PERSISTENT STATE */
    int current_term {0};
    std::optional<Vote> _vote;

    /* VOLATILE STATE ON ALL SERVERS */
    int _commit_index {0};
    int _last_applied {0};

    /* VOLATILE STATE ON LEADERS */
    std::vector<int> _next_index;
    std::vector<int> _match_index;

    /* ADDITIONAL STATE */
    // int _server_id;
    int server_no;
    // bool _leader {false};
    ServerState server_state {FOLLOWER};

    int _last_observed_leader_id {1};
    std::string _recovery_fname;      // name of instance's state recovery file
    unordered_map<int, std::string> cluster_addrs; // map each node ID to its net address

    /* UTIL */
    Messenger messenger;
    TimedCallback election_timer;
    /*
    Timer _election_timer;
    Timer _heartbeat_timer;
    */
    void request_listener();
    void response_listener();
    void timeout_listener();

    void request_handler(const Messenger::Request &req);

    void handler_AppendEntries(const AppendEntries &ae);
    void handler_RequestVote(const RequestVote &rv);
    void handler_ClientRequest(const ClientRequest &cr);

    void start_election();

    void process_command_routine(std::string command, std::string clientHostAndPort);
    void leader_tasks();
    bool try_election();
    void apply_log_entries();

    /*
    void broadcast_RPC(const RPC &rpc);
    void send_RPC(RPC rpc, int _server_id);
    std::optional<RPC> receive_RPC();
    */
};
