#include "VideoPlayer.hpp"

#include <xvid.h>
#include <decoder.h>

#include <utility>

#include <nspireio/uart.hpp>

HandleInsufficientDataResult VideoPlayer::handleInsufficientData(
    uint32_t frameDecodeStartTicks,
    FrameBufferType* frameBuffer,
    bool& hadDiscontinuity,
    const char* errorContext,
    bool requireDiscontinuity
) {
    profilingInfo.WastedFrame_DecodeTimes.push_back(
        frameDecodeStartTicks - this->frameTimer.getCurrentValue32()
    );

    // Always release the acquired buffer on any insufficient-data path.
    // (Callers only keep ownership when a decoded frame is pushed.)
    this->decodedFramesSwapchain.release(frameBuffer);
    
    if (this->decoderReadAvailable == SIZEOF_FILE_READ_BUFFER) {
        // buffer full but no progress, error
        this->failedFlag = true;
        this->errorMsg = std::string("Decoder stalled: ") + errorContext;
        return HandleInsufficientDataResult::Error;
    }
    
    if (!this->fileEndReached) {
        // fillReadBuffer() returns true when more data may still be available.
        this->fileEndReached = !this->fillReadBuffer();
    }

    if (this->fileEndReached) {
        if (this->decoderReadAvailable == 0) {
            return HandleInsufficientDataResult::EndOfFile;
        }

        // EOF reached but decoder still wants more data.
        // Drop remaining trailing bytes to avoid an infinite decode loop.
        this->decoderReadHead += this->decoderReadAvailable;
        this->decoderReadAvailable = 0;
        return HandleInsufficientDataResult::EndOfFile;
    }

    // Only flag discontinuity when the caller knows decoder state might be
    // inconsistent with upcoming input (e.g., partial consume beyond buffer).
    if (requireDiscontinuity) {
        hadDiscontinuity = true;
    }
    return HandleInsufficientDataResult::Success;
}

void VideoPlayer::advanceReadHead(int bytesConsumed) {
    this->decoderReadHead += bytesConsumed;
    this->decoderReadAvailable -= bytesConsumed;
}

