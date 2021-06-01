#include "Log.h"
#include "PersistentStorage.h"


/*
    note: This should be perfectly functional. 

    ~ Alternative #1: use simple index/size conversion methods instead of a class
        -it would be less code!
        -but, it would have cognitive load
    
    ~ Alternative #2: do nothing (ie. shift indices around in the existing code)
        -this has identical cons to alternative #1, but is a little worse

    ~ Alternative #3: inherited class
        -a big pro would be if Log doesn't appear anywhere, and instead we only
        have this raft_log. This would reduce cognitive overhead and confusion 
        that there's two logs.
        -big con is that I have to redefine constructor, private members, etc. 

    ~ This class
        -con is that it's confusing that there's this class and then the Log
        - if the Log is a private member and this class isn't people will 
        mistakenly use Log for sure. 
*/


/** 
 * A wrapper class over a 'Log', whose purpose is solely to allow the raft code
 * to work in terms of the log's LOGICAL size and index, which, due to
 * snapshotting, is different than the size and index of the PHYSICAL log. This
 * class peforms the translations so that the programmer may think in terms of
 * the logical indices, not physical ones. This is helpful as servers must
 * communicate in terms of the logical indices. 
 */  
class RaftLog {

    public: 
    struct LogEntry {
        std::string command;
        int term;
    };
        RaftLog(Log<LogEntry>* log, PersistentStorage& ps) : 
            _phys_log(*log), _ps(ps) {};


        /**********************************************************************
        *                          UPDATED LOG API                            *
        ***********************************************************************/


        /**
         * Return the log entry at logical index 'i'. 
         */
        LogEntry operator[](int i) {
            int phys_idx = i - _ps.state().last_included_index();
            if (phys_idx <= 0) {
                LOG_F(INFO, "RaftLog: Error: index out of bounds: i=%d",phys_idx);
            }
            return _phys_log[phys_idx];
        }

        /**
         * Return the physical log size in bytes. 
         */
        int physical_size() const {
            return _phys_log.size();
        }

        /**
         * Return the logical log size in bytes. 
         */
        int size() const { 
            return _phys_log.size() + _ps.state().last_included_index();
        };

        /**
         * Truncate the log to the new logical size 'new_size'. 
         */
        void trunc(int new_size) { 
            _phys_log.trunc(new_size - _ps.state().last_included_index()); 
        }


        /**********************************************************************
        *                        IDENTICAL LOG API                            *
        ***********************************************************************/


        void clear() { _phys_log.clear(); }

        void recover() { 
            /* Note: I've removed the optional offset argument. It orignally was
             * a minor optimization to load only up from 'last_applied' elements
             * into the cache. However, this notion no longer makes sense in the
             * snapshot case, because 'last_applied' must be set to
             * 'last_included_index'. */
            _phys_log.recover(0);
        }

        /**
         * Remove all entries from the start of the log up to and including the
         * entry at index PHYSICAL 'i'. 
         * 
         * Design note: this was determined to be less confusing to than using a
         * logical index after some deliberation. 
         */
        void clip_front(int i) {
            _phys_log.clip_front(i);
        }

        void append(const LogEntry& entry) { _phys_log.append(entry); }

        bool empty() const { return size() == 0; }
        
    private:
        Log<LogEntry>& _phys_log;
        PersistentStorage& _ps;
};