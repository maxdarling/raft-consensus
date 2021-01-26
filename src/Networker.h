#include <vector>
#include <unordered_map>
#include <map>
#include <queue>

#include <mutex>

#include <thread>

/* sockaddr_in */
#include <netinet/in.h>

using std::vector;
using std::unordered_map;
using std::map;
using std::queue; 
// todo: figure out how to hide this in the source somehow
struct cmpByAddr {
    bool operator()(const struct sockaddr_in& a, const struct sockaddr_in& b) const {
        if (a.sin_port != b.sin_port) {
            return a.sin_port < b.sin_port;
        }
        return a.sin_addr.s_addr < b.sin_addr.s_addr;

        }
};

class Networker {
    public: 
        Networker(const short port);
        ~Networker();

        int establishConnection(const struct sockaddr_in& serv_addr);
        void sendAll(const int connfd, const char* message, int length); 
        int getNextReadableFd();

    private:
        void listener_routine();
        void poller_routine(); // not yet implemented. no need? 

        int _listenfd;
        struct sockaddr_in _addr;

        std::mutex _m;

        //unordered_map<sockaddr_in, int> _currConnections; 
        map<struct sockaddr_in, int, cmpByAddr> _currConnections; // above requires custom hash
        //map<struct sockaddr_in, int> _currConnections; // above requires custom hash
        //map<sockaddr_in, int, decltype(cmp)> _currConnections;

        struct pollfd* _pfds;
        int _pfds_size; 
        int _pfds_capacity;
        queue<int> _readableFds;
};
