#include <string>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <cstdio>

/* Width of each line in the table file, in bytes. */
const int TABLE_ENTRY_WIDTH = 5;

/**
 * The Log class implements a generic, 1-indexed log that backs up to a file
 * and can be recovered from a file. Individual log entries are immutable. As
 * an optimization, log entries are recovered from file into the cache after a 
 * specified offset, so not all entries are required to be read at a time.
 * 
 * When a Log is constructed, a table file is also generated. Deleting,
 * moving, or otherwise modifying this table file may corrupt the entire log.
 */
template <typename T>
class Log {
  public:
    Log(std::string file_name, 
        std::function<std::string(const T&)> _serialize_entry, 
        std::function<T(std::string)> _deserialize_entry);

    void clear();
    void recover(int _offset = 0);
    void trunc(int new_size);
    void append(const T &entry);
    /* Return the number of entries stored in the log. */
    int size() const { return log_cache.size() + offset; }
    /* Returns true if there are no entries stored in the log. */
    bool empty() const { return size() == 0; }

    T operator[](int i); // 1-INDEXED

  private:
    /* File to which the log entries are stored, one entry per line. */
    std::string log_file;
    /* Each line of this file is the byte length of the corresponding line in
     * log_file. The lines in table_file are of a fixed length. */
    std::string table_file;
    std::function<std::string(const T&)> serialize_entry;
    std::function<T(std::string)> deserialize_entry;
    std::vector<T> log_cache;

    /* The offset is the number of entries that are in the log file but not
     * loaded into the cache. The first element of the cache is the log entry
     * at index offset + 1. */
    int offset {0};

    size_t log_file_pos(int entry_offset);
};

/**
 * Construct a log that stores entries of type T and backs up to the specified
 * file. The user should provide a function to serialize an object of type T to
 * a string and a function to deserialize a string that would be returned from
 * the serialize function back into an object of type T. The serialized string
 * of each entry is stored as a line in the log file.
 */
template <typename T>
Log<T>::Log(std::string file_name, 
    std::function<std::string(const T&)> _serialize_entry, 
    std::function<T(std::string)> _deserialize_entry)
  : log_file(file_name),
    table_file(file_name + "table"),
    serialize_entry(_serialize_entry),
    deserialize_entry(_deserialize_entry) {}

/**
 * Clear resets the log by deleting the cache, log file, and table file.
 */
template <typename T>
void Log<T>::clear()
{
    log_cache.clear();
    std::remove(log_file.c_str());
    std::remove(table_file.c_str());
}

/**
 * Recover a log from the file specified in the constructor. Only the entries
 * AFTER the offset index will be read into the in-memory cache. An offset of 0
 * means all the entries will be read.
 */
template <typename T>
void Log<T>::recover(int _offset)
{
    offset = _offset;

    std::ifstream log_ifs(log_file, std::ios::binary);
    log_ifs.seekg(log_file_pos(offset));

    // read entries into cache
    log_cache.clear();
    for (std::string log_entry; std::getline(log_ifs, log_entry);) {
        log_cache.push_back(deserialize_entry(log_entry));
    }
}

/**
 * Truncate the log to the specified size. If new_size >= current size or
 * new_size is negative, this function has no effect.
 */
template <typename T>
void Log<T>::trunc(int new_size)
{
    if (new_size < 0 || new_size >= size()) return;

    // truncate() from unistd.h (see Linux man page for more info)
    truncate(log_file.c_str(), log_file_pos(new_size));
    log_cache.resize(new_size - offset > 0 ? new_size - offset : 0);
    if (new_size < offset) offset = new_size;
}

/**
 * Add an entry to the end of the log.
 */
template <typename T>
void Log<T>::append(const T &entry)
{
    // add to file
    std::ofstream log_ofs(log_file, std::ios::app | std::ios::binary);
    std::ofstream table_ofs(table_file, std::ios::app | std::ios::binary);
    std::string entry_str = serialize_entry(entry);
    // Ensure the serialized string's length can be represented in the table 
    // file, the lines of which must be at most TABLE_ENTRY_WIDTH bytes wide.
    if (entry_str.size() >= pow(10, TABLE_ENTRY_WIDTH - 1)) {
        throw std::runtime_error("Serialized entry too large for log");
    }
    log_ofs << entry_str << "\n";
    table_ofs << std::setfill('0') << std::setw(TABLE_ENTRY_WIDTH - 1) 
        << entry_str.size() << "\n";

    // add to cache
    log_cache.push_back(entry);
}

/**
 * Returns the log entry at index i.
 */
template <typename T>
T Log<T>::operator[](int i)
{
    if (i < 1 || i > size()) throw std::runtime_error("Index out of bounds");

    // cache hit
    if (i > offset) return log_cache[i - offset - 1];

    std::ifstream log_ifs(log_file, std::ios::binary);
    log_ifs.seekg(log_file_pos(i - 1));
    std::string entry;
    std::getline(log_ifs, entry);
    return deserialize_entry(entry);
}

/**
 * Translates from entry_offset, which is the number of log entries being
 * skipped, to a byte offset, which is the number of bytes in the log file that
 * should be skipped in order to skip that many log entry lines.
 */
template <typename T>
size_t Log<T>::log_file_pos(int entry_offset)
{
    std::ifstream table_ifs(table_file, std::ios::binary);
    if (!table_ifs) throw std::runtime_error("Could not access table file");

    int bytes = 0;
    for (std::string table_entry; entry_offset > 0; entry_offset--) {
        std::getline(table_ifs, table_entry);
        bytes += std::stoi(table_entry) + 1;
    }
    return bytes;
}
