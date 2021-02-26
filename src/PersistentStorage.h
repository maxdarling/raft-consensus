#include "RaftRPC.pb.h"
#include <fstream>

// exception class for persistent storage
class PersistentStorageException : public std::exception {
    private:
        std::string _msg;
    public:
        PersistentStorageException(const std::string& msg) : _msg(msg){}

        virtual const char* what() const noexcept override
        {
            return _msg.c_str();
        } 
};

class PersistentStorage {
  public:
    PersistentStorage(std::string file_name) : storage_file(file_name) {}

    ServerPersistentState& state() { return sps; }

    void save()
    {
        std::ofstream ofs(storage_file, std::ios::trunc | std::ios::binary);
        if (!ofs || !sps.SerializeToOstream(&ofs)) {
            // give up if we're unable to write
            throw PersistentStorageException("fatal error: can't write "
                                             "recovery file");
        }
        ofs.close();
    }

    void recover()
    {
        std::ifstream ifs(storage_file, std::ios::binary);
        if (!ifs || !sps.ParseFromIstream(&ifs)) {
            // give up if we can't open the recovery file
            throw PersistentStorageException("fatal error: can't open "
                                             "recovery file");
        }
        ifs.close();
    }

  private:
    ServerPersistentState sps;
    std::string storage_file;
};
