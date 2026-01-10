#include "../terminal.hpp"

#include "../videoplayer/VideoPlayer.hpp"

CommandHandler GetplayCommandHandler() {
    return CommandHandler{
        "play",
        [](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) {
                return "Usage: play <filename> [options...]\n"
                       "Options:\n"
                       "  -b\tRun in benchmark mode (no video output)\n"
                       "  -bdb\tBlit frames even in benchmark mode\n"
                       "  -qd\tUse quality decoding mode (slower)\n";
            }
            const std::string& filename = args[1];

            // parse options
            bool benchmarkMode = false;
            bool blitDuringBenchmark = false;
            bool qualityDecoding = false;
            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "-b") {
                    benchmarkMode = true;
                } else if (args[i] == "-bdb") {
                    blitDuringBenchmark = true;
                } else if (args[i] == "-qd") {
                    qualityDecoding = true;
                } else {
                    return "play: Unknown option: " + args[i];
                }
            }
            
            VideoPlayerOptions options{
                .filename = filename,
                .benchmarkMode = benchmarkMode,
                .blitDuringBenchmark = blitDuringBenchmark,
                .qualityDecoding = qualityDecoding
            };

            VideoPlayer videoPlayer(options);
            if (videoPlayer.failed()) {
                return "play: Error playing video: " + videoPlayer.getErrorMessage();
            }

            videoPlayer.play();
            if (videoPlayer.failed()) {
                return "play: Error during playback: " + videoPlayer.getErrorMessage() + "\n" + videoPlayer.dumpState();
            }

            return "";
        }
    };
}