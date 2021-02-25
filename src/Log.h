#include <string>

/**
 * The Log class implements a generic, 1-indexed log that backs up to a file
 * and can be recovered from a file. As an optimization, logs are recovered
 * from file into the in-memory cache starting at a specified offset index, 
 * so not all entries are required to be read at a time.
 */
template <typename T>
class Log {
  public:
    Log(std::string file_name, 
        std::function<std::string(const T&)> _serialize_entry, 
        std::function<T(std::string)> _deserialize_entry);

    void recover(int _offset);
    void truncate(int new_size);
    void append(const T &entry);
    int size() const;

    T& operator[](int i); // 1-INDEXED

  private:
    // File to which the log entries are stored, one entry per line.
    std::string storage_file;
    // Each line of this file is the byte length of the corresponding line in
    // storage_file. The lines in table_file are of a fixed length (4 bytes).
    std::string table_file;
    std::function<std::string(const T&)> serialize_entry;
    std::function<T(std::string)> deserialize_entry;

    std::vector<T> log_cache;
    int offset {0}; 
};
