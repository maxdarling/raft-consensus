#include "Log.h"
#include <iostream>

using std::cout, std::cerr, std::string;
const string test_file = "test_log";
struct LogEntry {
    std::string command;
    int term;
};

string serialize(const LogEntry &entry);
LogEntry deserialize(string entry_str);

int main(int argc, char* argv[]) {
    string table_file = test_file + "table";
    std::remove(test_file.c_str());
    std::remove(table_file.c_str());

    std::vector<LogEntry> entries = {{"pwd", 12}, {"ls | echo", 1}, 
    {"./run_some_executible with these args 1 2", 4}, 
    {"how you like me now?", 100}, {"", 0}, {"woah", 2}};

    // add all entries to log
    {
        Log<LogEntry> l("test_log", serialize, deserialize);
        for (const auto &e : entries) l.append(e);
    }
    
    // test recovery at all possible offsets
    for (int i = 0; i < entries.size(); i++) {
        Log<LogEntry> l("test_log", serialize, deserialize);
        l.recover(i);
        for (int j = 0; j < entries.size(); j++) {
            assert(entries[j].command == l[j + 1].command && 
                   entries[j].term == l[j + 1].term);
        }
        // test size()
        assert(entries.size() == l.size());
    }

    // test truncation
    {
        Log<LogEntry> l("test_log", serialize, deserialize);
        l.recover(entries.size() / 2 - 1);
        l.trunc(entries.size() / 2);
        assert(entries.size() / 2 == l.size());
        for (int j = 0; j < l.size(); j++) {
            assert(entries[j].command == l[j + 1].command && 
                   entries[j].term == l[j + 1].term);   
        }
    }

    std::remove(test_file.c_str());
    std::remove(table_file.c_str());

    cout << "ALL TESTS PASS!\n";
    return 0;
}

/**
 * Serialize a LogEntry object to a string representation.
 */
string serialize(const LogEntry &entry)
{
    return std::to_string(entry.term) + " " + entry.command;
}

/**
 * Deserialize the string representation of a LogEntry object back into the
 * object.
 */
LogEntry deserialize(string entry_str)
{
    size_t delimiter_idx = entry_str.find(" ");
    int term = std::stoi(entry_str.substr(0, delimiter_idx));
    string command = entry_str.substr(delimiter_idx + 1);
    return {command, term};
}
