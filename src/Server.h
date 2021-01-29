#include <optional>
#include <vector>
#include <chrono>
#include "Messenger.h"

class Timer {
  public:
    Timer(int duration_ms);
    Timer(int duration_ms_lower_bound, int duration_ms_upper_bound);
    void start();
    bool is_expired();
    
  private:
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> _start_time;
    std::chrono::milliseconds _timer_duration;
    
    // FOR USE IN RANDOM TIMER
    std::optional<int> _lower_bound;
    std::optional<int> _upper_bound;    
};

class Server {
  public:
    Server(const int server_id, const unordered_map<int, struct sockaddr_in>& cluster_list);
    void run();

  private:
    // PERSISTENT STATE
    int _current_term {0};
    std::optional<int> _voted_for;
    // _log[] -> might store protobuf messages encapsulating the raw commands?

    // VOLATILE STATE ON ALL SERVERS
    int _commit_index {0};
    int _last_applied {0};

    // VOLATILE STATE ON LEADERS
    std::vector<int> _next_index;
    std::vector<int> _match_index;

    // ADDITIONAL STATE
    bool _leader {false};

    // UTIL
    Messenger _messenger;
    Timer _election_timer;
    Timer _heartbeat_timer;

    // FOR DEBUGGING/LOGGING
    int _server_id;

    void RPC_handler();
    void leader_tasks();
    void handler_AppendEntries();
    void handler_RequestVote();
    void handler_ClientCommand();
    bool try_election();
    void apply_log_entries();
};
