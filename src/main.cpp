#include <os.h>
#include <nspireio/nspireio.h>

#include "terminal.hpp"

int main(int argc, char** argv) {
    // success or fail doesnt matter
    enable_relative_paths(argv);

    // if running from cfg
    if(argc > 1) {
        // if a filename is provided, play it directly
        auto playHandler = GetplayCommandHandler();
        std::vector<std::string> args;
        args.push_back("play");
        for(int i = 1; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        playHandler.handler(args);
        return 0;
    }

    // create simple terminal with ls and cd
    // use play command to play video
    Terminal terminal({
        GetlsCommandHandler(),
        GetcdCommandHandler(),
        GetplayCommandHandler(),
        GetregisterCommandHandler()
    });
    terminal.run();

    return 0;
}