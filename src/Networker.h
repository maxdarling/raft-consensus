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

class Networker {
    public: 
        Networker(const short port);
        ~Networker();

        int establishConnection(const struct sockaddr_in& serv_addr);
        int sendAll(const int connfd, const void* message, int length); 
        int readAll(const int connfd, void* buf, int bytesToRead);
        int getNextReadableFd(bool shouldBlock);

    private:
        void listenerRoutine();
        void pollerRoutine(); // not yet implemented. no need? 

        int _listenfd;
        struct sockaddr_in _addr;

        std::mutex _m;

        struct pollfd* _pfds;
        int _pfds_size; 
        int _pfds_capacity;
        queue<int> _readableFds;
};
