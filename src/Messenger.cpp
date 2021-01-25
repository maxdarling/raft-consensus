#include "Messenger.h"

/* for read() */
#include <unistd.h>


/** 
 * ~ Implementation Notes ~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each message leads with
 * 4 bytes - the size of the protobuf - and then the serialized protobuf. 
 * 
 * 
 * Blocking / non-Blocking Calls: 
 * -todo
 */ 




/**
 * Activates networking functionality and establishes connections to all 
 * other messengers in the given list. 
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

    // connect to other servers
    vector<std::thread> threads;
    for (const serverInfo& elem : serverList) {
        if (elem.serverId != _serverId) {
            // launch a thread to connect to the server
            threads.push_back(std::thread([&] {
                int connfd;
                while( (connfd = _networker->establishConnection(elem.addr)) == -1) {
                    sleep(1);
                }
                _serverIdToFd[elem.serverId] = connfd;
            }));
        }
    }

    // wait till all connections have been made 
    for (int i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }
}


/**
 * Class destructor. 
 */
Messenger::~Messenger() {

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
    // serialize message and its length
    std::string messageBytes = message.SerializeAsString();

    int len = messageBytes.length();
    printf("Sending message of length %d, not to be confused with %d", len, htonl(len));
    len = htonl(len);

    // send the message length, then the message itself
    int connfd = _serverIdToFd[serverId];
    _networker->sendAll(connfd, (char *)&len, sizeof(len)); 
    _networker->sendAll(connfd, messageBytes.c_str(), messageBytes.length());
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
    len = ntohl(len); // convert from network to host order
    printf("Incoming message is %d bytes", len);

    // read the rest of the message
    char msgBuf [len];
    n = read(connfd, msgBuf, sizeof(msgBuf));
    if (n < 0) {
        perror("\n Error : read() failed \n");
        exit(EXIT_FAILURE);
    }
    if (n < 4) {
        perror("\n Error : read() failed to read entire message at once \n"); // todo: fix
        exit(EXIT_FAILURE);
    }

    RPC::container message;
    std::string data(msgBuf, sizeof(msgBuf));
    //message.ParseFromArray(msgBuf, sizeof(msgBuf));
    message.ParseFromString(data);
    return message;
}