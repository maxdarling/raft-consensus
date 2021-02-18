#include <unordered_map>
#include <mutex>
#include <chrono>
#include "BlockingQueue.h"

using std::unordered_map;
using std::chrono::steady_clock;


/**
 * The Messenger class implements message-based communication between servers
 * and clients.  
 * 
 * Client/Server Differences: 
 *   - Clients: Send requests to servers and get back responses. 
 *
 *   - Servers: Get requests from and send responses to clients. 
 *              Do anything with other severs. 
 */
class Messenger {
    public: 
        Messenger(int listenPort); // server instance
        Messenger();                 // client instance
        ~Messenger();

        /* the type for a recieved request, containing the message itself and 
           a 'ResponseToken' to be used when responding to the request. */
        struct Request {
            std::string message;
            struct ResponseToken {
                int sockfd;
                std::chrono::time_point<steady_clock> timestamp;
            } responseToken;
        };
        
        bool sendRequest(std::string peerAddr, std::string message); 
        bool sendResponse(Request::ResponseToken responseToken, std::string message);

        std::optional<Request> getNextRequest(int timeoutMs);
        std::optional<std::string> getNextResponse(int timeoutMs); 

    private:
        void listener(int listenfd);

        void receiveMessagesTask(int sockfd, bool shouldReadRequests);
        void sendMessagesTask(int sockfd);

        /* shared state between a socket's worker threads and the main thread */
        struct SocketState {
            /* messages to be sent on this socket */
            BlockingQueue<std::string> outboundMessages;

            /* time the socket was created. used to catch state responses */  
            std::chrono::time_point<steady_clock> timeCreated;

            /* binary "refcount" to coordinate worker cleanup efforts */
            bool oneExited = false;

            /* key into '_peerAddrToSocket' to enable full cleanup */
            std::string peerAddr;
        };
        /* maps each socket to state shared with its worker threads */
        unordered_map<int, SocketState *> _socketToState;

        /* maps peer network addresses to their associated sockets */
        unordered_map<std::string, int> _peerAddrToSocket;

        /* synchronize access to '_socketToState' and '_peerAddrToSocket' */  
        std::mutex _m;

        /* store request and response messages when received */
        BlockingQueue<Request> _requestQueue;
        BlockingQueue<std::string> _responseQueue; 
};
