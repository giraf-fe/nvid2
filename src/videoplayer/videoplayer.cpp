#include "VideoPlayer.hpp"

#include <xvid.h>
#include <decoder.h>
#include <os.h>

#include <new>
#include <algorithm>

#include <nspireio/uart.hpp>

static void pwr_lcd(bool on)
{
    uint32_t control = *IO_LCD_CONTROL;
    if (on)
        control |= (1 << 0);
    else
        control &= ~(1 << 0);
    *IO_LCD_CONTROL = control;

}
static void set_lcd_mode(unsigned int mode)
{
    uint32_t control = *IO_LCD_CONTROL;
    control &= ~0b1110;
    control |= mode << 1;
    *IO_LCD_CONTROL = control;
}

VideoPlayer::VideoPlayer(const VideoPlayerOptions& options) : options(options) {
    // check options
    if (options.useMagicFrameBuffer && options.use24bitRGB) {
        this->failedFlag = true;
        this->errorMsg = "Incompatible options: useMagicFrameBuffer and use24bitRGB cannot both be true";
        return;
    }
    if (options.useMagicFrameBuffer && options.useLcdBlitAPI) {
        this->failedFlag = true;
        this->errorMsg = "Incompatible options: useMagicFrameBuffer and useLcdBlitAPI cannot both be true";
        return;
    }
    if (options.use24bitRGB && options.useLcdBlitAPI) {
        this->failedFlag = true;
        this->errorMsg = "Incompatible options: use24bitRGB and useLcdBlitAPI cannot both be true";
        return;
    }
    if (options.preRotatedVideo && (options.useMagicFrameBuffer || options.useLcdBlitAPI)) {
        this->failedFlag = true;
        this->errorMsg = "Incompatible options: preRotatedVideo cannot be true when using MagicFrameBuffer or LcdBlitAPI";
        return;
    }


    // init timer
    frameTimer.stop();
    frameTimer.clearIRQ();
    frameTimer.configure(CreateSP804TimerInfo{
        .mode = SP804TimerMode::FreeRunning,
        .oneshotMode = SP804OneshotMode::Wrapping,
        .prescale = SP804TimerPrescale::Div256,
        .size = SP804TimerSize::Size32Bit,
        .interruptEnable = false,
        .enableTimer = false
    });
    frameTimer.setLoadValue(timerStartValue);
    frameTimer.start();

    // init lcd, decoder, file
    // open file
    this->videoFile = fopen(options.filename.c_str(), "rb");
    if (!this->videoFile) {
        this->failedFlag = true;
        this->errorMsg = "Failed to open video file: " + options.filename;
        return;
    }

    // xvid global init
    xvid_gbl_init_t xvid_gbl_init{};
    xvid_gbl_init.version = XVID_VERSION;
    xvid_gbl_init.sram_base = (void*)this->sramBuffer.get();
    xvid_gbl_init.sram_size = 0x20000;
    xvid_global(NULL, XVID_GBL_INIT, &xvid_gbl_init, NULL);
    
    // xvid decoder init
    xvid_dec_create_t xvid_dec_create{};
    xvid_dec_create.version = XVID_VERSION;
    if (xvid_decore(NULL, XVID_DEC_CREATE, &xvid_dec_create, NULL) < 0) {
        this->failedFlag = true;
        this->errorMsg = "Failed to create Xvid decoder";
        return;
    }
    this->xvidDecoderHandle = xvid_dec_create.handle;

    // init buffers
    this->fileReadBuffer = std::unique_ptr<uint8_t[], AlignedDeleter>(
        static_cast<uint8_t*>(AlignedAllocate(CACHE_LINE_SIZE, SIZEOF_FILE_READ_BUFFER + FILE_READ_BUFFER_PADDING))
    );
    if (!this->fileReadBuffer) {
        this->failedFlag = true;
        this->errorMsg = "Failed to allocate file read buffer";
        return;
    }
    // zero padding
    memset(this->fileReadBuffer.get() + SIZEOF_FILE_READ_BUFFER, 0, FILE_READ_BUFFER_PADDING);

    // allocate decoded frames buffer
    for (size_t i = 0; i < FRAMES_IN_FLIGHT_COUNT; i++) {
        if (options.useMagicFrameBuffer) {
            this->frameBuffersArray[i] = new MagicFrameBuffer();
        } else if (options.use24bitRGB) {
            this->frameBuffersArray[i] = (StandardFrameBuffer<SIZEOF_RGB888>*) 
                aligned_malloc(CACHE_LINE_SIZE, sizeof(StandardFrameBuffer<SIZEOF_RGB888>));
            if (this->frameBuffersArray[i]) {
                new (this->frameBuffersArray[i]) StandardFrameBuffer<SIZEOF_RGB888>();
            } 
        } else {
            this->frameBuffersArray[i] = (StandardFrameBuffer<SIZEOF_RGB565>*) 
                aligned_malloc(CACHE_LINE_SIZE, sizeof(StandardFrameBuffer<SIZEOF_RGB565>));
            if (this->frameBuffersArray[i]) {
                new (this->frameBuffersArray[i]) StandardFrameBuffer<SIZEOF_RGB565>();
            }
        }
        if (!this->frameBuffersArray[i]) {
            this->failedFlag = true;
            this->errorMsg = "Failed to allocate frame buffer";
            return;
        }
    }
    this->decodedFramesSwapchain.setBuffers(this->frameBuffersArray);
    
    // prime read buffer
    this->fillReadBuffer();

    // try get vol header
    this->readVOLHeader();
    if(this->failedFlag) {
        return;
    }
    // check video dimensions
    if(options.preRotatedVideo) {
        if (this->videoWidth != SCREEN_HEIGHT || this->videoHeight != SCREEN_WIDTH) {
            this->failedFlag = true;
            this->errorMsg = 
                "Invalid video dimensions: Got " + 
                std::to_string(this->videoWidth) + "x" + std::to_string(this->videoHeight) + 
                ", expected " + 
                std::to_string(SCREEN_HEIGHT) + "x" + std::to_string(SCREEN_WIDTH);
            return;
        }
    } else {
        if (this->videoWidth != SCREEN_WIDTH || this->videoHeight != SCREEN_HEIGHT) {
            this->failedFlag = true;
            this->errorMsg = 
                "Invalid video dimensions: Got " + 
                std::to_string(this->videoWidth) + "x" + std::to_string(this->videoHeight) + 
                ", expected " + 
                std::to_string(SCREEN_WIDTH) + "x" + std::to_string(SCREEN_HEIGHT);
            return;
        }
    }

    // fill decoded frames buffer
    this->fillFramesInFlightQueue();
    if (this->failedFlag) {
        return;
    }
    
    // init lcd
    if (options.benchmarkMode && !options.blitDuringBenchmark) {
        // skip
    } else if (options.useLcdBlitAPI) {
        this->lcdScreenType = lcd_type();
        if (!lcd_init(this->lcdScreenType)) {
            this->failedFlag = true;
            this->errorMsg = "Failed to initialize LCD";
            return;
        }
    } else if (options.useMagicFrameBuffer || options.preRotatedVideo) {
        // nothing needs to be done
    } else if (options.use24bitRGB) { // need to allocate rotation buffers
        this->rotationBufferPtr = aligned_malloc(
            CACHE_LINE_SIZE,
            FRAME_TOTAL_PIXELS * SIZEOF_RGB888
        );
        if (!this->rotationBufferPtr.has_value()) {
            this->failedFlag = true;
            this->errorMsg = "Failed to allocate rotation buffer for 24-bit RGB mode";
            return;
        }
    } else {
        // for normal 16-bit rgb mode too
        this->rotationBufferPtr = aligned_malloc(
            CACHE_LINE_SIZE,
            FRAME_TOTAL_PIXELS * SIZEOF_RGB565
        );
        if (!this->rotationBufferPtr.has_value()) {
            this->failedFlag = true;
            this->errorMsg = "Failed to allocate rotation buffer for 16-bit RGB mode";
            return;
        }
    }

    this->failedFlag = false;
    this->errorMsg = "Successful initialization";
}
VideoPlayer::~VideoPlayer() {
    if(this->rotationBufferPtr.has_value()) {
        aligned_free(this->rotationBufferPtr.value());
        this->rotationBufferPtr.reset();
    }

    for (size_t i = 0; i < FRAMES_IN_FLIGHT_COUNT; i++) {
        if (this->frameBuffersArray[i]) {
            if(options.useMagicFrameBuffer) {
                delete this->frameBuffersArray[i];
            } else {
                this->frameBuffersArray[i]->~FrameBufferType();
                aligned_free(this->frameBuffersArray[i]);
            }
        }
    }

    if (this->xvidDecoderHandle) {
        xvid_decore(this->xvidDecoderHandle, XVID_DEC_DESTROY, NULL, NULL);
        this->xvidDecoderHandle = nullptr;
    }
    if (this->videoFile) {
        fclose(this->videoFile);
        this->videoFile = nullptr;
    }
}

