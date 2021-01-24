# raft6
Raft projects for Max Darling and Ali Saeed


Setup Instructions: 
- Ensure you have cmake and protocol buffers installed
- In the root directory, run 'cmake -S . -B ./build'
- Next, compile with 'make -C ./build'


Usage Instructions: 
- To start a raft server, run './build/raft N' where 'N' is the server number
- Repeat this as many times as desired, using distinct server numbers
- The servers will connect to one another automatically. 
- Client requests: [coming soon]

