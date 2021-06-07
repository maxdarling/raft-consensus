#include <string>
#include <unordered_map>
#include <netinet/in.h>

using std::string;

/* Default path for the server address file. See README for details. */
const string SRC_DIR = 
  string(__FILE__).substr(0, string(__FILE__).find_last_of("/") + 1);
const string DEFAULT_SERVER_FILE_PATH =  SRC_DIR + "../server_list";

std::unordered_map<int, string> parseClusterInfo(string serverFilePath);
int parsePort(string hostAndPort);


/* File reading */
int readFileChunk(string filename, char *buf, size_t offset, size_t count);