#include <string>
#include <map>

#ifndef INI_PARSER
#define INI_PARSER

typedef std::map<std::string, std::map<std::string, std::string>> ini_map;

ini_map ini_to_map(const std::string& ini_data);

#endif
