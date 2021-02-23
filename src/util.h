#include <string>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <netinet/in.h>  // for sockaddr_in

const std::string SRC_DIR = std::string(__FILE__).substr(0, std::string(__FILE__).find_last_of("/") + 1);
const std::string DEFAULT_SERVER_FILE_PATH =  SRC_DIR + "../server_list";

/**
 * This class implements a timer which can be deterministic, going off after a
 * set duration, or stochastic, going off after a random duration in a specified
 * interval.
 */
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
    
    // FOR USE IN STOCHASTIC TIMER
    std::optional<int> _lower_bound;
    std::optional<int> _upper_bound;    
};

std::unordered_map<int, sockaddr_in> parseClusterInfoBad(std::string serverFilePath);
std::unordered_map<int, std::string> parseClusterInfo(std::string serverFilePath);
int parsePort(std::string hostAndPort);

