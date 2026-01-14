#include "VideoPlayer.hpp"

#include <xvid.h>
#include <decoder.h>

#include <stdint.h>
#include <stddef.h>

#include <nspireio/uart.hpp>

typedef struct {
    int      ok;      // 1 if parsed
    uint16_t R;       // vop_time_increment_resolution (ticks/sec)
    uint8_t  fixed;   // fixed_vop_rate
    uint16_t inc;     // fixed_vop_time_increment (ticks/frame) if fixed
    uint8_t  inc_bits;

    // Optional geometry (rectangular only)
    uint16_t width;
    uint16_t height;
} vol_timing_t;

/* --- tiny MSB-first bitreader with 64-bit cache --- */
typedef struct {
    const uint8_t *p, *end;
    uint64_t cache;
    int bits; // number of valid bits in cache
} br_t;

static inline void br_fill(br_t *br) {
    while (br->bits <= 56 && br->p < br->end) {
        br->cache = (br->cache << 8) | (uint64_t)(*br->p++);
        br->bits += 8;
    }
}

static inline int br_need(br_t *br, int n) {
    br_fill(br);
    return br->bits >= n;
}

static inline uint32_t br_get(br_t *br, int n) {
    int shift = br->bits - n;
    uint32_t out;
    if (n == 32) out = (uint32_t)(br->cache >> shift);
    else out = (uint32_t)((br->cache >> shift) & ((1u << n) - 1u));
    br->bits -= n;
    return out;
}

static inline int time_inc_bits(uint16_t R) {
    // bits needed to represent [0..R-1]
    if (R <= 1) return 1;
    uint32_t v = (uint32_t)R - 1;
    int b = 0;
    while (v) { ++b; v >>= 1; }
    return b ? b : 1;
}

// Strictly consume a marker_bit; returns 0 if unavailable.
static inline int br_marker(br_t *br) {
    if (!br_need(br, 1)) return 0;
    (void)br_get(br, 1); // spec says it should be '1'; we don't enforce to be tolerant
    return 1;
}

/*
 * Parse VOL payload that starts immediately AFTER "00 00 01 2x".
 * payload_len should extend through the VOL header (typically until next startcode).
 * parse_wh: if nonzero, also parses width/height for rectangular shape.
 */
static vol_timing_t parse_vol_timing(const uint8_t *payload, size_t payload_len, int parse_wh) {
    vol_timing_t out = {0};
    br_t br = { payload, payload + payload_len, 0, 0 };

    uint32_t v;
    uint32_t verid = 1; // default when is_object_layer_identifier == 0

    // random_accessible_vol (1)
    if (!br_need(&br, 1)) return out;
    (void)br_get(&br, 1);

    // video_object_type_indication (8)
    if (!br_need(&br, 8)) return out;
    (void)br_get(&br, 8);

    // is_object_layer_identifier (1)
    if (!br_need(&br, 1)) return out;
    uint32_t olid = br_get(&br, 1);
    if (olid) {
        // video_object_layer_verid (4)
        if (!br_need(&br, 4)) return out;
        verid = br_get(&br, 4);

        // video_object_layer_priority (3)
        if (!br_need(&br, 3)) return out;
        (void)br_get(&br, 3);
    }

    // aspect_ratio_info (4)
    if (!br_need(&br, 4)) return out;
    uint32_t ar = br_get(&br, 4);
    if (ar == 15) {
        // par_width (8) + par_height (8)
        if (!br_need(&br, 16)) return out;
        (void)br_get(&br, 16);
    }

    // vol_control_parameters (1)
    if (!br_need(&br, 1)) return out;
    uint32_t vcp = br_get(&br, 1);
    if (vcp) {
        // chroma_format (2)
        if (!br_need(&br, 2)) return out;
        (void)br_get(&br, 2);

        // low_delay (1)
        if (!br_need(&br, 1)) return out;
        (void)br_get(&br, 1);

        // vbv_parameters (1)
        if (!br_need(&br, 1)) return out;
        uint32_t vbv = br_get(&br, 1);
        if (vbv) {
            // first_half_bit_rate (15), marker, latter_half_bit_rate (15), marker
            if (!br_need(&br, 15)) return out; (void)br_get(&br, 15);
            if (!br_marker(&br)) return out;
            if (!br_need(&br, 15)) return out; (void)br_get(&br, 15);
            if (!br_marker(&br)) return out;

            // first_half_vbv_buffer_size (15), marker, latter_half_vbv_buffer_size (3), marker
            if (!br_need(&br, 15)) return out; (void)br_get(&br, 15);
            if (!br_marker(&br)) return out;
            if (!br_need(&br, 3)) return out;  (void)br_get(&br, 3);
            if (!br_marker(&br)) return out;

            // first_half_vbv_occupancy (11), marker, latter_half_vbv_occupancy (15), marker
            if (!br_need(&br, 11)) return out; (void)br_get(&br, 11);
            if (!br_marker(&br)) return out;
            if (!br_need(&br, 15)) return out; (void)br_get(&br, 15);
            if (!br_marker(&br)) return out;
        }
    }

    // video_object_layer_shape (2)
    if (!br_need(&br, 2)) return out;
    uint32_t shape = br_get(&br, 2);

    // video_object_layer_shape_extension (4) only if grayscale (shape==3) AND verid != 1
    if (shape == 3 && verid != 1) {
        if (!br_need(&br, 4)) return out;
        (void)br_get(&br, 4);
    }

    // marker_bit (1)
    if (!br_marker(&br)) return out;

    // vop_time_increment_resolution (16)
    if (!br_need(&br, 16)) return out;
    out.R = (uint16_t)br_get(&br, 16);
    out.inc_bits = (uint8_t)time_inc_bits(out.R);

    // marker_bit (1)
    if (!br_marker(&br)) return out;

    // fixed_vop_rate (1)
    if (!br_need(&br, 1)) return out;
    out.fixed = (uint8_t)br_get(&br, 1);

    if (out.fixed) {
        if (!br_need(&br, out.inc_bits)) return out;
        out.inc = (uint16_t)br_get(&br, out.inc_bits);
    }

    // Optional: width/height if rectangular shape (shape==0)
    if (parse_wh && shape == 0) {
        // marker, width(13), marker, height(13), marker
        if (!br_marker(&br)) return out;
        if (!br_need(&br, 13)) return out;
        out.width = (uint16_t)br_get(&br, 13);
        if (!br_marker(&br)) return out;
        if (!br_need(&br, 13)) return out;
        out.height = (uint16_t)br_get(&br, 13);
        if (!br_marker(&br)) return out;
    }

    out.ok = (out.R != 0);
    return out;
}

