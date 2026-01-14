#include "../terminal.hpp"

#include "../videoplayer/VideoPlayer.hpp"

CommandHandler GetplayCommandHandler() {
    return CommandHandler{
        "play",
        [](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) {
                return "Usage: play <filename> [options...]\n"
                       "Options:\n"
                       "  -b\tRun in benchmark mode (no video output) | Default: off\n"
                       "  -bdb\tBlit frames even in benchmark mode | Default: off\n"
                       "  -mfb\tUse the magic framebuffer | Default: on\n"
                       "  -lcdblit\tUse LCD blit API for frame output | Default: off\n"
                       "  -prv\tPre-rotated video (no rotation during blit, video must be pre-rotated) | Default: off\n"
                       "  -fd\tFast decoding (less CPU usage, lower quality) | Default: on\n"
                       "  -ld\tLow-delay mode (reduces latency, drops b-frame support) | Default: on\n"
                       "  -dbl\tEnable luma deblocking filter | Default: off\n"
                       "  -dbc\tEnable chroma deblocking filter | Default: off\n"
                       "  -drl\tEnable luma deringing filter | Default: off\n"
                       "  -drc\tEnable chroma deringing filter | Default: off\n"
                       "\n"
                       "  To turn off an option that is on by default, use the opposite flag (e.g. -Nmfb to disable magic framebuffer).\n"
                       "  Options can be combined in any order, later options override earlier ones.\n"
                       ;
            }
            const std::string& filename = args[1];

            // parse options
            VideoPlayerOptions options;
            options.filename = filename;

            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "-b") {
                    options.benchmarkMode = true;
                } else if (args[i] == "-bdb") {
                    options.blitDuringBenchmark = true;
                } else if (args[i] == "-mfb") {
                    options.useMagicFrameBuffer = true;
                } else if (args[i] == "-lcdblit") {
                    options.useLcdBlitAPI = true;
                } else if (args[i] == "-prv") {
                    options.preRotatedVideo = true;
                } else if (args[i] == "-fd") {
                    options.fastDecoding = true;
                } else if (args[i] == "-ld") {
                    options.lowDelayMode = true;
                } else if (args[i] == "-dbl") {
                    options.deblockLuma = true;
                } else if (args[i] == "-dbc") {
                    options.deblockChroma = true;
                } else if (args[i] == "-drl") {
                    options.deringLuma = true;
                } else if (args[i] == "-drc") {
                    options.deringChroma = true;
                } else if (args[i] == "-Nb") {
                    options.benchmarkMode = false;
                } else if (args[i] == "-Nbdb") {
                    options.blitDuringBenchmark = false;
                } else if (args[i] == "-Nmfb") {
                    options.useMagicFrameBuffer = false;
                } else if (args[i] == "-Nlcdblit") {
                    options.useLcdBlitAPI = false;
                } else if (args[i] == "-Nprv") {
                    options.preRotatedVideo = false;
                } else if (args[i] == "-Nfd") {
                    options.fastDecoding = false;
                } else if (args[i] == "-Nld") {
                    options.lowDelayMode = false;
                } else if (args[i] == "-Ndbl") {
                    options.deblockLuma = false;
                } else if (args[i] == "-Ndbc") {
                    options.deblockChroma = false;
                } else if (args[i] == "-Ndrl") {
                    options.deringLuma = false;
                } else if (args[i] == "-Ndrc") {
                    options.deringChroma = false;
                } else {
                    return "play: Unknown option: " + args[i];
                }
            }

            VideoPlayer videoPlayer(options);
            if (videoPlayer.failed()) {
                return "play: Error configuring VideoPlayer: " + videoPlayer.getErrorMessage();
            }

            videoPlayer.play();
            if (videoPlayer.failed()) {
                return "play: Error during playback: " + videoPlayer.getErrorMessage() + "\n" + videoPlayer.dumpState();
            }

            return "";
        }
    };
}