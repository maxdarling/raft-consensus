#include <optional>
#include <chrono>

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
