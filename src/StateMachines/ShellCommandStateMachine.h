#include "StateMachine.h"
#include <array>

using std::string;

/**
 * A state machine for executing command-line commands.
 */
class ShellCmdStateMachine : public StateMachine {
  public: 
    
    /**
     * Execute a bash command and return its output verbatim as a string. 
     */
    std::string apply(std::string command) {
        string bash_cmd = "bash -c \"" + command + "\"";
        std::unique_ptr<FILE, decltype(&pclose)> pipe(
            popen(bash_cmd.c_str(), "r"), pclose
        );
        string result;
        std::array<char, 128> buf;
        if (!pipe) {
            result = "ERROR: popen() failed";
        }
        else {
            while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr) {
                result += buf.data();
            }
        }
        return result;
    }

    
    /* There's no true state to this state machine as it's too difficult to
     * define the state of a shell environment after a series of commands, so
     * these functions are not implemented meaningfully. */
    bool exportState(string filename) { return true; }
    bool importState(string filename) { return true; }
};