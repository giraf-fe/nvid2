#include "terminal.hpp"

#include <os.h>

#include <algorithm>
#include <array>

Terminal::Terminal(const std::vector<CommandHandler>& handlers)
    : commandHandlers(handlers)
{
    nio_init(&this->csl,
        NIO_MAX_COLS, NIO_MAX_ROWS,
        0, 0, NIO_COLOR_BLACK, NIO_COLOR_WHITE, TRUE
    );
}

Terminal::~Terminal() {
    nio_free(&this->csl);
}

static std::string GetCWD() {
    char buffer[256];
    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        return std::string(buffer);
    }
    return std::string("/documents");
}

static std::string trim(std::string str) {
    str.erase(0, str.find_first_not_of(" \n\r\t"));
    str.erase(str.find_last_not_of(" \n\r\t") + 1);
    return str;
}

static std::vector<std::string> splitInput(const std::string& input) {
    std::vector<std::string> result;
    size_t pos = 0, found;
    while((found = input.find_first_of(' ', pos)) != std::string::npos) {
        if(found > pos)
            result.push_back(trim(input.substr(pos, found - pos)));
        pos = found + 1;
    }
    if(pos < input.length())
        result.push_back(trim(input.substr(pos)));
    return result;
}

void Terminal::run() {
    char inputBuffer[256];
    while (true) {
        nio_fprintf(&this->csl, "%s> ", GetCWD().c_str());
        // read input
        nio_fgets(inputBuffer, sizeof(inputBuffer), &this->csl);

        std::string inputStr(inputBuffer);
        std::vector<std::string> args = splitInput(inputStr);
        if (args.empty()) {
            continue;
        }
        const std::string& command = args[0];
        uart_puts(("Command: " + command + "\n").c_str());

        for(const auto& handler : this->commandHandlers) {
            if (handler.commandName == command) {
                std::string output = handler.handler(args);
                // too long for fprintf
                // nio_fprintf(&this->csl, "%s\n", output.c_str());
                for(size_t i = 0; i < output.size(); i++) {
                    nio_fputc(output[i], &this->csl);
                }
                nio_fputc('\n', &this->csl);
                break;
            }
        }

        // place exit/quit commands after handler loop
        std::array<std::string, 3> exitCommands = {"exit", "quit", "q"};
        if (std::find(exitCommands.begin(), exitCommands.end(), command) != exitCommands.end()) {
            break;
        }
        
        memset(inputBuffer, 0, sizeof(inputBuffer));
    }
}