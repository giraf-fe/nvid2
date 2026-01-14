#include <os.h>
#include <nspireio/nspireio.h>

#include "terminal.hpp"

int main(int argc, char** argv) {
    // success or fail doesnt matter
    enable_relative_paths(argv);

    // create simple terminal with ls and cd
    // use play command to play video
    Terminal terminal({
        GetlsCommandHandler(),
        GetcdCommandHandler(),
        GetplayCommandHandler()
    });
    terminal.run();
}