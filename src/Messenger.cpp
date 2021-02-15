#include "Messenger.h"

/* low-level networking */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <poll.h>

/* general */
#include <unistd.h>
#include <cassert>
#include <iostream>
#include <thread>

using std::cout, std::endl;

int sendEntireMessage(const int connfd, const void* buf, const int length);
int readEntireMessage(const int connfd, void* buf, int bytesToRead);
/** 
 * ~~~~~~ Design Notes ~~~~~~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each communication leads
 * with 4 bytes - denoting message length - and follows with the message
 * itself.
 */ 


/**
 * Messenger constructor.
 * 
 * Note: port should be in host-byte order. 
 */
Messenger::Messenger(const int myPort) : _myPort(myPort) {
    // start a networker on our assigned port
    std::thread listener(&Messenger::listenerRoutine, this);
    listener.detach();

    // start message collection background thread
    // note: threading architecture elminates this need
    // std::thread collector(&Messenger::collectMessagesRoutine, this);
    // collector.detach();

    sleep(5);
    cout << "finished messenger constructor "<< endl;
}


/**
 * Class destructor. 
 */
Messenger::~Messenger() {
}


/**
 * Socket management table:
 * 
 * -what: a map from socket : state for the socket
 * each entry represents an open socket (either we or a peer opened it).
 * 
 * -how: whenever a new socket is opened (sendMessage, listener), an entry is added
 * to the table. each state variable contains a thread that will manage this sockets 
 * reads/writes, a semaphore/cond var for synchro, an outbound queue of messages to send, 
 * a flag to indicate a read was requested, and a timeout variable set by the caller for
 * the read.  
 * 
 */


/**
 * Thread routine for socket managers. 
 */
void Messenger::sendAndReceiveMessagesTask(SocketManagerState* state){
        int sockfd = state->sockfd;
        queue<std::string>& outboundMessages = state->outboundMessages;
                                            
        // rotate between receiving and sending messages
        int timeout = 1000; // todo: choose wisely
        while (true) {
            // if there's messages to send, do so
            bool sendAMessage;
            {
                std::lock_guard<std::mutex> lock(_m);
                sendAMessage = !outboundMessages.empty(); 
            }
            if (sendAMessage) {
                std::string message = outboundMessages.front();
                outboundMessages.pop();

                /* SEND MESSAGE */
                {
                    // todo: determine if we actually need to do this conversion on both ends. 
                    int msgLength = htonl(message.length()); 

                    // send message length and body
                    if ((sendEntireMessage(sockfd, &msgLength, sizeof(msgLength)) != 
                                                                sizeof(msgLength)) ||      
                        (sendEntireMessage(sockfd, message.c_str(), message.length()) != 
                                                                    message.length())) {

                        /* if the message failed to send, assume the socket is dead and close
                        * the connection. */
                        std::lock_guard<std::mutex> lock(_m);
                        close(sockfd);
                        _socketTable.erase(sockfd);
                        // todo: must also remove _hostAndPortToFd entry :C
                        return;
                    }
                    cout << "sent a message of length " << msgLength << endl;
                }
            } 
            // always read on a timeout 
            // note, hard to do a timeout unless we poll. so this method is easily able to hang indefinitely. 
            // the only reason it currently doesn't is because sends/reads are always 1:1 in the server, 
            // and sends come first. 

            /* READ MESSAGE */
            {
                // read the message length first
                int msgLength;
                int n = readEntireMessage(sockfd, &msgLength, sizeof(msgLength));
                if (n != sizeof(msgLength)) {
                    cout << "message read error" << endl;
                    close(sockfd);
                    std::lock_guard<std::mutex> lock(_m);
                    _socketTable.erase(sockfd);
                    return;
                }
                msgLength = ntohl(msgLength); // convert to host order before use

                // read the message itself
                char msgBuf [msgLength];
                n = readEntireMessage(sockfd, msgBuf, sizeof(msgBuf));
                if (n != msgLength) {
                    cout << "message read error" << endl;
                    close(sockfd);
                    std::lock_guard<std::mutex> lock(_m);
                    _socketTable.erase(sockfd);
                    return;
                }
                std::lock_guard<std::mutex> lock(_m);
                _messageQueue.push(std::string(msgBuf, sizeof(msgBuf)));
                cout << "Got a message of length " << msgLength << endl;
            }
            
        } 
}


/**
 * Background thread routine to accept incoming connection requeuests.
 */
