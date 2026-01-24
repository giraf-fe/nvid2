#include "../terminal.hpp"

CommandHandler GetregisterCommandHandler() {
    return CommandHandler{
        "register",
        [](const std::vector<std::string>& args) -> std::string {
            // add to ndless cfg
            cfg_register_fileext("m4v", "nvid2");
            return "Registered .m4v file extension to nvid2.\n";
        }
    };
}