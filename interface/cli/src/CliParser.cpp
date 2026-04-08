#include "CliParser.h"

#include <iostream>

namespace iee {

ParsedCommand CliParser::Parse(int argc, char* argv[]) {
    ParsedCommand parsed;

    if (argc < 2) {
        return parsed;
    }

    parsed.command = argv[1];
    int index = 2;

    if (parsed.command == "execute" && argc >= 3) {
        parsed.action = argv[2];
        index = 3;
    }

    while (index < argc) {
        const std::string token = argv[index];
        if (token.rfind("--", 0) == 0) {
            std::string key = token.substr(2);
            std::string value = "true";
            if (index + 1 < argc) {
                const std::string next = argv[index + 1];
                if (next.rfind("--", 0) != 0) {
                    value = next;
                    ++index;
                }
            }
            parsed.options[key] = value;
        } else {
            parsed.positionals.push_back(token);
        }

        ++index;
    }

    return parsed;
}

void CliParser::PrintHelp() {
    std::cout << "Intent Execution Engine (IEE) CLI\n\n";
    std::cout << "Global options:\n";
    std::cout << "  --pure-json    force structured JSON output and disable runtime log noise\n\n";
    std::cout << "Commands:\n";
    std::cout << "  iee state [--json]\n";
    std::cout << "  iee state/ai [--json]\n";
    std::cout << "  iee list-intents\n";
    std::cout << "  iee execute <intent> --target \"<label>\" [--value \"<value>\"]\n";
    std::cout << "  iee inspect\n";
    std::cout << "  iee graph [--json] [--delta_since <version>]\n";
    std::cout << "  iee node <id> [--json]\n";
    std::cout << "  iee plan <id> [--json]\n";
    std::cout << "  iee reveal <id> [--json]\n";
    std::cout << "  iee capabilities [--all] [--json]\n";
    std::cout << "  iee explain --action <intent> --target \"<label>\"\n";
    std::cout << "  iee debug-intents [--json]\n";
    std::cout << "  iee telemetry [--json] [--status <STATUS>] [--adapter <NAME>] [--limit <N>]\n";
    std::cout << "  iee telemetry --persistence [--json]\n";
    std::cout << "  iee latency [--json] [--limit <N>]\n";
    std::cout << "  iee perf [--json] [--target_ms <MS>] [--limit <N>] [--strict]\n";
    std::cout << "  iee vision [--json] [--limit <N>]\n";
    std::cout << "  iee demo presentation|browser [--json] [--run]\n";
    std::cout << "  iee trace [<trace_id>] [--limit <N>]\n";
    std::cout << "  iee api [--port 8787] [--once]\n";
    std::cout << "  iee execute move --path \"file.txt\" --destination \"docs/\"\n";
    std::cout << "  iee execute delete --path \"file.txt\"\n";
    std::cout << "  iee execute create --path \"notes.txt\"\n";
}

}  // namespace iee
