# raft6
Raft projects for Max Darling and Ali Saeed

Build instructions: 
- Ensure you have cmake (minimum version 3.10) and protocol buffers installed.
- In the root directory, run `cmake -S . -B ./build`
- Next, compile with `make -C ./build`

Configuring the desired IP addresses & ports of the servers in your RAFT cluster:
- By default, RAFT clients & servers will parse the `server_list` file to configure the RAFT cluster. Each line is an <IP, port> pair which should correspond to the address of a RAFT server instance. An IP of `0` signifies the local IP of the current device. 
- A RAFT client will always use the local IP and port 3030.
- `server_list` is default initialized to support 3 server instances numbered 1, 2, and 3 running locally. To increase the number of servers, simply add more lines to the file.

Starting the RAFT cluster:
- For each line `n` in `server_list`, you should run a server instance with `./build/raft_server n` on a device at the address specified on that line.

Starting the RAFT client:
- Invoke `./build/raft_client` and you will be greeted by RASH, the RAFT shell. The first command may take several seconds to process, as the cluster is being set up. Subsequent commands should process quickly.

Terminating the RAFT cluster:
- Servers periodically dump their persistent state in files that look like `server1_state` in the `build` directory, so that they can recover from crashes. If you wish to terminate a cluster session for good and start from scratch the next time the cluster restarts, manually delete these dump files.

Happy RAFTing!

ADVANCED FEATURES
- If you want to specify a custom server address list located `at/some/path`, you can pass that path as the second argument to both `raft_server` and `raft_client`.

CITATIONS
- Code for executing bash command in Server::process_command_routine sourced from: 
https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
