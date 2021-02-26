#include "Messenger.h"
#include "util.h"
#include "RaftRPC.pb.h"

/**
 * This class implements a RAFT server instance. Each server in a RAFT cluster
 * is disambiguated by its unique server ID.
 */
class Server {
  public:
    Server(const int server_id, 
        const unordered_map<int, struct sockaddr_in>& cluster_list);
    void run();

  private:
    /* To track our most recent leader election vote. */
    struct Vote {
      int term_voted;
      int voted_for;
    };

    /* PERSISTENT STATE */
    int _current_term {0};
    std::optional<Vote> _vote;

    /* VOLATILE STATE ON ALL SERVERS */
    int _commit_index {0};
    int _last_applied {0};

    /* VOLATILE STATE ON LEADERS */
    std::vector<int> _next_index;
    std::vector<int> _match_index;

    /* ADDITIONAL STATE */
    int _server_id;
    bool _leader {false};
    int _last_observed_leader_id {1};
    std::string _recovery_fname;      // name of instance's state recovery file
    std::vector<int> _cluster_list;   // stores IDs of servers in cluster

    /* UTIL */
    Messenger _messenger;
    Timer _election_timer;
    Timer _heartbeat_timer;

    void RPC_handler(const RPC &rpc);
    void handler_AppendEntries(const AppendEntries &ae);
    void handler_RequestVote(const RequestVote &rv);
    void handler_ClientCommand(const ClientRequest &cr);
    void process_command_routine(std::string command, sockaddr_in client_addr);
    void leader_tasks();
    bool try_election();
    void apply_log_entries();

    void send_RPC(const RPC &rpc);
    void send_RPC(const RPC &rpc, int _server_id);
    std::optional<RPC> receive_RPC();
};