bool VideoPlayer::failed() const {
    return this->failedFlag;
}
std::string VideoPlayer::getErrorMessage() const {
    return this->errorMsg;
}

bool VideoPlayer::fillReadBuffer(uint32_t requestedBytes) {
    // Returns true if more data may still be available (i.e., not at EOF).
    // Callers interpret false as end-of-file reached.
    uint32_t memmoveStartTicks = this->frameTimer.getCurrentValue32();

    // Compact unread bytes to the start of the buffer.
    if (this->decoderReadHead > 0 && this->decoderReadAvailable > 0) {
        memmove(
            (void*)this->fileReadBuffer.get(),
            (void*)this->fileReadBuffer.get() + this->decoderReadHead,
            this->decoderReadAvailable
        );
    }
    // After compaction, unread data begins at index 0.
    this->decoderReadHead = 0;

    uint32_t memmoveEndTicks = this->frameTimer.getCurrentValue32();

    const uint32_t freeSpace = SIZEOF_FILE_READ_BUFFER - static_cast<uint32_t>(this->decoderReadAvailable);
    const uint32_t bytesToRead = std::min<uint32_t>(requestedBytes, freeSpace);

    const uint32_t fileReadStartTicks = memmoveEndTicks;
    uint32_t bytesRead = 0;
    if (bytesToRead > 0) {
        bytesRead = fread(
            (void*)this->fileReadBuffer.get() + this->decoderReadAvailable,
            1,
            bytesToRead,
            this->videoFile
        );
        this->decoderReadAvailable += bytesRead;
    }
    const uint32_t fileReadEndTicks = this->frameTimer.getCurrentValue32();

    this->lastMemmoveTime = memmoveStartTicks - memmoveEndTicks;
    this->lastMemmoveBytes = static_cast<uint32_t>(this->decoderReadAvailable) - bytesRead;
    this->lastFileReadTime = fileReadStartTicks - fileReadEndTicks;
    this->lastFileReadBytes = bytesRead;

    this->profilingInfo.Buffer_RefillTimes.push_back({
        this->lastMemmoveTime,
        this->lastMemmoveBytes,
        this->lastFileReadTime,
        this->lastFileReadBytes
    });

    // If we couldn't fill the requested amount, treat it as end-of-file.
    // (Short reads can also happen for other reasons, but for this project
    //  we treat them as EOF.)
    if (bytesToRead == 0) {
        // Buffer full; not EOF.
        return true;
    }
    return bytesRead == bytesToRead;
}

