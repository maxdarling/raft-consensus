#include <string>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <netinet/in.h>  // for sockaddr_in

const std::string DEFAULT_SERVER_FILE_PATH = "../server_list";

class Timer {
  public:
    Timer(int duration_ms);
    Timer(int duration_ms_lower_bound, int duration_ms_upper_bound);
    void start();
    bool has_expired();
    void mark_as_expired() { _marked_as_expired = true; }
    
  private:
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> _start_time;
    std::chrono::milliseconds _timer_duration;
    bool _marked_as_expired {false};
    
    // FOR USE IN RANDOM TIMER
    std::optional<int> _lower_bound;
    std::optional<int> _upper_bound;    
};

std::unordered_map<int, sockaddr_in> parseClusterInfo(std::string serverFilePath);
