#include <stdio.h>
#include <poll.h>
#include <thread>
#include <iostream>
#include <unistd.h>

#include "Messenger.h"

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include "loguru/loguru.hpp"

using std::cout;
using std::endl;

const int PORT_BASE = 5000;


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ATTENTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    This file should be ignored for CS190 code reviews. 






   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ATTENTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */




/* server numbers: 1 or 2 */
void MessengerTester(int serverNumber) {
    // start Messenger
    int myPort = PORT_BASE + serverNumber;
    Messenger messenger(myPort);

    int nServers = 2;
    cout << "Server #" << serverNumber << " of " << nServers << " has started" << endl;

    // send messages
    while(true) {
        // pick a random server to send a message to
        int peerServerNumber; 
        while ( (peerServerNumber = 1 + (rand() % nServers)) == serverNumber);

        std::string peerHostAndPort = "0:" + std::to_string(PORT_BASE + peerServerNumber);
        
        // construct a message
        std::string requestMessage = "this is a request\n";

        // get any responses
        std::optional<std::string> responseOpt = messenger.getNextResponse(100);
        if (responseOpt) {
            cout << "Response received: " << endl;
            cout << *responseOpt << endl;
        }
        
        // send the message
        messenger.sendRequest(peerHostAndPort, requestMessage);


        // check for received messages
        int timeoutMs = 100;
        std::optional<Messenger::Request> requestOpt = messenger.getNextRequest(timeoutMs);
        if (requestOpt) {
            cout << "Request received:" << endl;
            cout << (requestOpt->message) << endl; 
            // try to send a response, too!
            std::string responseMsg = "this is a reponse\n";
            requestOpt->sendResponse(responseMsg);
        } else {
            cout << "No request message received in time" << endl;
        }
    
        sleep(3);
    }
}


/* one sender (client), one receiver. sender is 1, receiver is 2 */
void MessengerTester2(int serverNumber) {
    if (serverNumber == 1) {
        Messenger messenger;
        while(true) {
            // send message to receiver
            messenger.sendRequest("0:5002", "~This is a message~\n");
            sleep(3);
        }
    }
    else if (serverNumber == 2) {
        Messenger messenger(5002);
        while(true) {
            // receive message from sender
            std::optional<Messenger::Request> reqOpt = messenger.getNextRequest(100);
            if (!reqOpt) {
                cout << "No message received in time " << endl;
            } else {
                cout << "Message recieved: " << reqOpt.value().message;
            }

            sleep(3);
        }
    }
}


int main(int argc, char* argv[])
{
    int serverNumber = std::stoi(argv[1]);
    MessengerTester(serverNumber);
    //MessengerTester2(serverNumber);

    return 0;
}