static size_t findVOLStartCode(const uint8_t* p, size_t n) {
    // looks for 00 00 01 2x where x is 0..F
    if (n < 4) return (size_t)-1;
    for (size_t i = 0; i + 3 < n; ++i) {
        if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x01) {
            uint8_t code = p[i+3];
            if (code >= 0x20 && code <= 0x2F) return i;
        }
    }
    return (size_t)-1;
}

static inline std::string bytes_to_hex(const std::uint8_t* data, std::size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";

    std::string out;
    out.resize(len * 2);

    for (std::size_t i = 0; i < len; ++i) {
        const std::uint8_t b = data[i];
        out[2 * i + 0] = kHex[b >> 4];
        out[2 * i + 1] = kHex[b & 0x0F];
    }
    return out;
}

std::string GetXvidErrorMessage(int errorCode) {
    switch (errorCode)
    {
    case XVID_ERR_FAIL:
        return "Generic failure (XVID_ERR_FAIL)";
    case XVID_ERR_MEMORY:
        return "Allocation failed (XVID_ERR_MEMORY)";
    case XVID_ERR_FORMAT:
        return "Invalid bitstream format (XVID_ERR_FORMAT)";
    case XVID_ERR_VERSION:
        return "Version mismatch (XVID_ERR_VERSION)";
    case XVID_ERR_END:
        return "End of stream reached (XVID_ERR_END)";
    default:
        return "Unknown error";
    }
}

void VideoPlayer::readVOLHeader() {
    xvid_dec_frame_t decFrame{};
    decFrame.version = XVID_VERSION;

    // header is first read, need discontinuity flag
    decFrame.general = XVID_DISCONTINUITY |
        (this->options.fastDecoding ? XVID_DEC_FAST : 0) | 
        (this->options.lowDelayMode ? XVID_LOWDELAY : 0);
    decFrame.bitstream = (void*)(this->fileReadBuffer.get() + this->decoderReadHead);
    decFrame.length = this->decoderReadAvailable;
    decFrame.output.csp = XVID_CSP_NULL;
    decFrame.output.plane[0] = nullptr;
    decFrame.output.stride[0] = 0;

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
        this->errorMsg = "Failed to decode VOL header: " + GetXvidErrorMessage(bytesConsumed);
        return;
    } 
    if(bytesConsumed == 0) {
        // no bytes consumed, need more data
        this->failedFlag = true;
        this->errorMsg = "Insufficient data to decode VOL header";
        return;
    }

    if(decStats.type != XVID_TYPE_VOL) {
        this->failedFlag = true;
        this->errorMsg = "Expected VOL header, got different data type: " + std::to_string(decStats.type);
        uart_puts((this->dumpState() + "\n").c_str());
        uart_puts(("Bitstream (hex): " + bytes_to_hex(
            (const uint8_t*)(this->fileReadBuffer.get() + this->decoderReadHead),
            std::min((size_t)256, this->decoderReadAvailable)
        ) + "\n").c_str());
        return;
    }

    // use custom vol parser
    const size_t vol_StartCodePosition = findVOLStartCode(
        (const uint8_t*)(this->fileReadBuffer.get() + this->decoderReadHead),
        this->decoderReadAvailable
    );
    if(vol_StartCodePosition == (size_t)-1 || vol_StartCodePosition + 4 >= this->decoderReadAvailable) {
        this->failedFlag = true;
        this->errorMsg = "Failed to find VOL start code in bitstream";
        return;
    }
    const uint8_t* vol_payload = 
        (const uint8_t*)(this->fileReadBuffer.get() + this->decoderReadHead + vol_StartCodePosition + 4);
    const size_t vol_payload_len = 
        this->decoderReadAvailable - vol_StartCodePosition - 4;

    vol_timing_t volTiming = parse_vol_timing(vol_payload, vol_payload_len, 0);
    if(!volTiming.ok) {
        this->failedFlag = true;
        this->errorMsg = "Failed to parse VOL timing information";
        return;
    }

    // get/fill video information
    this->videoWidth = decStats.data.vol.width;
    this->videoHeight = decStats.data.vol.height;
    this->videoTimingInfo.timeIncrementResolution = volTiming.R;
    this->videoTimingInfo.fixedVopRate = (volTiming.fixed != 0);
    this->videoTimingInfo.fixedVopTimeIncrement = volTiming.inc;

    // consumed some bytes, move read head
    this->decoderReadHead += bytesConsumed;
    this->decoderReadAvailable -= bytesConsumed;
}

