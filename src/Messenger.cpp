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
        // while ( (connfd = _networker->getNextReadableFd(false)) == -1) {
        //     // keep waiting until we can read a new message
        // }
        connfd = _networker->getNextReadableFd(true);

        // read the message length first
        int len;
        int n = _networker->readAll(connfd, &len, sizeof(len));
        if (n != sizeof(len)) {
            cout << "readAll failed, so we're aborting this message" << endl;
            continue;
        }
        len = ntohl(len); // convert back to host order before using 
        printf("Incoming message is %d bytes \n", len);

        // check for shadow message
        if (len == SHADOW_MESSAGE_ID) {
            cout << "enter shadow message read handler" << endl;
            // protocol: read the peer's 4-byte ID 
            int peerServerId;
            n = _networker->readAll(connfd, &peerServerId, sizeof(peerServerId));
            if (n != sizeof(peerServerId)) {
                cout << "readAll failed, so we're aborting this message" << endl;
                continue;
            }
            peerServerId = ntohl(peerServerId);
            cout << "received shadow message from server " << peerServerId << endl;

            // update the outbound connection to the peer 
            if (_serverIdToFd.count(peerServerId)) {
                close(_serverIdToFd[peerServerId]);
            }
            _serverIdToFd[peerServerId] =
                _networker->establishConnection(_serverIdToAddr[peerServerId]);
            cout << "successful shadow connection to server " << peerServerId << endl;
        }
        // not a shadow message, but a peer message
        else {
            // read the rest of the message
            char msgBuf [len];
            n = _networker->readAll(connfd, msgBuf, sizeof(msgBuf));
            if (n != len) {
                cout << "readAll failed, so we're aborting this message" << endl;
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
                cout << "failed to connect to server #" << peerId << endl;
                sleep(3);
            }
            cout << "successfully connected to server #" << peerId << endl;
            _serverIdToFd[peerId] = connfd;
            int msg = htonl(_serverId); // shadow message: tell peerd w/ peerId to connect to us at _serverId
            char buf [4];
            memcpy(buf, &msg, sizeof(msg));
            _sendMessage(peerId, std::string(buf, sizeof(buf)), true);
            cout << "sent shadow message to server #" << peerId << endl;
            sleep(5);
        }
    }
    cout << "messenger map contents are: " << endl;
    for (auto it = _serverIdToFd.begin(); it != _serverIdToFd.end(); ++it) {
        cout << it->first<< ", " << it->second << endl;
    }
    sleep(5);
    // wait till all connections have are made (before client may send/receive)
    // note: the timer approach is not guaranteed to work, but likely to...
    cout << "Outbound connections completed on server " << _serverId << endl;
}


/**
 * Class destructor. 
 */
Messenger::~Messenger() {
    free(_networker);
}

/**
 * Send a message to the specified server. 
 * 
 * Errors and closed connections are dealt with automatically.  
 */
void Messenger::_sendMessage(const int serverId, std::string message, bool isShadow) {
    if (!_serverIdToFd.count(serverId)) {
        cout << "sendMessage(): server#" << serverId << " is bogus, or we closed it" << endl;
        return;
    }
    // serialize message and its length
    int len = (isShadow) ? SHADOW_MESSAGE_ID : message.length();
    cout << "sending message of length " << len << endl;
    len = htonl(len); // convert to network order before sending

    // send the message length, then the message itself
    int connfd = _serverIdToFd[serverId];
    int n = _networker->sendAll(connfd, &len, sizeof(len));
    if (n != sizeof(len)) {
        cout << "sendMessage failed, closing conn to server#" << serverId << endl;
        close(_serverIdToFd[serverId]);
        _serverIdToFd.erase(serverId);
        return;
    }
    n = _networker->sendAll(connfd, message.c_str(), message.length());
    if (n != message.length()) {
        cout << "sendMessage failed, closing conn to server#" << serverId << endl;
        close(_serverIdToFd[serverId]);
        _serverIdToFd.erase(serverId);
        return;
    }
}

/**
 * public message sending API. 
 * 
 * See '_sendMessage()' above for details. 
 */
void Messenger::sendMessage(const int serverId, std::string message) {
    _sendMessage(serverId, message, false);
}



/** 
 * Return a message if one is available.
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