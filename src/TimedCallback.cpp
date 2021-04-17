#include "TimedCallback.h"
#include "loguru/loguru.hpp"
#include <random> 
#include <thread>

/* loguru priority */
const int LOG_PRIORITY = 3;

/**
 * Construct a TimedCallback function f that will be executed 'duration_ms'
 * milliseconds after it has been started.
 */
TimedCallback::TimedCallback(int duration_ms, std::function<void()> f)
  : cb(f), lower_bound(duration_ms), upper_bound(duration_ms) {}

/**
 * Here, f will be executed after a random duration within the specified 
 * interval.
 */
TimedCallback::TimedCallback(int duration_ms_lower_bound, 
    int duration_ms_upper_bound, std::function<void()> f)
  : cb(f), 
    lower_bound(duration_ms_lower_bound), 
    upper_bound(duration_ms_upper_bound) 
{
    std::random_device rd;
    srand(rd());
}

/**
 * Start the timer for callback execution. If a timer is already running when
 * this function is called, it will be restarted before executing the callback.
 */
void TimedCallback::start() {
    std::lock_guard<std::mutex> l(m);

    // CASE: timer is already restarting, so don't launch a new thread
    if (timer_state == RESTART_REQUESTED) return;

    if (timer_state == RUNNING || timer_state == STOP_REQUESTED) {
        VLOG_F(LOG_PRIORITY, "Timer already running; signalling restart");
        timer_state = RESTART_REQUESTED;
        cv.notify_one();
        return;
    }

    VLOG_F(LOG_PRIORITY, "Starting timed callback");
    timer_state = RUNNING;
    std::thread([this]() {
        {
            std::unique_lock<std::mutex> _l(m);
            for (;;) {
                auto duration = std::chrono::milliseconds { 
                    lower_bound == upper_bound? lower_bound :
                    rand() % (upper_bound - lower_bound + 1) + lower_bound
                };

                cv.wait_for(_l, duration, [this] {
                    return timer_state != RUNNING;
                });

                if (timer_state == STOP_REQUESTED) {
                    VLOG_F(LOG_PRIORITY, "Timer stop request received");
                    timer_state = NOT_RUNNING;
                    return;
                }
                if (timer_state == RESTART_REQUESTED) {
                    VLOG_F(LOG_PRIORITY, "Timer restart request received");
                    timer_state = RUNNING;
                }
                else {
                    VLOG_F(LOG_PRIORITY, "Timer expired; executing cb");
                    break;
                }
            }
            timer_state = NOT_RUNNING;
        }
        cb();
    }).detach();
}

/**
 * Stop the callback timer. If the timer has not been started, this function
 * will have no effect.
 */
void TimedCallback::stop()
{
    std::lock_guard<std::mutex> l(m);
    if (timer_state == RUNNING || timer_state == RESTART_REQUESTED) {
        VLOG_F(LOG_PRIORITY, "Timer running; signalling stoppage");
        timer_state = STOP_REQUESTED;
        cv.notify_one();
    }
}
