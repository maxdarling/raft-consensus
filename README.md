# raft6
Raft projects for Max Darling and Ali Saeed

Setup Instructions: 
- Ensure you have cmake (minimum version 3.10) and protocol buffers installed
- In the root directory, run 'cmake -S . -B ./build'
- Next, compile with 'make -C ./build'

Configure the IP addresses & ports of the servers in your RAFT cluster:
- By default, `server_list`

Run a RAFT server:
- 

Usage Instructions: 
- To start a raft server, run './build/raft N' where 'N' is the server number
- Repeat this as many times as desired, using distinct server numbers
- The servers will connect to one another automatically. 
- Client requests: [coming soon]

CITATIONS
- Code for executing bash command in Server::process_command_routine sourced from https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
