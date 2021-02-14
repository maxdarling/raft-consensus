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


/*
 * This class implements a networking abstraction for servers. A Networker 
 * instance automatically manages incoming connections, and allows the client
 * to make and manage their own outgoing connections. Convenience methods
 * for sending and reading from sockets are provided, too. 
 */ 
class Networker {
    public: 
        Networker(const short port);
        ~Networker();

        int establishConnection(const sockaddr_in& serv_addr);
        int getNextReadableFd(bool shouldBlock);

        int sendAll(const int connfd, const void* message, int length); 
        int readAll(const int connfd, void* buf, int bytesToRead);

    private:
        /* background thread routine to manage incoming connections */
        void listenerRoutine();

        /* networking information for this instance */
        int _listenfd;
        sockaddr_in _addr;

        /* table of polled file descriptors */
        vector<struct pollfd> _pfds;

        /* container of file descriptors that are ready to read from */
        queue<int> _readableFds;

        /* synchronize accesses of the polling table and fd queue */
        std::mutex _m;
};
