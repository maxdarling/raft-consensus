#include "Log.h"

/**
 * Construct a log that stores entries of type T and backs up to the specified
 * file. The user should provide a function to serialize an object of type T to
 * a string and a function to deserialize a string that would be returned from
 * the serialize function back into an object of type T. The serialized string
 * of each entry is stored as a line on the file.
 */
template <typename T>
Log<T>::Log(std::string file_name, 
    std::function<std::string(const T&)> _serialize_entry, 
    std::function<T(std::string)> _deserialize_entry)
{

}

/**
 * Recover a log from the file specified in the constructor. Only the entries
 * at and after the offset index will be read into the in-memory cache.
 */
template <typename T>
void Log<T>::recover(int _offset)
{
    
}

/**
 * Truncate the log to the specified size. If new_size >= current size or
 * new_size is negative, this function will have no effect.
 */
template <typename T>
void Log<T>::truncate(int new_size)
{
    
}

/**
 * Add an entry to the end of the log.
 */
template <typename T>
void Log<T>::append(const T &entry)
{
    
}

/**
 * Returns the size of the entire log.
 */
template <typename T>
int Log<T>::size() const
{
    
}

/**
 * Returns a reference to the entry at index i. This is a constant time
 * operation unless an index less than the offset of a recovered log is
 * provided, in which case the runtime is linear in the size of the log.
 */
template <typename T>
T& Log<T>::operator[](int i)
{
    
}