uint32_t VideoPlayer::CalculateFileReadAmount(uint32_t ticks) {
    // calculate how much data can be read in given ticks
    // use last read speed as reference
    const uint32_t memmoveBytesPerTick = (this->lastMemmoveBytes) / (this->lastMemmoveTime ? this->lastMemmoveTime : 1);
    const uint32_t fileReadBytesPerTick = (this->lastFileReadBytes) / (this->lastFileReadTime ? this->lastFileReadTime : 1);

    // calculate the time needed for memmove
    const uint32_t estimatedMemmoveTime = (this->decoderReadAvailable) / (memmoveBytesPerTick ? memmoveBytesPerTick : 1);
    if(estimatedMemmoveTime >= ticks) {
        return 0;
    }
    // remaining time for file read
    const uint32_t remainingTicks = ticks - estimatedMemmoveTime;
    return remainingTicks * fileReadBytesPerTick;
}

void VideoPlayer::play() {
    void* oldBuf = this->InitLCD();

    uint32_t playbackStartTicks = this->frameTimer.getCurrentValue32();

    // play video
    uint64_t frameCounter = 0;
    while (true) {
        uint32_t frameStartTicks = this->frameTimer.getCurrentValue32();
        // escape
        if(any_key_pressed()) {
            if(isKeyPressed(KEY_NSPIRE_ESC)) {
                this->failedFlag = true;
                this->errorMsg = "Playback aborted by user";
                break;
            }
        }

        // check frames in flight
        if (this->framesInFlightQueue.empty()) {
            // no frames, video ended?
            this->failedFlag = true;
            this->errorMsg = "No more frames to display, video may have ended";
            break;
        }

        // get next frame
        bool success;
        FrameInFlightData<FrameBufferType>& frameData = this->framesInFlightQueue.pop(success);
        if (!success) {
            // should not happen due to empty check
            this->failedFlag = true;
            this->errorMsg = "Failed to get frame from frames in flight queue";
            break;
        }
        // fixed VOP rate adjustment
        frameData.timingTicks = (this->videoTimingInfo.fixedVopRate ? (frameCounter * this->videoTimingInfo.fixedVopTimeIncrement) : frameData.timingTicks);

        this->WaitForNextFrame(frameData.timingTicks, playbackStartTicks);
        
        // display frame
        uint32_t ticksBeforeBlit = frameTimer.getCurrentValue32();
        if (!this->options.benchmarkMode || this->options.blitDuringBenchmark) {
            this->DisplayFrame(frameData);
            frameCounter++;
        }
        uint32_t ticksAfterBlit = frameTimer.getCurrentValue32();
        this->lastFrameBlitTime = ticksBeforeBlit - ticksAfterBlit;
        profilingInfo.Frame_BlitTimes.push_back(this->lastFrameBlitTime);

        // before releasing, fill more frames in flight
        this->fillFramesInFlightQueue();
        if (this->failedFlag) {
            break;
        }

        // release frame buffer back to swapchain
        if (!this->decodedFramesSwapchain.release(frameData.swapchainFramePtr)) {
            this->failedFlag = true;
            this->errorMsg = "Failed to release frame buffer back to swapchain";
            break;
        }
        
        uint32_t frameEndTicks = this->frameTimer.getCurrentValue32();
        profilingInfo.Frame_TotalTimes.push_back(frameStartTicks - frameEndTicks);
    }

    this->CleanupLCD(oldBuf);
}

