#include "Messenger.h"

/* for 'close()', 'sleep()' */
#include <unistd.h>

/* for 'inet_addr()' */
#include <arpa/inet.h>
#include <netinet/in.h>

#include <iostream>
using std::cout, std::endl;

/** 
 * ~~~~~~ Design Notes ~~~~~~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each communication leads
 * with 4 bytes - denoting message length - and follows with the message
 * itself.
 */ 


/** 
 * Background thread routine to read incoming messages and place them in the 
 * message queue.  
 */
void Messenger::collectMessagesRoutine() {
    while(true) {
        int connfd = _networker->getNextReadableFd(true);

        // read the message length first
        int msgLength;
        int n = _networker->readAll(connfd, &msgLength, sizeof(msgLength));
        if (n != sizeof(msgLength)) {
            continue;
        }
        msgLength = ntohl(msgLength); // convert to host order before use

        // read the message itself
        char msgBuf [msgLength];
        n = _networker->readAll(connfd, msgBuf, sizeof(msgBuf));
        if (n != msgLength) {
            continue;
        }
        std::lock_guard<std::mutex> lock(_m);
        _messageQueue.push(std::string(msgBuf, sizeof(msgBuf)));
        cout << "Got a message of length " << msgLength << endl;
    }
}


/**
 * Messenger constructor.
 * 
 * Note: port should be in host-byte order. 
 */
Messenger::Messenger(const int myPort) {
    // start a networker on our assigned port
    _networker = new Networker(myPort);

    // start message collection background thread
    std::thread th(&Messenger::collectMessagesRoutine, this);
    th.detach();

    sleep(5);
    cout << "finished messenger constructor (client)" << endl;
}


/**
 * Class destructor. 
 */
Messenger::~Messenger() {
    free(_networker);
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
    }
    connfd = _hostAndPortToFd[hostAndPort];

    // todo: determine if we actually need to do this conversion on both ends. 
    int msgLength = htonl(message.length()); 

    // send message length and body
    if ((_networker->sendAll(connfd, &msgLength, sizeof(msgLength)) != 
                                                sizeof(msgLength)) ||      
        (_networker->sendAll(connfd, message.c_str(), message.length()) != 
                                                    message.length())) {

        /* if the message failed to send, assume the socket is dead and close
         * the connection. */
        close(_hostAndPortToFd[hostAndPort]);
        _hostAndPortToFd.erase(hostAndPort);
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
