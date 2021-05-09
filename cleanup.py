# simple script to cleanup the created files after running raft servers

import os

filenames = {"log!", 
             "log!table", 
             "log!_temp", 
             "log!_temptable", 
             "server_!_snapshot_a", 
             "server_!_snapshot_b", 
             "server!_state"}


for i in range(1,4):
    for filename in filenames:
        filename = filename.replace('!', str(i))
        try: os.remove(filename)
        except: pass
