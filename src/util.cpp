#include "util.h"
#include <random>    // for random_device
#include <fstream>   // for ifstream

// Construct a timer that, once started, expires after duration_ms milliseconds.
Timer::Timer(int duration_ms) : _timer_duration(duration_ms) {}

// Construct a timer that, once started, expires after a random duration within 
// the interval [duration_ms_lower_bound, duration_ms_upper_bound].
Timer::Timer(int duration_ms_lower_bound, int duration_ms_upper_bound) 
    : _lower_bound(duration_ms_lower_bound),
      _upper_bound(duration_ms_upper_bound) {
    // seed pseudorandom generator with truly random value
    std::random_device rd;
    srand(rd());
}

// Start the timer. If the timer is already running, restart it.
void Timer::start() {
    _start_time = std::chrono::steady_clock::now();
    if (_lower_bound) {
        _timer_duration = std::chrono::milliseconds {
            rand() % (*_upper_bound - *_lower_bound + 1) + *_lower_bound
        };
    }
}

// Returns true when the timer has expired, or if the timer has been marked as
// expired.
bool Timer::has_expired() {
    if (_marked_as_expired) {
        _marked_as_expired = false;
        _start_time.reset();
        return true;
    }

    if (!_start_time) return false;
    
    if (std::chrono::steady_clock::now() - *_start_time >= _timer_duration) {
        _start_time.reset();
        return true;
    } 
    return false;
}

// Parse a cluster info map from a server address list file.
std::unordered_map<int, sockaddr_in> parseClusterInfoBad(std::string serverFilePath) {
    std::unordered_map<int, sockaddr_in> clusterInfo;
    
    std::ifstream ifs(serverFilePath);
    unsigned int ipAddr, port;
    for (int serverNum = 1; ifs >> ipAddr >> port; ++serverNum) {
        sockaddr_in addr;
        memset(&addr, '0', sizeof(addr));
        addr.sin_family      = AF_INET; // use IPv4
        addr.sin_addr.s_addr = htons(ipAddr);
        addr.sin_port        = htons(port);

        clusterInfo.emplace(serverNum, addr);
    }

    return clusterInfo;
}

// revised for p2
std::unordered_map<int, std::string> parseClusterInfo(std::string serverFilePath) {
    std::unordered_map<int, std::string> clusterInfo;
    
    std::ifstream ifs(serverFilePath);
    std::string hostAndPort;
    for (int serverNum = 1; ifs >> hostAndPort; ++serverNum) {
        clusterInfo.emplace(serverNum, hostAndPort);
    }

    if (clusterInfo.empty()) {
        throw std::invalid_argument("Invalid (or empty) server address list! "
            "Either the file is improperly formatted, or the custom path to "
            "the file is wrong, or the default server_list has been "
            "deleted/moved/corrupted. See README for details.");
    }

    return clusterInfo;
}

// behavior: "127.0.0.95:8000" -> 8000
// TODO(ali): error checking? probably not actually since stoi already throws
int parsePort(std::string hostAndPort) {
    return std::stoi(hostAndPort.substr(hostAndPort.find(":") + 1));
}
