#include <string>
#include <unordered_map>
#include <netinet/in.h>

/* Default path for the server address file. See README for details. */
const std::string SRC_DIR = 
  std::string(__FILE__).substr(0, std::string(__FILE__).find_last_of("/") + 1);
const std::string DEFAULT_SERVER_FILE_PATH =  SRC_DIR + "../server_list";

std::unordered_map<int, std::string> parseClusterInfo(std::string serverFilePath);
int parsePort(std::string hostAndPort);
