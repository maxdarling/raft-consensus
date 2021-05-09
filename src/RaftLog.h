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
 * A helper wrapper class over a Log. The purpose of this class is to allow 
 * the proceeding raft code to work in terms of the log's LOGICAL size and 
 * index, which, due to snapshotting, is different than the size and index
 * of the physical log. This class peforms the translations so that the programmer
 * may think in terms of the logical indices, not physical ones. This is helpful
 * as servers MUST communicate in terms of the logical indices. 
 */  
class RaftLog {

    public: 
    struct LogEntry {
        std::string command;
        int term;
    };
        RaftLog(Log<LogEntry>* log, PersistentStorage& ps) : 
            _phys_log(*log), _ps(ps) {};

        /* mimicked Log API */
        void clear() { _phys_log.clear(); }

        /* Note: I've removed the optional offset argument. It orignallly was a 
         * minor optimization to load only up from 'last_applied' elements into
         * the cache. However, this no longer makes sense in the snapshot case, 
         * because 'last_applied' must be set to 'last_included_index'. */ 
        void recover() { 
            _phys_log.recover(0);
        }

        void trunc(int new_size) { 
            _phys_log.trunc(new_size - _ps.state().last_included_index()); 
        }

        /** 
         * Note: 'i' is a physical index! 
         * Todo: this should probably be logical for consistency's sake, but 
         * it's unclear how get that to work in Server code (write_snapshot())
         */
        void clip_front(int i) {
            _phys_log.clip_front(i);
        }

        void append(const LogEntry& entry) { _phys_log.append(entry); }

        int size() const { 
            return _phys_log.size() + _ps.state().last_included_index();
        };

        int physical_size() const {
            return _phys_log.size();
        }

        bool empty() const { return size() == 0; }

        LogEntry operator[](int i) {
            int phys_idx = i - _ps.state().last_included_index();
            if (phys_idx <= 0) {
                LOG_F(INFO, "RaftLog: Error: index out of bounds: i=%d",phys_idx);
            }
            return _phys_log[phys_idx];
        }
        
    private:
        Log<LogEntry>& _phys_log;
        PersistentStorage& _ps;
};