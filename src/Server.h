#include "Messenger.h"
#include "util.h"
#include "RaftRPC.pb.h"

class Server {
  public:
    Server(const int server_id, const unordered_map<int, struct sockaddr_in>& cluster_list);
    void run();

  private:
    struct Vote {
      int term_voted;
      int voted_for;
    };

    // PERSISTENT STATE
    int _current_term {0};
    std::optional<Vote> _vote;

    // VOLATILE STATE ON ALL SERVERS
    int _commit_index {0};
    int _last_applied {0};

    // VOLATILE STATE ON LEADERS
    std::vector<int> _next_index;
    std::vector<int> _match_index;

    // ADDITIONAL STATE
    bool _leader {false};
    int _last_observed_leader_id {1};

    // UTIL
    Messenger _messenger;
    Timer _election_timer;
    Timer _heartbeat_timer;

    // FOR DEBUGGING/LOGGING
    int _server_id;
    // TODO(ali): make this the cluster_list map instead so we have addresses to direct clients
    std::vector<int> _cluster_list; // stores IDs of servers in cluster

    void RPC_handler(const RPC &rpc);
    void leader_tasks();
    void handler_AppendEntries(const AppendEntries &ae);
    void handler_RequestVote(const RequestVote &rv);
    void handler_ClientCommand(const ClientRequest &cr);
    bool try_election();
    void apply_log_entries();
    void process_command_routine(std::string command, sockaddr_in client_addr);

    // send RPC to all servers in cluster
    void send_RPC(const RPC &rpc);
    // send RPC to a particular server
    void send_RPC(const RPC &rpc, int _server_id);
    std::optional<RPC> receive_RPC();
};
