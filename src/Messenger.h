#include "loguru/loguru.hpp"
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "BlockingQueue.h"

using std::unordered_map;
using std::chrono::steady_clock;


/**
 * The Messenger class implements message-based communication between servers
 * and clients with request/response semantics.  
 * 
 * Client/Server Roles:
 *   - Clients: Send requests to servers and get back responses. 
 *
 *   - Servers: Receive requests from clients and send back responses. 
 *              Do anything with other severs. 
 */
class Messenger {
    public: 
        Messenger(int listenPort); // server instance
        Messenger();                 // client instance
        ~Messenger();

        /* the type for a recieved request, containing the request message
           itself and a means to respond with 'sendResponse()' */
        struct Request {
            std::string message;
            bool sendResponse(std::string responseMessage);

            private:
                Request(std::string m, int sock, time_point<steady_clock> ts, 
                        Messenger& mp) : message(m), _sockfd(sock), 
                        _timestamp(ts), _messengerParent(mp) {};
                int _sockfd;
                time_point<steady_clock> _timestamp;
                Messenger& _messengerParent;
            friend class Messenger;
        };
        
        bool sendRequest(std::string peerAddr, std::string message); 
        std::optional<Request> getNextRequest(int timeoutMs = -1);
        std::optional<std::string> getNextResponse(int timeoutMs = -1); 

    private:
        void listener();

        void receiveMessagesTask(int sockfd, bool shouldReadRequests);
        void sendMessagesTask(int sockfd);

        /* shared state between a socket's worker threads and the main thread */
        struct SocketState {
            /* messages to be sent on this socket */
            BlockingQueue<std::string> outboundMessages;

            /* time the socket was created. used only on sockets 
               that receive requests and send responses. */  
            std::chrono::time_point<steady_clock> timeCreated;

            /* binary "refcount" to coordinate worker cleanup efforts */
            bool oneExited = false;

            /* key into '_peerAddrToSocket' to enable cleanup of that map. 
               used only on sockets that send requests and receive responses. */
            std::string peerAddr;

            /* flag to indicate the destructor closed the socket. */
            bool destructorClosed = false;
        };

        /* maps each socket fd to the state shared with its worker threads */
        unordered_map<int, SocketState *> _socketToState;

        /* maps peer network addresses to their associated sockets */
        unordered_map<std::string, int> _peerAddrToSocket;

        /* synchronize access to '_socketToState' and '_peerAddrToSocket' */  
        std::mutex _m;

        /* store request and response messages when received */
        BlockingQueue<Request> _requestQueue;
        BlockingQueue<std::string> _responseQueue; 

        /* port to listen for requests on (server-only) */
        std::optional<int> _listenSock;


  public: 
    // exception class for messenger exceptions
    class Exception : public std::exception {
        private:
            std::string _msg;
        public:
            Exception(const std::string& msg) : _msg(msg){}

            virtual const char* what() const noexcept override
            {
                return _msg.c_str();
            } 
    };
};