void* VideoPlayer::InitLCD() {
    if(this->options.benchmarkMode && !this->options.blitDuringBenchmark) {
        // skip
        return nullptr;
    }

    // set lcd mode
    void* oldBuf = REAL_SCREEN_BASE_ADDRESS;

    if (this->options.useLcdBlitAPI) {
        // nothing to do here
        return oldBuf;
    } else if (this->options.useMagicFrameBuffer) {
        // nothing to do here
        return oldBuf;
    }
    
    void* newBuf = this->decodedFramesSwapchain.operator[](0)->data();

    // if the rotation buffer is used, set it as the base address
    if(this->rotationBufferPtr.has_value()) {
        newBuf = this->rotationBufferPtr.value();
    }

    if (!options.use24bitRGB) {
        set_lcd_mode(6); // RGB565 mode
        REAL_SCREEN_BASE_ADDRESS = newBuf;
    } else {
        pwr_lcd(false);
        set_lcd_mode(5); // RGB888 mode
        REAL_SCREEN_BASE_ADDRESS = newBuf;
        pwr_lcd(true);
    }
    
    return oldBuf;
}
void VideoPlayer::CleanupLCD(void* oldFramebufferPtr) {
    if(this->options.benchmarkMode && !this->options.blitDuringBenchmark) {
        // skip
        return;
    }
    // restore lcd mode
    pwr_lcd(false);
    set_lcd_mode(6); // set back to normal
    REAL_SCREEN_BASE_ADDRESS = oldFramebufferPtr;
    pwr_lcd(true);
}

