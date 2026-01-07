#include "../terminal.hpp"

#include "../videoplayer/VideoPlayer.hpp"

CommandHandler GetplayCommandHandler() {
    return CommandHandler{
        "play",
        [](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) {
                return "Usage: play <filename>";
            }
            const std::string& filename = args[1];
            
            VideoPlayer videoPlayer(filename);
            if (videoPlayer.failed()) {
                uart_puts(videoPlayer.dumpState().c_str());
                return "play: Error playing video: " + videoPlayer.getErrorMessage();
            }

            videoPlayer.play();
            if (videoPlayer.failed()) {
                uart_puts(videoPlayer.dumpState().c_str());
                return "play: Error during playback: " + videoPlayer.getErrorMessage() + "\n" + videoPlayer.dumpState();
            }


            return "";
        }
    };
}