#include <functional>
#include <mutex>
#include <condition_variable>

/**
 * This class implements something super cool
 */
class TimedCallback {
  public:
    TimedCallback(int duration_ms, std::function<void()> f);
    TimedCallback(int duration_ms_lower_bound, int duration_ms_upper_bound, std::function<void()> f);
    void start();
    void stop();
    
  private:
    enum TimerState {
        NOT_RUNNING,
        RUNNING,
        RESTART_REQUESTED,
        STOP_REQUESTED
    };

    std::function<void()> cb;
    int lower_bound;
    int upper_bound;

    TimerState timer_state {NOT_RUNNING};
    std::mutex m;
    std::condition_variable cv; 
};