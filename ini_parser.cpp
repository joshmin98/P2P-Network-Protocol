#include "ini_parser.h"
#include <sstream>
#include <fstream>

typedef std::map<std::string, std::map<std::string, std::string>> ini_map;

std::string readFileToString(const std::string& filename) {
  std::ifstream source;
  source.open(filename);
  std::string lines;
  std::string line;
  while(getline(source, line)) {
    lines += line + "\n";
  }
  return lines;
}

ini_map ini_to_map(const std::string& ini_file) {
  std::stringstream ini_lines(readFileToString(ini_file));
  ini_map data;

  std::string curr_section;
  while (!ini_lines.eof()) {
    std::string line;
    ini_lines >> line;
    if (line[0] == '[') {
      curr_section = line.substr(1, line.size() - 2);
      std::map<std::string, std::string> section_data;
      data[curr_section] = section_data;
    } else if (line[0] == ';') {
      continue;
    } else {
      unsigned int pos = line.find("=");
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1, line.size());
      data[curr_section][key] = value;
    }
  }
  return data;
}
