#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <optional>

using namespace std::chrono_literals;


// copied from: https://stackoverflow.com/questions/12805041/c-equivalent-to-javas-blockingqueue
template <typename T>
class BlockingQueue
{
private:
    std::mutex              d_mutex;
    std::condition_variable_any d_condition;
    std::deque<T>           d_queue;
public:
    void blockingPush(T const& value) {
        {
            std::unique_lock<std::mutex> lock(this->d_mutex);
            d_queue.push_front(value);
        }
        this->d_condition.notify_one();
    }
    T blockingPop() { 
        std::unique_lock<std::mutex> lock(this->d_mutex);
        this->d_condition.wait(lock, [=]{ return !this->d_queue.empty(); });
        T rc(std::move(this->d_queue.back()));
        this->d_queue.pop_back();
        return rc;
    }
    std::optional<T> blockingPop_timed(int timeout) {  // todo: actually use the timeout
        std::unique_lock<std::mutex> lock(this->d_mutex);
        this->d_condition.wait_until(
            lock, 
            std::chrono::system_clock::now() + 100ms, 
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