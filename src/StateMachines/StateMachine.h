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
 */ 
class StateMachine {
  public: 
    virtual std::string apply(std::string command) = 0;
};
#endif /* !STATEMACHINE_H */