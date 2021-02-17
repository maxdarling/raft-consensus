#include "Messenger.h"

/* low-level networking */
#include <arpa/inet.h>
#include <condition_variable>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h> 

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

    sleep(5);
    cout << "finished messenger constructor "<< endl;
}


/**
 * Class destructor. 
 */
Messenger::~Messenger() {
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
    if (setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR,
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
        int sockfd = accept(_listenfd, nullptr, nullptr); 
        if (sockfd == -1) {
            perror("accept failed\n");
        }
        // start a reader for this socket's lifetime
        std::thread reader(&Messenger::receiveRequestsTask, this, sockfd);
        reader.detach();
    } 
}


/**
 * worker task: read an entire message on a socket. dispatched by poller when a message
 * is detected. 
 */
void Messenger::receiveRequestsTask(int sockfd) {
    while(true) {
        int msgLength;
        int n = readEntireMessage(sockfd, &msgLength, sizeof(msgLength));
        if (n != sizeof(msgLength)) {
            cout << "message read error: " << " read " << n << " bytes, expected " << sizeof(msgLength) << endl;
            close(sockfd);
            return;
        }

        // read the message itself
        char msgBuf [msgLength];
        n = readEntireMessage(sockfd, msgBuf, sizeof(msgBuf));
        if (n != msgLength) {
            cout << "message read error: " << " read " << n << " bytes, expected " << msgLength << endl;
            close(sockfd);
            return;
        }
        _messageQueue.blockingPush(std::string(msgBuf, sizeof(msgBuf)));
        cout << "Got a message of length " << msgLength << endl;
    }
}

/**
 * worker task: continually send messages when they're put in your queue. 
 */
void Messenger::sendRequestsTask(int sockfd) {
    // todo: these accesses are vulnerable to the overall map being reallocated, no? 
    BlockingQueue<std::string>& outBoundMessages = _socketToSenderState[sockfd]->outboundMessages;
    std::string& hostAndPort = _socketToSenderState[sockfd]->hostAndPort;

    while(true) {
        // wait for a message to be ready to sent
        std::string message = outBoundMessages.blockingPop();

        int msgLength = message.length();

        // send message length and body
        if ((sendEntireMessage(sockfd, &msgLength, sizeof(msgLength)) != 
                                                    sizeof(msgLength)) ||      
            (sendEntireMessage(sockfd, message.c_str(), message.length()) != 
                                                        message.length())) {

            /* if the message failed to send, we don't close the socket and let the 
            reader to that instead. however, we do remove this entry from the map. 
            that way, we know to try creating a new connection to this peer the next
            time we try to send a message. 
            
            note: this is also safe no matter how fast/slow the reader is. if it 
            closes before the map, great. if it closes after, the socket is still open,
            meaning that a map entry for it cannot be created and overwrite us.  
            */
            std::lock_guard<std::mutex> lock(_m);
            _hostAndPortToFd.erase(hostAndPort); // map can only contain live connections
            cout << "sender: message failed to send. aborting connection to " << hostAndPort << endl;
            return;
        }
        cout << "sender: sent a message with length " << message.length() << endl;
    }
}


/**
 * Send a message to the designated address. 
 * 
 * Returns true if the connection was healthy for the send, false if the 
 * connection cound't be made. This method does NOT return true if the message
 * was sent successfully - it instead operates on a best-effort basis. 
 * 
 * If there is not an existing connection to the peer, one will be made. 
 * If sending a message fails, the connection is scrapped and the socket is closed. 
 */
bool Messenger::sendRequest(std::string hostAndPort, std::string message) { 
    // make a connection if it's the first time sending to this address
    int sockfd;
    std::lock_guard<std::mutex> lock(_m);
    if (!_hostAndPortToFd.count(hostAndPort)) {
        sockfd = Messenger::establishConnection(hostAndPort);
        if (sockfd == -1) {
            cout << "connection failed to " << hostAndPort << endl;
            return false;
        }
        cout << "connection established with " << hostAndPort << endl;
        _hostAndPortToFd[hostAndPort] = sockfd;

        // create a message sender for this socket's lifetime
        _socketToSenderState[sockfd] = new SenderState{{}, hostAndPort}; 
        std::thread sender(&Messenger::sendRequestsTask, this, sockfd);
        sender.detach();
    }
    sockfd = _hostAndPortToFd[hostAndPort];


    // pass the message to the socket's desigated sender 
    _socketToSenderState[sockfd]->outboundMessages.blockingPush(message);
    return true;
}

/** 
 * Return a message if one is available.
 */
std::optional<std::string> Messenger::getNextRequest(int timeout) {
    std::optional<std::string> msgOpt = _messageQueue.blockingPop_timed(timeout);

    if (msgOpt) {
        return *msgOpt;
    } else {
        return std::nullopt;
    }
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


/** 
 * Send 'length' bytes from a socket. Returns the number of bytes
 * read, or -1 if there was an error.
 */
int sendEntireMessage(const int connfd, const void* buf, const int length) { 
    int bytesWritten = 0;
    while (bytesWritten < length) {
        // 'MSG_NOSIGNAL': disable sigpipe in case write fails
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
 * Read 'bytesToRead' bytes from a socket. Returns the number of bytes
 * read, or -1 if there was an error.
 */
int readEntireMessage(const int connfd, void* buf, int bytesToRead) {
    int bytesRead = 0;
    while (bytesRead < bytesToRead) {
        int n = recv(connfd, (char *)buf + bytesRead, 
                     bytesToRead - bytesRead, MSG_NOSIGNAL);
        // orderly shutdown, or an error ocurred
        if (n == 0 || n < 0) {
            return -1; 
        }
        bytesRead += n;
    }
    return bytesRead;
}