void Messenger::listenerRoutine() {
    //initialize members
    sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(_myPort);

    // create the dedicated listening socket
    if((_listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("\n Error : socket() failed \n");
        exit(EXIT_FAILURE);
    } 
    // to prevent 'bind() failed: address already in use' errors on restarting
    int enable = 1;
    if (setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR,  // todo: sigpipe disable
                   &enable, sizeof(int)) < 0) {
        perror("\n Error : setsockopt() failed \n");
        exit(EXIT_FAILURE);
    }
    if (bind(_listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("\n Error : bind() failed \n");
        exit(EXIT_FAILURE);
    }
    int MAX_BACKLOG_CONNECTIONS = 20;
    if (listen(_listenfd, MAX_BACKLOG_CONNECTIONS) < 0) {
        perror("\n Error : listen() failed \n");
        exit(EXIT_FAILURE);
    }


    // Start actual listener routine
    while(true) {
        int socketfd = accept(_listenfd, nullptr, nullptr); 

        // add this connection to be polled 
        // std::lock_guard<std::mutex> lock(_m);
        // _pfds.push_back({});
        // _pfds.back().fd = connfd;
        // _pfds.back().events = POLLIN;


        // alternative: add a socket manager
        // todo: do this same thing in sendMessage when we want to change socket architecture
        std::lock_guard<std::mutex> lock(_m);
        _socketTable[socketfd] = SocketManagerState{socketfd, {}};

        // start a Messenger class thread that will run the manager routine
        std::thread socketManager(&Messenger::sendAndReceiveMessagesTask, this, &_socketTable[socketfd]);
        socketManager.detach();
        cout << "socket manager thread started" << endl;
    } 
}


/** 
 * Background thread routine to read incoming messages and place them in the 
 * message queue.  
 */
void Messenger::collectMessagesRoutine() {
    while(true) {
        // get a readable fd
        int connfd;
        {
            std::lock_guard<std::mutex> lock(_m);
            int timeout = 100;
            if (poll(&_pfds[0], (nfds_t) _pfds.size(), timeout) <= 0) {
                continue;
            }
            // grab the first readable fd
            for (int i = 0; i < _pfds.size(); ++i) {
                if (_pfds[i].revents & POLLIN) {
                    connfd = _pfds[i].fd;
                    break;
                }
            }
        }

        // read the message length first
        int msgLength;
        int n = readEntireMessage(connfd, &msgLength, sizeof(msgLength));
        if (n != sizeof(msgLength)) {
            continue;
        }
        msgLength = ntohl(msgLength); // convert to host order before use

        // read the message itself
        char msgBuf [msgLength];
        n = readEntireMessage(connfd, msgBuf, sizeof(msgBuf));
        if (n != msgLength) {
            continue;
        }
        std::lock_guard<std::mutex> lock(_m);
        _messageQueue.push(std::string(msgBuf, sizeof(msgBuf)));
        cout << "Got a message of length " << msgLength << endl;
    }
}




/**
 * Send a message to the designated address. Returns true if sent successfully, 
 * or false if the message couldn't be sent. 
 * 
 * If there is not an existing connection to the peer, one will be made. 
 * If sending a message fails, the connection is scrapped and the socket is closed. 
 */
bool Messenger::sendMessage(std::string hostAndPort, std::string message) { 
    // make a connection if it's the first time sending to this address
    int connfd;
    if (!_hostAndPortToFd.count(hostAndPort)) {
        connfd = Messenger::establishConnection(hostAndPort);
        if (connfd == -1) {
            cout << "connection failed to " << hostAndPort << endl;
            return false;
        }
        cout << "connection established with " << hostAndPort << endl;
        _hostAndPortToFd[hostAndPort] = connfd;

        // todo: socket re-architect: use a SocketManager to send the message  

    }
    connfd = _hostAndPortToFd[hostAndPort];

    // todo: determine if we actually need to do this conversion on both ends. 
    int msgLength = htonl(message.length()); 

    // send message length and body
    if ((sendEntireMessage(connfd, &msgLength, sizeof(msgLength)) != 
                                                sizeof(msgLength)) ||      
        (sendEntireMessage(connfd, message.c_str(), message.length()) != 
                                                    message.length())) {

        /* if the message failed to send, assume the socket is dead and close
         * the connection. */
        std::lock_guard<std::mutex> lock(_m);
        close(_hostAndPortToFd[hostAndPort]);
        _hostAndPortToFd.erase(hostAndPort); // map can only contain valid connections
        return false;
    }
    cout << "sent a message with length " << message.length() << endl;
    return true;
}

/** 
 * Return a message if one is available.
 */
std::optional<std::string> Messenger::getNextMessage() {
    std::lock_guard<std::mutex> lock(_m);
    if (_messageQueue.empty()) {
        return std::nullopt;
    }
    std::string message = _messageQueue.front();
    _messageQueue.pop();
    return message;
}


// temporary establish connection method, in terms of "IP:port"
// IP should be in standard IPv4 dotted decimal notation, ex. "127.0.0.95"
// example: "127.0.0.95:8000"
int Messenger::establishConnection(std::string hostAndPort) {
    // parse input string 
    int colonIdx = hostAndPort.find(":");
    std::string IPstr = hostAndPort.substr(0, colonIdx);
    int port = std::stoi(hostAndPort.substr(colonIdx + 1));

    sockaddr_in serv_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; // use IPv4
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(IPstr.c_str()); // translate str IP
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        perror("bad address.");
        return -1;
    }

    // make the connection
    int connfd;
    if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("\n Error : socket() failed \n");
        return -1;
    } 
    if(connect(connfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("\n Error : connect() failed \n");
        return -1;
    } 

    return connfd; 
}

int sendEntireMessage(const int connfd, const void* buf, const int length) { 
    int bytesWritten = 0;
    while (bytesWritten < length) {
        // todo: fix this broken check (disable sigpip, we should be able to write to closed socket)
        int checkEOF = recv(connfd, nullptr, 1, MSG_DONTWAIT);
        if (checkEOF == 0) {
            return -1;
        }
        // flag: disable error signal handling for this call. 
        int n = send(connfd, (char *)buf + bytesWritten, 
                     length - bytesWritten, MSG_NOSIGNAL);
        if (n < 0) {
            return -1; 
        }
        bytesWritten += n;
    }
    return bytesWritten;
}


/** 
 * Read 'bytesToRead' bytes from a peer's socket. Returns the number of bytes
 * read, or -1 if there was an error.
 * 
 * If the peer closed the connection or an error ocurred while reading, 
 * the socket will be closed.
 */
int readEntireMessage(const int connfd, void* buf, int bytesToRead) {
    int bytesRead = 0;
    while (bytesRead < bytesToRead) {
        int n = recv(connfd, (char *)buf + bytesRead, 
                     bytesToRead - bytesRead, MSG_NOSIGNAL);
        // orderly shutdown, or an error ocurred
        if (n == 0 || n < 0) {
            // todo: should remove from polling 
            return -1; 
        }
        bytesRead += n;
    }
    return bytesRead;
}
