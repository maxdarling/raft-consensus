#include <vector>
#include <unordered_map>

/* sockaddr_in */
#include <netinet/in.h>

using std::vector;
using std::unordered_map;


class Networker {
    public: 
        Networker(const short port);
        void send(const struct sockaddr_in& serv_addr, const vector<char>& message);
        vector<char> receive(); 

    private:
        int _listenfd;
        struct sockaddr_in _addr;
        unordered_map<sockaddr_in, int> _currConnections;
        


    
};