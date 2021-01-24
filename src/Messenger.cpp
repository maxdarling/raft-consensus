#include "Messenger.h"



/**
 * Initializes lower-level networking functionality. 
 * 
 * Todo: connect to other file-specified servers
 */
Messenger::Messenger(const int serverId, const vector<serverInfo>& serverList) : 
    _networker(serverList[serverId - 1].addr.sin_port) { // todo: change (1-index?)
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
void Messenger::sendMessage(const int serverId, const Message& message) {

}


/** 
 * Return a message if one is available. If not, return blank. // todo: better -1 return
 * 
 * This method does not block, and therefore is suitable for use in a hot-loop.
 * 
 * todo: add a blocking version, or make a flag available 
 */
Message Messenger::getNextMessage() {

}