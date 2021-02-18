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
        
        bool sendRequest(std::string hostAndPort, std::string message); 
        bool sendResponse(Request::ResponseToken responseToken, std::string message);

        std::optional<Request> getNextRequest(int timeoutMs);
        std::optional<std::string> getNextResponse(int timeoutMs); 

        std::optional<std::string> awaitResponseFrom(std::string hostAndPort, int timeout); // seems impossible to implement

    private:
        /* background thread routine to manage incoming connections */
        void listenerRoutine(int listenfd);

        //void receiveMessagesTask(int sockfd, BlockingQueue<std::string>* readyMessages); // todo: get this to work. thread errors. 
        void receiveMessagesTask(int sockfd, bool shouldReadRequests);

        void sendMessagesTask(int sockfd);

        /* shared state between a socket's sender and the main dispatching thread */
        struct SocketState {
            BlockingQueue<std::string> outboundMessages;
            std::string hostAndPort; // only used for request senders to remove self from map.

            // only used for response senders to check if sending is vaild 
            std::chrono::time_point<steady_clock> timeCreated;

            /* used to coordinate which of the reader / sender should close the socket, since 
               it's only safe to close ths socket once both threads have exited, and won't be 
               touching the socket anymore. 
            */
            bool oneExited = false;
        };
        /* maps socket to the state shared between its sender and the main dispatching thread */
        unordered_map<int, SocketState *> _socketToState;

        /* maps addresses for healthy connections to the associated socket */
        unordered_map<std::string, int> _hostAndPortToFd;

        /* store collected messages */
        BlockingQueue<Request> _requestQueue;
        BlockingQueue<std::string> _responseQueue; 

        /* synchronize access to the maps above */  
        std::mutex _m;
};
