#include "RaftRPC.pb.h"
#include <fstream>

class PersistentStorage {
  public:
    PersistentStorage(std::string file_name) : storage_file(file_name) {}

    ServerPersistentState& state() { return sps; }

    void save()
    {
        std::ofstream ofs(storage_file, std::ios::trunc | std::ios::binary);
        sps.SerializeToOstream(&ofs);
        ofs.close();
    }

    bool recover()
    {
        std::ifstream ifs(storage_file, std::ios::binary);
        if (!ifs) return false;
        sps.ParseFromIstream(&ifs);
        ifs.close();
        return true;
    }

  private:
    ServerPersistentState sps;
    std::string storage_file;
};
