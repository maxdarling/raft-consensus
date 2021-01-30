#include "Messenger.h"

/* for read() */
#include <unistd.h>

#include <iostream>
using std::cout;
using std::endl;

const static int SHADOW_MESSAGE_ID = -190;

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
 * 
 * 
 * Todo:
 * -define a special messenger-internal message protocol (likely <special_flag, serverId>) for crashed servers
 * -change constructor to be based in terms of these messages (later: impose ordering for fun?)
 *      -note: this will auto-fix a crashed server trying to reconnect since constructor.  
 * -add documentation in header file about shadow messages, background thread
 * -properly handle errors (failed writes, reads. close sockets, close conns? (or just ignore :) ))
 * -improve naming scheme (connfd from 'getNextReadableFd' is bad, perhaps. only from 'establishConnection'?)
 * -see if we don't have to convert serverList ports to network order before passing into messenger
 *  -> to do so, gotta change networker estConn() and constructor, perhaps
 */ 


/** 
 * Background thread routine that will await incoming messages and process
 * them. Shadow messages will be prompt a network connection attempt, while
 * client messages will be added to the message queue. 
 *
 */
void Messenger::collectMessagesRoutine() {
    while(true) {
        int connfd;
        // while ( (connfd = _networker->getNextReadableFd()) == -1) {
        //     // keep waiting until we can read a new message
        // }
        connfd = _networker->getNextReadableFd(true); // todo: perhaps use this later

        // read the message length first
        int len;
        int n = _networker->readAll(connfd, &len, sizeof(len));
        if (n < sizeof(len)) {
            // perror("\n Error : read() failed to read 4-byte length \n");
            // exit(EXIT_FAILURE);
            continue;
        }
        len = ntohl(len); // convert back to host order before using 
        printf("Incoming message is %d bytes \n", len);

        // check for shadow message
        if (len == SHADOW_MESSAGE_ID) {
            // protocol: read the peer's 4-byte ID 
            int peerServerId;
            n = _networker->readAll(connfd, &peerServerId, sizeof(peerServerId));
            if (n < sizeof(peerServerId)) {
                // perror("\n Error : read() failed to read 4-byte length \n");
                // exit(EXIT_FAILURE);
                continue;
            }
            peerServerId = ntohl(peerServerId);
            printf("Messenger conn request from server %d \n", peerServerId);

            // update the connection to the peer 
            if (_serverIdToFd.count(peerServerId)) {
                close(_serverIdToFd[peerServerId]);
            }
            _serverIdToFd[peerServerId] =
                _networker->establishConnection(_serverIdToAddr[peerServerId]);
        }
        // not a shadow message, but a peer message
        else {
            // read the rest of the message
            char msgBuf [len];
            n = _networker->readAll(connfd, msgBuf, sizeof(msgBuf));
            if (n < len) {
                // perror("\n Error : read() failed to read entire message at once \n"); // todo: fix
                // exit(EXIT_FAILURE);
                continue;
            }

            std::lock_guard<std::mutex> lock(_m);
            std::string message(msgBuf, sizeof(msgBuf));
            _messageQueue.push(message);
        }
    }
}


/**
 * Establishes connections to all other messengers in the given list.
 * 
 * Todo: make connecting robust 
 */
Messenger::Messenger(const int serverId, const unordered_map<int, struct sockaddr_in>& serverList) {
    _serverId = serverId;
    _serverIdToAddr = serverList;
    // start a networker on our assigned port
    int port = ntohs(_serverIdToAddr[_serverId].sin_port);
    _networker = new Networker(port);

    // start message collection background thread
    std::thread th(&Messenger::collectMessagesRoutine, this);
    th.detach();

    cout << "Begin server connection loop inside Messenger" << endl;
    // connect to other servers
    for (const auto& [peerId, peerAddr] : serverList) {
        if (peerId != _serverId) {
            int connfd;
            while( (connfd = _networker->establishConnection(peerAddr)) == -1) {
                cout << "failed to connect to peer messenger #" << peerId << endl;
                sleep(3);
            }
            cout << "successfully connected to peer messenger #" << peerId << endl;
            _serverIdToFd[peerId] = connfd;
            sleep(5);
        }
    }
    cout << "messenger map contents are: " << endl;
    for (auto it = _serverIdToFd.begin(); it != _serverIdToFd.end(); ++it) {
        cout << it->first<< ", " << it->second << endl;
    }
    sleep(5);
    // wait till all connections have been made 
    // note: the timer approach is not guaranteed to work, but likely to...
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
void Messenger::sendMessage(const int serverId, std::string message) {
    assert(_serverIdToFd.count(serverId));

    // serialize message and its length
    int len = message.length();
    printf("Sending message of length %d, not to be confused with %d \n", len, htonl(len));
    len = htonl(len); // convert to network order before sending

    // send the message length, then the message itself
    int connfd = _serverIdToFd[serverId];
    _networker->sendAll(connfd, &len, sizeof(len)); // todo: catch return value
    cout << "sent msg length" << endl;
    _networker->sendAll(connfd, message.c_str(), message.length());
    cout << "sent msg body" << endl;
}


/** 
 * Return a message if one is available. If not, return blank.
 * 
 * This method does not block, and therefore is suitable for use in a hot-loop.
 * 
 * todo: add a blocking version, or make a flag available 
 */
std::optional<std::string> Messenger::getNextMessage() {
    // check the message queue 
    std::lock_guard<std::mutex> lock(_m);
    if (_messageQueue.empty()) {
        return std::nullopt;
    }
    std::string message = _messageQueue.front();
    _messageQueue.pop();
    return message;
}