void VideoPlayer::fillFramesInFlightQueue() {
    bool hadDiscontinuity = false;

    while (!this->framesInFlightQueue.full() && this->decodedFramesSwapchain.availableCount() > 0) {
        uint32_t frameDecodeStartTicks = this->frameTimer.getCurrentValue32();

        xvid_dec_frame_t decFrame{};
        decFrame.version = XVID_VERSION;
        
        decFrame.general = 
            (this->options.fastDecoding ? XVID_DEC_FAST : 0) | 
            (this->options.lowDelayMode ? XVID_LOWDELAY : 0) |
            (this->options.deblockLuma ? XVID_DEBLOCKY : 0) |
            (this->options.deblockChroma ? XVID_DEBLOCKUV : 0) |
            (this->options.deringLuma ? XVID_DERINGY : 0) |
            (this->options.deringChroma ? XVID_DERINGUV : 0)
            ;

        decFrame.general |= (hadDiscontinuity ? XVID_DISCONTINUITY : 0);
        decFrame.bitstream = (void*)(this->fileReadBuffer.get() + this->decoderReadHead);
        decFrame.length = this->decoderReadAvailable;
        
        decFrame.output.csp = XVID_CSP_RGB565;
        if(this->options.benchmarkMode && !this->options.blitDuringBenchmark) {
            // in benchmark mode without blitting, skip color conversion to measure true decode speed
            decFrame.output.csp = XVID_CSP_INTERNAL;
        }

        FrameBufferType* frameBuffer = this->decodedFramesSwapchain.acquire();
        if(!frameBuffer) {
            // should not happen due to while condition
            this->failedFlag = true;
            this->errorMsg = "Failed to get Framebuffer from SwapChain";
            return;
        }
        decFrame.output.plane[0] = frameBuffer->data();
        decFrame.output.stride[0] = (this->options.preRotatedVideo ? SCREEN_HEIGHT : SCREEN_WIDTH) * SIZEOF_RGB565;

        xvid_dec_stats_t decStats{};
        decStats.version = XVID_VERSION;

        int bytesConsumed = xvid_decore(
            this->xvidDecoderHandle,
            XVID_DEC_DECODE,
            &decFrame,
            &decStats
        );
        if (bytesConsumed < 0) {
            // error
            this->failedFlag = true;
            this->errorMsg = "Failed to decode frame: " + GetXvidErrorMessage(bytesConsumed);
            return;
        } 
        if (bytesConsumed == 0) {
            // need more data
            auto result = handleInsufficientData(
                frameDecodeStartTicks, frameBuffer, hadDiscontinuity,
                "no bytes consumed with full input buffer",
                /*requireDiscontinuity=*/false
            );
            if (result == HandleInsufficientDataResult::Error ||
                result == HandleInsufficientDataResult::EndOfFile) {
                return;
            }
            continue;
        }
        // uart_puts((std::to_string(decStats.type) + " " + std::to_string(bytesConsumed) + "\n").c_str());
        if (decStats.type == XVID_TYPE_IVOP || decStats.type == XVID_TYPE_PVOP ||
            decStats.type == XVID_TYPE_BVOP || decStats.type == XVID_TYPE_SVOP) {
            // check if xvid read beyond provided data
            // this prevents a frame from decoding incomplete
            if ((size_t)bytesConsumed > this->decoderReadAvailable) {
                // dont advance the read head since we hit the end, read more data instead
                auto result = handleInsufficientData(
                    frameDecodeStartTicks, frameBuffer, hadDiscontinuity,
                    "read beyond available data with full input buffer, the file read buffer may be too small.",
                    /*requireDiscontinuity=*/false
                );
                if (result == HandleInsufficientDataResult::Error ||
                    result == HandleInsufficientDataResult::EndOfFile) {
                    return;
                }
                continue;
            }

            // successful decode
            this->framesInFlightQueue.push(FrameInFlightData<FrameBufferType>{
                .timingTicks = 
                (uint64_t)decStats.data.vop.time_base * this->videoTimingInfo.timeIncrementResolution +
                (uint64_t)decStats.data.vop.time_increment,
                .swapchainFramePtr = frameBuffer
            });

            [=]() -> std::vector<uint32_t>& {
                switch (decStats.type)
                {
                case XVID_TYPE_IVOP: return this->profilingInfo.IFrame_DecodeTimes;
                case XVID_TYPE_PVOP: return this->profilingInfo.PFrame_DecodeTimes;
                case XVID_TYPE_BVOP: return this->profilingInfo.BFrame_DecodeTimes;
                case XVID_TYPE_SVOP: return this->profilingInfo.SFrame_DecodeTimes;
                default:
                    throw 1; // unreachable
                }
            }().push_back(
                frameDecodeStartTicks - this->frameTimer.getCurrentValue32()
            );
            

            // advance read head
            advanceReadHead(bytesConsumed);
            hadDiscontinuity = false;
            continue;
        }
        if (decStats.type == XVID_TYPE_VOL) {
            // update video timing info
            this->readVOLHeader();
            if(this->failedFlag) {
                return;
            }
            this->decodedFramesSwapchain.release(frameBuffer);
            hadDiscontinuity = false;
            continue;
        }
        if(decStats.type == 5 /* internal nvop type*/) {
            // check if xvid read beyond provided data
            // this prevents a frame from decoding incomplete
            if ((size_t)bytesConsumed > this->decoderReadAvailable) {
                // dont advance the read head since we hit the end, read more data instead
                auto result = handleInsufficientData(
                    frameDecodeStartTicks, frameBuffer, hadDiscontinuity,
                    "read beyond available data with full input buffer, the file read buffer may be too small.",
                    /*requireDiscontinuity=*/false
                );
                if (result == HandleInsufficientDataResult::Error ||
                    result == HandleInsufficientDataResult::EndOfFile) {
                    return;
                }
                continue;
            }
            // ignore nvop frames
            this->decodedFramesSwapchain.release(frameBuffer);
            
            // advance read head
            advanceReadHead(bytesConsumed);
            hadDiscontinuity = false;
            continue;
        }
        // unexpected data type
        this->failedFlag = true;
        this->errorMsg = "Expected video frame, got different data type: " + std::to_string(decStats.type);
    }
}