void VideoPlayer::WaitForNextFrame(uint32_t timingTicks, uint32_t playbackStartTicks) {
    uint32_t targetTicksElapsed = ((timingTicks * timerHz) + (this->videoTimingInfo.timeIncrementResolution / 2)) / this->videoTimingInfo.timeIncrementResolution;
    uint32_t targetTimerTicks = playbackStartTicks - targetTicksElapsed + this->lastFrameBlitTime;
    {
        constexpr uint32_t marginOfErrorTicks = timerHz / (1000); // 1 ms margin of error
        constexpr uint32_t attemptReadThreshold = SIZEOF_FILE_READ_BUFFER / 4; // try read if less than 1/4 buffer free
        int32_t ticksToWait = frameTimer.getCurrentValue32() - targetTimerTicks;
        
        // if there is extra time to do other processing, try to fill read buffer
        if (!this->fileEndReached && ticksToWait > marginOfErrorTicks && this->decoderReadAvailable < attemptReadThreshold) {
            uint32_t readStartTime = frameTimer.getCurrentValue32();
            uint32_t fileReadAmount = this->CalculateFileReadAmount(ticksToWait - marginOfErrorTicks);
            if(fileReadAmount > 0) {
                this->fileEndReached = !this->fillReadBuffer(fileReadAmount);
                if (this->failedFlag) {
                    return;
                }
                uint32_t readEndTime = frameTimer.getCurrentValue32();
                ticksToWait -= (readStartTime - readEndTime);
            }
        }

        profilingInfo.Pacing_WaitTimes.push_back(ticksToWait);
        if (ticksToWait > 0) { 
            // calculate sleep time in ms
            uint32_t sleepMs = (ticksToWait * 1000) / timerHz;
            if (sleepMs > 1 && !this->options.benchmarkMode) { // sleeping for 1 ms is ridiculous
                msleep(sleepMs);
            }
        } else {
            // TODO: implement frame skipping
            // uart_puts((std::string("Frame late by ") + std::to_string(-ticksToWait) + " ticks\n").c_str());
        }
    }
}

void VideoPlayer::DisplayFrame(FrameInFlightData<FrameBufferType>& frameData) {
    if (this->options.useMagicFrameBuffer) {
        // dont need to do anything, magic framebuffer auto-updates
        return;
    } else if (this->options.useLcdBlitAPI) {
        lcd_blit(frameData.swapchainFramePtr->data(), this->lcdScreenType);
        return;
    } else if (this->options.preRotatedVideo) {
        // pre-rotated video, can display directly
        REAL_SCREEN_BASE_ADDRESS = frameData.swapchainFramePtr->data();
        return;
    } 
    // right now: normal framebuffer, need to rotate during blit
    else if (this->options.use24bitRGB) {
        // 24-bit RGB, 32-bit stride
        uint32_t* srcPtr = (uint32_t*)frameData.swapchainFramePtr->data();
        uint32_t* dstPtr = static_cast<uint32_t*>(REAL_SCREEN_BASE_ADDRESS);
        for (int col = 0; col < SCREEN_HEIGHT; ++col) {
            int flippedCol = (SCREEN_HEIGHT - 1) - col;
            uint32_t* outcol = dstPtr + flippedCol;
            for(int row = 0; row < SCREEN_WIDTH; ++row, outcol += SCREEN_HEIGHT)
                *outcol = *srcPtr++;
        }
        return;
    } else {
        // 16-bit RGB565
        uint16_t* srcPtr = (uint16_t*)frameData.swapchainFramePtr->data();
        uint16_t* dstPtr = static_cast<uint16_t*>(REAL_SCREEN_BASE_ADDRESS);
        for (int col = 0; col < SCREEN_HEIGHT; ++col) {
            int flippedCol = (SCREEN_HEIGHT - 1) - col;
            uint16_t* outcol = dstPtr + flippedCol;
            for(int row = 0; row < SCREEN_WIDTH; ++row, outcol += SCREEN_HEIGHT)
                *outcol = *srcPtr++;
        }
        return;
    }
}

