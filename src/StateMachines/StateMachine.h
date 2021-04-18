#ifndef STATEMACHINE_H
#define STATEMACHINE_H
#include <string> 

/** 
 * A simple abstract class for a state machine. Users may apply commands to the
 * state machine, each prompting an internal state transition and the return of
 * an output in string form.
 * 
 * Correct implementations are deterministic: applying the same commands in the
 * same order from the same initial state should always result in the same 
 * end state. 
 * 
 * State machines also must implement a recovery mechanism in the form of 
 * 'exportState' and 'importState'. 
 */ 
class StateMachine {
  public: 
    virtual std::string apply(std::string command) = 0;

    /**
     * Write the current state to a new file called 'filename', blocking 
     * until completion. Must be compatible for use with 'importState'. 
     *
     * Returns true if the export was successful, false otherwise. 
     */
    virtual bool exportState(std::string filename) = 0;

    /** 
     * Overwrite the current state machine's state with the exported state at
     * 'filename'. The resulting state should be identical to that when the 
     * export was written. 
     * 
     * Returns true if the import was successful, false otherwise. 
     */
    virtual bool importState(std::string filename) = 0;
};
#endif /* !STATEMACHINE_H */