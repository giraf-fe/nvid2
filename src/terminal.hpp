#pragma once
#include <vector>
#include <string>
#include <functional>

#include <nspireio/nspireio.h>

struct CommandHandler {
    std::string commandName;
    std::function<std::string(const std::vector<std::string>& args)> handler;
};

class Terminal {
    std::vector<CommandHandler> commandHandlers;
    nio_console csl;
public:
    Terminal(
        const std::vector<CommandHandler>& handlers
    );
    ~Terminal();

    void run();
};

CommandHandler GetplayCommandHandler();
CommandHandler GetlsCommandHandler();
CommandHandler GetcdCommandHandler();
CommandHandler GetregisterCommandHandler();