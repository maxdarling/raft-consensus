# raft6
Raft projects for Max Darling and Ali Saeed

Setup Instructions: 
- Ensure you have cmake (minimum version 3.10) and protocol buffers installed
- In the root directory, run 'cmake -S . -B ./build'
- Next, compile with 'make -C ./build'

Configure the IP addresses & ports of the servers in your RAFT cluster:
- By default, RAFT clients & servers will parse the `server_list` file to
configure the RAFT cluster. Each line is an <IP, port> pair which should
correspond to the address of a RAFT server instance. An IP value of `0` 
signifies the local IP of the current device. A RAFT client will always
use the local IP and port 3030.

Run a RAFT server:
- 

Usage Instructions: 
  - Configure the 'server_list' to your liking. Each line corresponds to an 
  expected messenger instance, and contains <IP, port> pairs, where '0'
  corresponds to the local IP.  
  - To start a raft server, run './build/raft_server N' where 'N' is an untaken
    server number in the server list. 
  -To start a raft client, [Ali todo]



CITATIONS
- Code for executing bash command in Server::process_command_routine sourced from: 
https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
