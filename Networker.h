#include <vector>
#include <unordered_map>
#include <queue>

/* sockaddr_in */
#include <netinet/in.h>

using std::vector;
using std::unordered_map;
using std::queue;


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

        unordered_map<sockaddr_in, int> _currConnections;

        struct pollfd* _pfds;
        int _pfds_size; 
        int _pfds_capacity;
        queue<int> _readableFds;
};
