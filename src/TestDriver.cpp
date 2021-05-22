#include <cstring>
#include <memory>
#include <stdio.h>
#include <poll.h>
#include <thread>
#include <iostream>
#include <unistd.h>
#include <filesystem>

#include "Messenger.h"
#include "Log.h"
#include "RaftLog.h"
#include "util.h"
#include "RaftRPC.pb.h"

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <utility>
#include "loguru/loguru.hpp"

using std::cout;
using std::endl;
using std::string;

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

void test_raft_log() {
    string log_file = "logfile";
    PersistentStorage ps("fake_recovery_file");
    RaftLog log (new Log<RaftLog::LogEntry>(log_file, 
        [](const RaftLog::LogEntry &entry) {
            return std::to_string(entry.term) + " " + entry.command;
        },
        [](string entry_str) {
            size_t delimiter_idx = entry_str.find(" ");
            int term = std::stoi(entry_str.substr(0, delimiter_idx));
            string command = entry_str.substr(delimiter_idx + 1);
            return RaftLog::LogEntry {command, term};
        }
    ), ps);

    int size = 10;
    for (int i = 1; i <= size; ++i) {
        log.append({"fake command", i});
    }

    cout << "log before: ";
    for (int i = 1; i <= log.size(); ++i) {
        cout << log[i].term << ", ";
    }
    cout << endl;

    log.clip_front(3);


    cout << "log after: ";
    for (int i = 1; i <= log.size(); ++i) {
        cout << log[i].term << ", ";
    }
    cout << endl;

    std::remove(log_file.c_str());
    std::remove((log_file + "table").c_str());
}

void test_log() {

    auto serialize = [](const int& a) { return std::to_string(a); };
    auto deserialize = [](std::string a) { return std::stoi(a); };
    Log<int> log(
        "logfile", serialize, deserialize 
    );
    string log_file = "logfile";

    int size = 10;
    for (int i = 1; i <= size; ++i) {
        log.append(i);
    }

    cout << "log before: ";
    for (int i = 1; i <= log.size(); ++i) {
        cout << log[i] << ", ";
    }
    cout << endl;

    log.clip_front(3);


    cout << "log after: ";
    for (int i = 1; i <= log.size(); ++i) {
        cout << log[i] << ", ";
    }
    cout << endl;



    std::remove("logfile");
    std::remove("logfiletable");
}


void fileChunkTest() { 
    string outfile = "test_output";

    string path = std::filesystem::current_path();
    std::string filename = path + "/test";
    int chunk_size = 2;
    size_t offset = 0;
    while (true) {
        char buffer [chunk_size];
        memset(buffer, '\0', sizeof(buffer));
        bool eof = readFileChunk(filename, buffer, offset, chunk_size);
        offset += chunk_size;
        cout << "buffer contents: " << string(buffer) << std::endl;
        cout << endl;
        InstallSnapshot* is = new InstallSnapshot();
        is->set_data(string(buffer));


        // append to file
        std::ofstream ofs(outfile, std::ios::app|std::ios::binary);
        //ofs << string(buffer);
        ofs << is->data();
        ofs.close();

        if (eof) {
            break;
        }
    }
}

void filesystem_test() { 
    std::filesystem::path p = std::filesystem::current_path();
    cout << "current path: " << endl << p << endl;

    try {
        std::filesystem::rename(p/"test", p/"twest");
    }
    catch(std::exception& e) {
        cout << e.what() << endl; 
        return;
    }


}


int main(int argc, char* argv[])
{
    //int serverNumber = std::stoi(argv[1]);
    //server_server(serverNumber);
    //client_server(serverNumber);

    //test_log();
    //test_raft_log();
    fileChunkTest();
    //filesystem_test();

    return 0;
}
