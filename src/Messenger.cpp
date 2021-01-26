#include "Messenger.h"

/* for read() */
#include <unistd.h>

#include <iostream>
using std::cout;
using std::endl;


/** 
 * ~ Implementation Notes ~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each communication leads
 * with 4 bytes - denoting message length - and follows with the message itself.
 * 
 * 
 * Blocking / non-Blocking Calls: 
 * -todo: determine which we aim to provide
 * 
 * Automatic connection maintenance, message dropping policy, exception handling:
 * -todo: decide what to do and where (networker / messenger) for the above 
 */ 




/**
 * Establishes connections to all other messengers in the given list.
 * 
 * Todo: make connecting robust 
 */
Messenger::Messenger(const int serverId, const vector<serverInfo>& serverList) {
    _serverId = serverId;

    // find ourselves in the list, initialize networker
    int port;
    for (const serverInfo& elem : serverList) {
        if (elem.serverId == _serverId) {
            port = ntohs(elem.addr.sin_port);
        }
    }
    _networker = new Networker(port);

    cout << "Begin server connection loop inside Messenger" << endl;
    // connect to other servers
    for (const serverInfo& elem : serverList) {
        if (elem.serverId != _serverId) {
            int connfd;
            while( (connfd = _networker->establishConnection(elem.addr)) == -1) {
                //cout << "connection to " << elem.addr.sin_port << " failed. " << endl;
                sleep(3);
            }
            //cout << "connection to " << elem.addr.sin_port << " succeeded!" << endl;
            _serverIdToFd[elem.serverId] = connfd;
            sleep(5);
        }
    }
    cout << "messenger map contents are: " << endl;
    for (auto it = _serverIdToFd.begin(); it != _serverIdToFd.end(); ++it) {
        cout << it->first<< ", " << it->second << endl;
    }
    sleep(5);
    // wait till all connections have been made 
    // note: the above is not guaranteed to work, it's just likely to...
    cout << "Outbound connections completed on server " << _serverId << endl;
}


/**
 * Class destructor. 
 */
Messenger::~Messenger() {
   cout << "REACHED MESSENGER DESTRUCTOR" << endl; 
}

/**
 * Send a message to the specified server. 
 * 
 * This method blocks until the entire message has been sent. 
 * 
 * todo: consider a way around indefinite blocking like using 
 * a timeout or spawning a thread to send the message. 
 */
void Messenger::sendMessage(const int serverId, const RPC::container& message) {
    assert(_serverIdToFd.count(serverId));
    // serialize message and its length
    std::string messageBytes = message.SerializeAsString();

    int len = messageBytes.length();
    printf("Sending message of length %d, not to be confused with %d", len, htonl(len));
    len = htonl(len); // convert to network order before sending

    // send the message length, then the message itself
    int connfd = _serverIdToFd[serverId];
    _networker->sendAll(connfd, (char *)&len, sizeof(len)); 
    cout << "sent msg length" << endl;
    _networker->sendAll(connfd, messageBytes.c_str(), messageBytes.length());
    cout << "sent msg body" << endl;
}


/** 
 * Return a message if one is available. If not, return blank. // todo: better -1 return
 * 
 * This method does not block, and therefore is suitable for use in a hot-loop.
 * 
 * todo: add a blocking version, or make a flag available 
 */
std::optional<RPC::container> Messenger::getNextMessage() {
    int connfd;
    if ( (connfd = _networker->getNextReadableFd()) == -1) {
        return std::nullopt;
    }

    // read the message bytes first
    int len;
    int n = read(connfd, &len, sizeof(len));
    if (n < 0) {
        perror("\n Error : read() failed \n");
        exit(EXIT_FAILURE);
    }
    if (n < 4) {
        perror("\n Error : read() failed to read 4-byte length \n");
        exit(EXIT_FAILURE);
    }
    len = ntohl(len); // convert back to host order before using 
    printf("Incoming message is %d bytes", len);

    // read the rest of the message
    char msgBuf [len];
    n = read(connfd, msgBuf, sizeof(msgBuf));
    if (n < 0) {
        perror("\n Error : read() failed \n");
        exit(EXIT_FAILURE);
    }
    if (n < len) {
        perror("\n Error : read() failed to read entire message at once \n"); // todo: fix
        exit(EXIT_FAILURE);
    }

    RPC::container message;
    message.ParseFromArray(msgBuf, sizeof(msgBuf));
    return message;
}