// adapted from user Dietmar Kühl on SO: 
// https://stackoverflow.com/questions/12805041/c-equivalent-to-javas-blockingqueue

#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <optional>

using namespace std::chrono;


/**
 * This class implements a queue with built-in thread-safe blocking properties
 * on its push / pop methods.  
 */ 
template <typename T>
class BlockingQueue
{
private:
    std::mutex              d_mutex;
    std::condition_variable_any d_condition;
    std::deque<T>           d_queue;
public:
    /* push a value and notify a waiting thread. */
    void notifyingPush(T const& value) {
        {
            std::unique_lock<std::mutex> lock(this->d_mutex);
            d_queue.push_front(value);
        }
        this->d_condition.notify_one();
    }

    /* wait indefinitely until the queue is non-empty and pop the front value */
    T waitingPop() { 
        std::unique_lock<std::mutex> lock(this->d_mutex);
        this->d_condition.wait(lock, [=]{ return !this->d_queue.empty(); });
        T rc(std::move(this->d_queue.back()));
        this->d_queue.pop_back();
        return rc;
    }

    /* wait for a timeout duration (in milliseconds) until the queue is non-empty
       and pop the front value */
    std::optional<T> waitingPop_timed(int timeout_ms) {
        std::unique_lock<std::mutex> lock(this->d_mutex);
        this->d_condition.wait_until(
            lock, 
            system_clock::now() + milliseconds(timeout_ms), 
            [=]{ return !this->d_queue.empty(); 
        });
        if (this->d_queue.empty()) {
            return std::nullopt;
        }
        T rc(std::move(this->d_queue.back()));
        this->d_queue.pop_back();
        return rc;
    }
};