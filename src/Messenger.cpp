#include "Messenger.h"

/* for 'close()', 'sleep()' */
#include <unistd.h>

#include <iostream>

using std::cout;
using std::endl;

const static int SHADOW_MESSAGE_ID = -190;

bool sockaddr_in_cmp(const sockaddr_in a, const sockaddr_in b);

/** 
 * ~~~~~~ Design Notes ~~~~~~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each communication leads
 * with 4 bytes - denoting message length - and follows with the message itself.
 * 
 * Server re-connection:
 * To implement post-crash reconnection, servers send special internal 
 * messages to all other servers on initialization. These are dubbed "shadow 
 * messages", and contain only the sender's serverId. When received, they 
 * indicate that the sending server is running, and wants to be connected to.
 * For simplicity, these messages are always sent on initialization, whether
 * the server is starting for the "first time" or re-joining post-crash.
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
            cout << "readAll failed, message collection aborted" << endl;
            continue;
        }
        msgLength = ntohl(msgLength); // convert back to host order before using 

        // handle shadow message
        if (msgLength == SHADOW_MESSAGE_ID) {
            // protocol: read the peer's 4-byte ID 
            int peerServerId;
            n = _networker->readAll(connfd, &peerServerId, sizeof(peerServerId));
            if (n != sizeof(peerServerId)) {
                cout << "collectMessagesRoutine(): aborting message" << endl;
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
            cout << "successful ~~SHADOW~~ connection to server " << peerServerId << endl;
        }

        // handle default message 
        else {
            char msgBuf [msgLength];
            n = _networker->readAll(connfd, msgBuf, sizeof(msgBuf));
            if (n != msgLength) {
                cout << "readAll failed, message collection aborted" << endl;
                continue;
            }
            std::lock_guard<std::mutex> lock(_m);
            _messageQueue.push(std::string(msgBuf, sizeof(msgBuf)));
        }
    }
}


/**
 * Establishes connections to all other messenger servers in the given list.
 * Waits indefinitely until all outgoing connections are established. 
 * 
 * Note: 'clientPort' should be in host-byte order. 
 */
Messenger::Messenger(const int serverId, const unordered_map<int, struct sockaddr_in>& serverList, 
                     bool isClient, int clientPort) {
    _serverId = serverId;
    _serverIdToAddr = serverList;
    _clientAddrToFd = map<sockaddr_in, int, decltype(sockaddr_in_cmp)*>(&sockaddr_in_cmp);

    // start a networker on our assigned port
    int port = (isClient) ? clientPort : ntohs(_serverIdToAddr[_serverId].sin_port);
    _networker = new Networker(port);

    // start message collection background thread
    std::thread th(&Messenger::collectMessagesRoutine, this);
    th.detach();

    // client doesn't opt to connect to all other messengers, and it cannot send 
    // send shadow messages 
    if (isClient) {
        return;
    }

    // connect to other servers
    for (const auto& [peerId, peerAddr] : serverList) {
        if (peerId != _serverId) {
            int connfd;
            while( (connfd = _networker->establishConnection(peerAddr)) == -1) {
                cout << "failed to connect to server #" << peerId << endl;
                sleep(3);
            }
            cout << "successfully connected to server #" << peerId << endl;
            _serverIdToFd[peerId] = connfd;

            // send a shadow message to this peer
            int msg = htonl(_serverId); 
            char buf [4];
            memcpy(buf, &msg, sizeof(msg));
            _sendMessage(peerId, std::string(buf, sizeof(buf)), true, false, {});
        }
    }
    cout << "Outbound connections completed on server " << _serverId << endl;
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
bool Messenger::_sendMessage(const int serverId, std::string message, bool isShadowMsg,
                             bool isIntendedForClient, sockaddr_in clientAddr) {
    // for now: only manage closed connections for servers, not clients
    if (!isIntendedForClient && _closedConnections.count(serverId)) {
        cout << "sendMessage(): server #" << serverId << " is bogus, or we closed it" << endl;
        return false;
    }

    int msgLength = (isShadowMsg) ? SHADOW_MESSAGE_ID : message.length();
    msgLength = htonl(msgLength); // convert to network order before sending

    int connfd = (isIntendedForClient) ? _clientAddrToFd[clientAddr] : _serverIdToFd[serverId];

    // send message length and body
    if (_networker->sendAll(connfd, &msgLength, sizeof(msgLength)) != sizeof(msgLength) ||
        _networker->sendAll(connfd, message.c_str(), message.length()) != message.length()) {
        cout << "sendMessage(): closing connection to server #" << serverId << endl;
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
 * todo: deprecate
 * public version of '_sendMessage()', without shadow messages. Returns true if
 * the message was sent successfully. 
 * 
 * Note: the method is wrapped to prevent information leakage. We want
 * Messengers to additionally USE the message-sending functionality they
 * publicly implement, but for internal purposes (ie. shadow messages).
 */
bool Messenger::sendMessage(const int serverId, std::string message) {
    return _sendMessage(serverId, message, false, false, {});
}

/**
 * Message sending interface for the client. Returns true if the 
 * message was sent successfully. 
 * 
 * This method also handles network connections automatically, since the client
 * doesn't connect in the constructor. 
 * 
 * Note: instead of shadow messages, this method has the client handle crashed
 * servers by simply re-attempting to connect. 
 */
bool Messenger::sendMessageToServer(const int serverId, std::string message) {
    // if no connection exists, or it's been closed before, attempt to create one
    if (!_serverIdToFd.count(serverId) || _closedConnections.count(serverId)) {
        int connfd = _networker->establishConnection(_serverIdToAddr[serverId]);
        if (connfd == -1) {
            cout << "sendMessageToServer(): couldn't connect to server #" << serverId << endl;
            return false;
        }
        _serverIdToFd[serverId] = connfd;
        _closedConnections.erase(serverId);
    }

    return _sendMessage(serverId, message, false, false, {});
}


/**
 * Message sending interface for servers to clients. Returns true if the 
 * message was sent successfully. 
 *  
 * Raft servers will parse out the client's addr, and use this method to 
 * send back to it. Outbound connections will be made inside this method. 
 * Also, we want to reuse sockets, so we'll store the fd's returned by 
 * establishConnection with the client. 
 * 
 * Note: currently we don't have a '_closedConnections' equivalent for client
 * addresses, but hopefully it's not needed? It probably isn't for raft, but 
 * is for this class. We should test that.   
 */ 
bool Messenger::sendMessageToClient(const sockaddr_in clientAddr, std::string message) {
    if (!_clientAddrToFd.count(clientAddr)) {
        int connfd = _networker->establishConnection(clientAddr);
        if (connfd == -1) {
            cout << "sendMessageToClient(): couldn't connect to client " << endl;
            return false;
        }
        _clientAddrToFd[clientAddr] = connfd;  
    }

    // we need to let '_sendMessage' know to not use serverId's, but use our 'clientAddr' 
    return _sendMessage(-1, message, false, true, clientAddr);
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