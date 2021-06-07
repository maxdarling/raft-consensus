#ifndef PERSISTENT_STORAGE_H
#define PERSISTENT_STORAGE_H

#include "RaftPersistentState.pb.h"
#include <fstream>


/**
 * The PersistentStorage class is a wrapper around the ServerPersistentStorage
 * protobuf message, for use in a RAFT server.
 */
class PersistentStorage {
  public:
    /* Specifiy a file to which the persistent state will be backed up. */
    PersistentStorage(std::string file_name) : storage_file(file_name) {}

    /* Return a reference to the protobuf message. */
    ServerPersistentState& state() { return sps; }

    /* Serialize the current state to the file specified at construction. */
    void save()
    {
        std::ofstream ofs(storage_file, std::ios::trunc | std::ios::binary);
        if (!ofs || !sps.SerializeToOstream(&ofs)) {
            throw PersistentStorage::Exception("PersistentStorage.h: " + 
                                                std::string(strerror(errno)));
        }
        ofs.close();
    }

    /* Deserialize the state from the file specified at construction. */
    void recover()
    {
        std::ifstream ifs(storage_file, std::ios::binary);
        if (!ifs || !sps.ParseFromIstream(&ifs)) {
            // give up if we can't open the recovery file
            throw PersistentStorage::Exception("PersistentStorage.h: " + 
                                                std::string(strerror(errno)));
        }
        ifs.close();
    }

  private:
    ServerPersistentState sps;
    std::string storage_file;



  public: 
    /* Exception class for persistent storage. */
    class Exception : public std::exception {
        private:
            std::string _msg;
        public:
            Exception(const std::string& msg) : _msg(msg) {}

            virtual const char* what() const noexcept override
            {
                return _msg.c_str();
            } 
    };
};

#endif /* !PERISTENT_STORAGE_H */
