#include "Messenger.h"

/* for 'close()', 'sleep()' */
#include <unistd.h>

#include <iostream>

const static int SHADOW_MESSAGE_ID = -190;

bool sockaddr_in_cmp(const sockaddr_in a, const sockaddr_in b);

/** 
 * ~~~~~~ Design Notes ~~~~~~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each communication leads
 * with 4 bytes - denoting message length - and follows with the message
 * itself.
 * 
 * Server re-connection: 
 * To implement post-crash reconnection, servers send special internal messages
 * to all other servers on initialization. These are dubbed "shadow messages",
 * and contain only the sender's serverId. When received, they indicate that
 * the sending server is running, and wants to be connected to. For simplicity,
 * these messages are always sent on initialization, whether the server is
 * starting for the "first time" or re-joining post-crash.
 */ 


/** 
 * Background thread routine to process incoming messages.
 * 
 * Client messages will be placed in the message queue, while shadow messages
 * will prompt a network connection to the sender.
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

        // handle shadow message
        if (msgLength == SHADOW_MESSAGE_ID) {
            // protocol: read the peer's 4-byte ID 
            int peerServerId;
            n = _networker->readAll(connfd, &peerServerId, 
                                    sizeof(peerServerId));
            if (n != sizeof(peerServerId)) {
                continue;
            }
            peerServerId = ntohl(peerServerId);

            // update the outbound connection to the peer 
            if (_serverIdToFd.count(peerServerId) && 
                !_closedConnections.count(peerServerId)) {
                close(_serverIdToFd[peerServerId]);
            }
            _serverIdToFd[peerServerId] =
                _networker->establishConnection(_serverIdToAddr[peerServerId]);
            _closedConnections.erase(peerServerId);
        }

        // handle default message 
        else {
            char msgBuf [msgLength];
            n = _networker->readAll(connfd, msgBuf, sizeof(msgBuf));
            if (n != msgLength) {
                continue;
            }
            std::lock_guard<std::mutex> lock(_m);
            _messageQueue.push(std::string(msgBuf, sizeof(msgBuf)));
        }
    }
}


/**
 * Server constructor. 
 * Establishes connections to all other messenger servers in the given list.
 * Waits indefinitely until all outgoing connections are established. 
 */
Messenger::Messenger(const int serverId, 
                     const unordered_map<int, sockaddr_in>& serverList) {
    _isClient = false;
    _serverId = serverId;
    _serverIdToAddr = serverList;
    _clientAddrToFd = 
        map<sockaddr_in, int, decltype(sockaddr_in_cmp)*>(&sockaddr_in_cmp);

    // start a networker on our assigned port
    int port = ntohs(_serverIdToAddr[_serverId].sin_port);
    _networker = new Networker(port);

    // start message collection background thread
    std::thread th(&Messenger::collectMessagesRoutine, this);
    th.detach();

    // connect to other servers
    for (const auto& [peerId, peerAddr] : serverList) {
        if (peerId != _serverId) {
            int connfd;
            while(-1 == (connfd = _networker->establishConnection(peerAddr))) {
                sleep(3);
            }
            _serverIdToFd[peerId] = connfd;

            // send a shadow message to this peer
            int msg = htonl(_serverId); 
            char buf [4];
            memcpy(buf, &msg, sizeof(msg));
            _sendMessage(peerId, std::string(buf, sizeof(buf)), true);
        }
    }
}


/**
 * Client constructor. 
 * 
 * Note: 'clientPort' should be in host-byte order. 
 */
Messenger::Messenger(const unordered_map<int, sockaddr_in>& serverList, 
                     const int clientPort) {
    _isClient = true;
    _serverIdToAddr = serverList;
    _clientAddrToFd = 
        map<sockaddr_in, int, decltype(sockaddr_in_cmp)*>(&sockaddr_in_cmp);

    // start a networker on our assigned port
    _networker = new Networker(clientPort);

    // start message collection background thread
    std::thread th(&Messenger::collectMessagesRoutine, this);
    th.detach();
}


/**
 * Class destructor. 
 */
Messenger::~Messenger() {
    free(_networker);
}

/**
 * Send a message to the specified messenger. Returns true if the message was
 * sent successfully.  
 * 
 * Errors and closed connections are dealt with automatically.  
 */
bool Messenger::_sendMessage(const int serverId, std::string message, 
                             bool isShadowMsg = false,
                             bool isIntendedForClient = false, 
                             sockaddr_in clientAddr = {}) {
    // for now: only manage closed connections for servers, not clients
    if (!isIntendedForClient && _closedConnections.count(serverId)) {
        return false;
    }

    int msgLength = (isShadowMsg) ? SHADOW_MESSAGE_ID : message.length();
    msgLength = htonl(msgLength); // convert to network order before sending

    int connfd = (isIntendedForClient) ? 
        _clientAddrToFd[clientAddr] : 
        _serverIdToFd[serverId];

    // send message length and body
    if ((_networker->sendAll(connfd, &msgLength, sizeof(msgLength)) != 
                                                sizeof(msgLength)) ||      
        (_networker->sendAll(connfd, message.c_str(), message.length()) != 
                                                     message.length())) {
        if (isIntendedForClient) {
            close(_clientAddrToFd[clientAddr]);
        } else {
            close(_serverIdToFd[serverId]);
            _closedConnections.insert(serverId);
        }
        return false;
    }
    return true;
}


/**
 * Send a message to a server, as a server OR client. Returns true if the 
 * message was sent successfully.
 */
bool Messenger::sendMessageToServer(const int serverId, std::string message) {
    /* client case: client network connections are made here on-demand */
    if (_isClient && 
        !_serverIdToFd.count(serverId) || _closedConnections.count(serverId)) {
        int connfd = _networker->establishConnection(_serverIdToAddr[serverId]);
        if (connfd == -1) {
            return false;
        }
        _serverIdToFd[serverId] = connfd;
        _closedConnections.erase(serverId);
    }

    return _sendMessage(serverId, message);
}


/**
 * Send a message as a server to a client. Returns true if the 
 * message was sent successfully. 
 * 
 * Note: theoretically, messages may be sent between clients, although not 
 * initially intended. 
 */ 
bool Messenger::sendMessageToClient(const sockaddr_in clientAddr, 
                                    std::string message) {
    if (!_clientAddrToFd.count(clientAddr)) {
        int connfd = _networker->establishConnection(clientAddr);
        if (connfd == -1) {
            return false;
        }
        _clientAddrToFd[clientAddr] = connfd;  
    }

    return _sendMessage(0, message, false, true, clientAddr);
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


// custom comparator for '_clientAddrToFd' map
bool sockaddr_in_cmp(const sockaddr_in a, const sockaddr_in b) {
    if (a.sin_port != b.sin_port) {
        return a.sin_port < b.sin_port;
    }
    return a.sin_addr.s_addr < b.sin_addr.s_addr;
}