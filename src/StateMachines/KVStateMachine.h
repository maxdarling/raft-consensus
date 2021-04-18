#include "StateMachine.h"
#include <sstream>
#include <unordered_map>
#include <fstream>

using std::string;


/**
 * A state machine implementing a simple key-value store. 
 *
 * Note: As this is a simple prototype, no type checking is provided: keys and
 * values are stored as strings. While the string input - string output
 * interface is somewhat restrictive, features such as 'GET-INT' and
 * 'GET-STRING' may be considered for implementation in the future.
 *
 * Three basic commands are provided, GET, SET, and DELETE:
 *
 * GET: return the value of a currently stored key, 0 otherwise. 
 *  -usage: "GET <varame>"
 *
 * SET: set the value of a variable, creating a new variable if it didn't exist.
 *      Returns the set value. 
 *  -usage: "SET <varname> <value>"
 * 
 * DELETE: make a variable not exist (ie. no effect on non-existent vars).
 *         Returns the name of the deleted variable. 
 *  -usage: "DELETE <varname>"
 */
 
class KVStateMachine : public StateMachine {
  public: 
    /** 
     * Handles the commands as described above. Invalid commands will cause an 
     * error message to be returned. 
    */
    string apply(string command) {
        std::istringstream iss(command);
        string cmd, var;
        if ( !(iss >> cmd && iss && iss >> var && iss) ){
            return "Error: invalid # of arguments";
        }

        if (cmd == "GET") {
            if (!_map.count(var)) {
                return "0";
            }
            return _map[var];
        }
        else if (cmd == "SET") {
            string val;
            iss >> val;
            // even if val is blank, use it (empty string)
            _map[var] = val;
            return val;
        }
        else if (cmd == "DELETE") {
            _map.erase(var);
            return var;
        } else {
            return "Error: invalid command name; not one of GET, SET, DELETE";
        }
    }


    bool exportState(string filename) {
        std::ofstream ofs(filename, std::ios::trunc | std::ios::binary);
        if (!ofs) {
            return false;
        }

        /* simple export scheme: space delimited tokens */
        for (auto &[key, value] : _map) {
            ofs << key << " " << value << " ";
        }
        ofs.close();
        return true;
    }


    bool importState(string filename) {
        std::ifstream ifs(filename, std::ios::binary);
        if (!ifs) {
            return false;
        }

        _map = {};
        string key, value;
        while(ifs >> key >> value) {
            _map[key] = value;
        }
        ifs.close();
        return true;
    }

  private:
    std::unordered_map<string, string> _map;
};