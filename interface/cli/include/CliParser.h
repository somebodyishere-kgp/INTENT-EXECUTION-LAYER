#pragma once

#include <map>
#include <string>
#include <vector>

namespace iee {

struct ParsedCommand {
    std::string command;
    std::string action;
    std::vector<std::string> positionals;
    std::map<std::string, std::string> options;
};

class CliParser {
public:
    static ParsedCommand Parse(int argc, char* argv[]);
    static void PrintHelp();
};

}  // namespace iee