std::string VideoPlayer::dumpState() const {
    std::string state;
    state += "VideoPlayer State Dump:\n";
    state += "-----------------------\n";
    state += "Video File: " + std::string(this->videoFile ? "Open" : "Closed") + "\n";
    state += "Decoder Read Head: " + std::to_string(this->decoderReadHead) + "\n";
    state += "Decoder Read Available: " + std::to_string(this->decoderReadAvailable) + "\n";
    state += "Decoded Frames Swapchain Available Count: " + 
        std::to_string(this->decodedFramesSwapchain.availableCount()) + "\n";
    state += "Frames In Flight Queue Size: " + 
        std::to_string(this->framesInFlightQueue.size()) + "\n";
    state += "Video Dimensions: " + 
        std::to_string(this->videoWidth) + "x" + std::to_string(this->videoHeight) + "\n";
    state += "Video Timing Info:\n";
    state += "  Time Increment Resolution: " + 
        std::to_string(this->videoTimingInfo.timeIncrementResolution) + "\n";
    state += "  Fixed VOP Rate: " + 
        std::string(this->videoTimingInfo.fixedVopRate ? "Yes" : "No") + "\n";
    state += "  Fixed VOP Time Increment: " + 
        std::to_string(this->videoTimingInfo.fixedVopTimeIncrement) + "\n";
    state += "Last Frame Blit Time (ticks): " + 
        std::to_string(this->lastFrameBlitTime) + "\n";
    state += "Failed Flag: " + std::string(this->failedFlag ? "True" : "False") + "\n";
    state += "Error Message: " + this->errorMsg + "\n";

    state += "-----------------------\n";
    state += "Profiling Info Summary (ticks):\n";
    state += "I dec: " + this->short_stats(this->profilingInfo.IFrame_DecodeTimes) + "\n";
    state += "P dec: " + this->short_stats(this->profilingInfo.PFrame_DecodeTimes) + "\n";
    state += "B dec: " + this->short_stats(this->profilingInfo.BFrame_DecodeTimes) + "\n";
    state += "S dec: " + this->short_stats(this->profilingInfo.SFrame_DecodeTimes) + "\n";
    state += "Wasted dec: " + this->short_stats(this->profilingInfo.WastedFrame_DecodeTimes) + "\n";
    state += "Blit: " + this->short_stats(this->profilingInfo.Frame_BlitTimes) + "\n";
    state += "Memmove times (bytes/tick): " + this->short_stats(
        [this]{
            std::vector<uint32_t> out;
            out.reserve(profilingInfo.Buffer_RefillTimes.size() * 2);
            for (const auto& [mmTime, bytesMoved, freadTime, bytesRead] : profilingInfo.Buffer_RefillTimes) {
                out.push_back(bytesMoved / (mmTime ? mmTime : 1));
            }
            return out;
        }()
    ) + "\n";
    state += "File Read Times (bytes/tick): " + this->short_stats(
        [this]{
            std::vector<uint32_t> out;
            out.reserve(profilingInfo.Buffer_RefillTimes.size());
            for (const auto& [mmTime, bytesMoved, freadTime, bytesRead] : profilingInfo.Buffer_RefillTimes) {
                out.push_back(bytesRead / (freadTime ? freadTime : 1));
            }
            return out;
        }()
    ) + "\n";
    state += "Pacing Wait Times: " + this->short_stats(this->profilingInfo.Pacing_WaitTimes) + "\n";
    state += "Frame too late count: " + std::to_string(
        std::count_if(
            this->profilingInfo.Pacing_WaitTimes.begin(),
            this->profilingInfo.Pacing_WaitTimes.end(),
            [](int32_t v) { return v < 0; }
        )
    ) + "\n";
    state += "Total Frame Times: " + this->short_stats(this->profilingInfo.Frame_TotalTimes) + "\n";
    state += "Average FPS: " + std::to_string(
        [this]() -> float {
            uint64_t totalTicks = 0;
            for (const auto& t : this->profilingInfo.Frame_TotalTimes) {
                totalTicks += t;
            }
            if (totalTicks == 0) {
                return 0.0;
            }
            float totalSeconds = static_cast<float>(totalTicks) / static_cast<float>(timerHz);
            return static_cast<float>(this->profilingInfo.Frame_TotalTimes.size()) / totalSeconds;
        }()
    ) + "\n";
    return state;
}