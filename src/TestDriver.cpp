#include <stdio.h>
#include <poll.h>
#include <thread>
#include <iostream>
#include <unistd.h>

#include "Messenger.h"

#include "Log.h"

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
void server_server(int serverNumber) {
    // start Messenger
    int myPort = PORT_BASE + serverNumber;
    Messenger messenger(myPort);

    int nServers = 2;
    cout << "Server #" << serverNumber << " of " << nServers << " has started" << endl;

    while(true) {
        int peerServerNumber = (serverNumber == 1 ? 2 : 1);
        std::string peerHostAndPort = "0:" + std::to_string(PORT_BASE + peerServerNumber);
        

        // get any responses
        std::optional<std::string> responseOpt = messenger.getNextResponse(100);
        if (responseOpt) {
            cout << "Response received: " << endl;
            cout << *responseOpt << endl;
        } else {
            cout << "No response recieved in time" << endl;
        }
        
        // send a request
        messenger.sendRequest(peerHostAndPort, "this is a request\n"); 


        // check for requests
        int timeoutMs = 100;
        std::optional<Messenger::Request> requestOpt = messenger.getNextRequest(timeoutMs);
        if (requestOpt) {
            cout << "Request received:" << endl;
            cout << (requestOpt->message) << endl; 
            requestOpt->sendResponse("this is a response\n");
        } else {
            cout << "No request received in time" << endl;
        }
    
        sleep(3);
    }
}


/* tests client and server instance. 
    
    client is server 1, server is server2
*/
void client_server(int serverNumber) {
    if (serverNumber == 1) {
        Messenger messenger;
        while(true) {
            // get a response if exists
            std::optional<std::string> reqOpt = messenger.getNextResponse(100);
            if (reqOpt) {
                cout << "Response recieved: " << reqOpt.value() << endl;
            }

            // send message to receiver
            messenger.sendRequest("0:5002", "~This is a message~\n");
            cout << "sent request" << endl;
            sleep(3);
        }
    }
    else if (serverNumber == 2) {
        Messenger messenger(5002);
        while(true) {
            // receive message from sender
            std::optional<Messenger::Request> reqOpt = messenger.getNextRequest(100);
            if (reqOpt) {
                cout << "Request recieved: " << reqOpt.value().message;
                // send response
                cout << "about to send response" << endl;
                reqOpt.value().sendResponse("~This is a response\n");
                cout << "sent response" << endl;
            }

            sleep(3);
        }
    }
}


void test_log() {

    // test if "clip_front" works
    Log<int> log(
        "logfile", 
        [](const int& a) { return std::to_string(a); }, 
        [](std::string a) { return std::stoi(a); }
    );

    int size = 2;
    for (int i = 1; i <= size; ++i) {
        log.append(i);
    }

    cout << "log before: ";
    for (int i = 1; i <= log.size(); ++i) {
        cout << log[i] << ", ";
    }
    cout << endl;

    log.clip_front(0);


    cout << "log after: ";
    for (int i = 1; i <= log.size(); ++i) {
        cout << log[i] << ", ";
    }
    cout << endl;
}


int main(int argc, char* argv[])
{
    //int serverNumber = std::stoi(argv[1]);
    //server_server(serverNumber);
    //client_server(serverNumber);

    test_log();

    return 0;
}
