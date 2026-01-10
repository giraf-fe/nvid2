#pragma once

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <libndls.h>

#include "aligned_alloc/aligned_alloc.h"

#include "RingBuffer.hpp"
#include "SP804.hpp"

#define SIZEOF_RGB565 2
#define SIZEOF_RGB888 4
#define FILE_READ_BUFFER_PADDING 32
#define SIZEOF_FILE_READ_BUFFER (131072ul - FILE_READ_BUFFER_PADDING)
#define FRAMES_IN_FLIGHT_COUNT 1
#define SIZEOF_FRAME (SIZEOF_RGB565 * SCREEN_WIDTH * SCREEN_HEIGHT)
#define CACHE_LINE_SIZE 32

constexpr uint32_t timerHz = 12'000'000 / 256; // 12 MHz / 256 prescale
constexpr uint32_t timerStartValue = 0xFFFFFFFF;

static inline void* AlignedAllocate(size_t alignment, size_t size) {
    return aligned_malloc(alignment, size);
}
struct AlignedDeleter {
    inline void operator()(void* ptr) const {
        aligned_free(ptr);
    }
};

template<uintptr_t SRAMAddr, size_t BufferSize, uintptr_t Offset>
class SRAMBuffer {
    static_assert(Offset + BufferSize <= 0x40000, "SRAMBuffer exceeds SRAM bounds");
    void* SDRAMBuffer;
public:
    SRAMBuffer() {
        SDRAMBuffer = malloc(BufferSize);
        if (!SDRAMBuffer) {
            return;
        }
        // copy from SRAM to SDRAM buffer
        memcpy(SDRAMBuffer, (void*)(SRAMAddr + Offset), BufferSize);
    }
    bool isValid() const {
        return SDRAMBuffer != nullptr;
    }
    ~SRAMBuffer() {
        // copy back from SDRAM buffer to SRAM
        if (SDRAMBuffer) {
            memcpy((void*)(SRAMAddr + Offset), SDRAMBuffer, BufferSize);
        }
        free(SDRAMBuffer);
    }

    inline constexpr uintptr_t get() const {
        return SRAMAddr + Offset;
    }
};

// in volheader.cpp
std::string GetXvidErrorMessage(int errorCode);

enum class HandleInsufficientDataResult {
    Success,
    EndOfFile,
    Error
};

template <typename Framebuffer>
struct FrameInFlightData {
    uint64_t timingTicks;
    Framebuffer* swapchainFramePtr;
};

struct VideoPlayerOptions {
    std::string filename;
    bool benchmarkMode = false;
    bool blitDuringBenchmark = false;
    bool qualityDecoding = false;
};
struct MagicFrameBuffer {
    void* data() {
        return (void*)(0xA8000000);
    }
};

class VideoPlayer {
    VideoPlayerOptions options;  

    SRAMBuffer<0xA4000000, 0x20000, 0x20000> sramBuffer;

    FILE* videoFile = nullptr;
    bool fileEndReached = false;
    void* xvidDecoderHandle = nullptr;

    size_t decoderReadHead = SIZEOF_FILE_READ_BUFFER;
    size_t decoderReadAvailable = 0;
    std::unique_ptr<uint8_t[], AlignedDeleter> fileReadBuffer;
    
    // using FrameBufferType = std::array<uint8_t, SIZEOF_FRAME>;
    using FrameBufferType = MagicFrameBuffer;

    std::unique_ptr<SwapChain<FrameBufferType, FRAMES_IN_FLIGHT_COUNT>, AlignedDeleter> 
        decodedFramesSwapchain;

    std::unique_ptr<RingBuffer<FrameInFlightData<FrameBufferType>, FRAMES_IN_FLIGHT_COUNT>, AlignedDeleter>
        framesInFlightQueue;

    int videoWidth = 0, videoHeight = 0;

    // using timer 1
    SP804Timer<0x900C0000, timerHz> frameTimer;

    struct {
        uint16_t timeIncrementResolution;
        bool fixedVopRate;
        uint16_t fixedVopTimeIncrement;
    } 
    videoTimingInfo{};

    uint32_t lastFrameBlitTime = 0;

    uint32_t lastMemmoveTime = 0;
    uint32_t lastMemmoveBytes = 0;
    uint32_t lastFileReadTime = 0;
    uint32_t lastFileReadBytes = 0;

    uint32_t CalculateFileReadAmount(uint32_t ticks);

    struct {
        std::vector<uint32_t> IFrame_DecodeTimes;
        std::vector<uint32_t> PFrame_DecodeTimes;
        std::vector<uint32_t> BFrame_DecodeTimes;
        std::vector<uint32_t> SFrame_DecodeTimes;

        std::vector<uint32_t> WastedFrame_DecodeTimes;

        std::vector<uint32_t> Frame_BlitTimes;

        // memmove time, bytes moved, fread time, bytes read
        std::vector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> Buffer_RefillTimes;

        std::vector<int32_t> Pacing_WaitTimes;
        std::vector<uint32_t> Frame_TotalTimes;
    } profilingInfo;

    bool failedFlag = false;
    std::string errorMsg = "Incomplete initialization";

    scr_type_t lcdScreenType;

    // return false if file end reached
    bool fillReadBuffer(uint32_t requestedBytes = SIZEOF_FILE_READ_BUFFER);

    void readVOLHeader();

    HandleInsufficientDataResult handleInsufficientData(
        uint32_t frameDecodeStartTicks,
        FrameBufferType* frameBuffer,
        bool& hadDiscontinuity,
        const char* errorContext,
        bool requireDiscontinuity
    );

    void advanceReadHead(int bytesConsumed);

    void fillFramesInFlightQueue();

public:
    VideoPlayer(const VideoPlayerOptions& options);
    ~VideoPlayer();

    void play();

    bool failed() const;
    std::string getErrorMessage() const;

    std::string dumpState() const;

    std::string short_stats(const std::vector<std::uint32_t>& data) const;
    std::string short_stats(const std::vector<std::int32_t>& data) const;
};