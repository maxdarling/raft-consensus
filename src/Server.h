#include <optional>
#include <vector>
#include "Messenger.h"

class Server {
  public:
    Server();
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

    void RPC_handler();
    void leader_tasks();
    void handler_AppendEntries();
    void handler_RequestVote();
    void handler_ClientCommand();
    bool try_election();
    void apply_log_entries();
};
