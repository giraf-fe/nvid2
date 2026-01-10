# nvid2
new ti nspire video player using ndless + MPEG-4 Part 2 codec \
inspired by the older project [nvid](https://github.com/pbfy0/nvid)

**Current state: Only CX-II compatible, HW-W+** \
Includes the II-T and CAS models

uses a stripped down version of xvid, arm assembly paths may come soon to improve performance

ffmpeg command: \
`ffmpeg -i YOUR_VIDEO.extension -map 0:0 -c:v libxvid -bf 0 -gmc 0 -pix_fmt yuv420p -vf "scale=320:240,fps=24" -me_quality 6 -mbd rd -trellis 1 -b:v 500k -f m4v YOUR-VIDEO.tns`

### Pre-rotated videos (performance tip)
Since the display on the CX-II is rotated 90 degrees, pre-rotating the video skips needing to rotate 
the decoded output at runtime. This can save ~3.2ms per frame at 16bpp, double at 24bpp.

Use the option `-prv` to tell the player that the video is pre-rotated.

For a clockwise 90° rotation, use FFmpeg's `transpose=1` and swap the scale to **240x320**:

`ffmpeg -i YOUR_VIDEO.extension -map 0:0 -c:v libxvid -bf 0 -gmc 0 -pix_fmt yuv420p -vf "transpose=1,scale=240:320,fps=24" -me_quality 6 -mbd rd -trellis 1 -b:v 500k -f m4v YOUR-VIDEO.tns`

Then play it with `-prv` **and** `Nmfb` (The magic framebuffer is no longer needed for hardware rotation):

`play YOUR-VIDEO.tns -Nmfb -prv`

Notes:
 - `-prv` expects the encoded dimensions to be **240x320** (width x height). If you feed it 320x240, it will error.
 - `transpose` has multiple modes; if `transpose=1` rotates the “wrong way” for your source, adjust the transpose mode accordingly.
## parameter explanations:
 - `-map 0:0`: the video stream is usually the first input stream, choose something else if ffprobe says something different
 - `-c:v libxvid`: uses the libxvid encoder for mpeg4 part 2 video. the project uses the xvid decoder
 - `-bf 0`: disables b-frames, easier on the decoder
 - `-gmc 0`: global motion compensation, harder on the decoder when on
 - `-pix_fmt yuv420p`: format should be easier on the decoder
 - `-vf "scale=320:240,fps=24"`: the ti-nspire's display is 320x240, setting the resolution exactly removes the need for filtering on-device. set the fps to any value you like
 - `-me_quality 6`: increases motion estimation effort to max. this increases encoder efficiency at the expense of encode time, while minimally affecting decode effort. since mpeg4 part 2 is such an old codec, the maximum setting is still very easy for modern computers to handle.
 - `-mbd rd`: macroblock decision mode, same explanation as above
 - `-trellis 1`: quantization, same explanation as above
 - `-b:v 500k`: sets the video bitrate to average 500 kbps. a minute of video would then be around 3.75 MB. this number can be increased or decreased, depending on your target quality
 - `-f m4v`: sets the file format as an elementary mpeg4 part 2 stream, required.

## playing a video
Opening nvid2 will place you into a terminal-like interface with 3 commands:
 - ls: list stuff in directory
 - cd: change directory
 - play: play a video file (the thing you encoded with ffmpeg)

When playing a video, you can press esc to stop.

### `play` options
Usage:
 - `play <filename> [options...]`

Flags (later flags override earlier ones):
 - `-b`: benchmark mode (no video output)
 - `-bdb`: blit frames even in benchmark mode

Output / framebuffer mode:
 - `-mfb`: use the magic framebuffer (**default: on**)
 - `-24bpp`: use 24-bit RGB framebuffer (higher bandwidth; incompatible with magic framebuffer)
 - `-lcdblit`: use Ndless's LCD blit API (incompatible with magic framebuffer and 24bpp)
 - `-prv`: pre-rotated video (no rotation during blit; video must be pre-rotated to 240x320)

Decode quality / latency:
 - `-fd`: fast decoding (**default: on**) (lower CPU usage, lower quality)
 - `-ld`: low-delay mode (**default: on**) (reduces latency; disables b-frame support)
 - `-dbl` / `-dbc`: enable luma / chroma deblocking filter
 - `-drl` / `-drc`: enable luma / chroma deringing filter

Turning off defaults:
 - Any option that is on by default can be disabled with the opposite flag form: `-N...` (example: `-Nmfb` disables the magic framebuffer).

Incompatibilities enforced by the player:
 - `-mfb` cannot be combined with `-24bpp` or `-lcdblit`.
 - `-prv` cannot be combined with `-mfb` or `-lcdblit`.

Examples:
 - Normal playback (defaults): `play video.tns`
 - Benchmark decode only: `play video.tns -b`
 - Benchmark but still show frames: `play video.tns -b -bdb`
 - Set the LCD to use 24-bit color: `play video.tns -24bpp`
 - Pre-rotated playback (skip rotation work): `play video.tns -Nmfb -prv`
 - All deblock + dering filters (very slow): `play video.tns -dbl -dbc -drl -drc`

## Additional notes
 - **b frames are not supported.** The decode loop does not support the extra logic required for B-frames. This may change in the future.
 - you can use ffmpeg's native mpeg4 encoder if you want, but it likely has a different set of flags
 - if you set the output file's file extension as *.m4v, the container format will change and decoding will fail. the -f flag makes it a raw stream, *.tns isn't recognized by ffmpeg so it ignores it
 - try out a two pass decode on your video
 - all the budget went to the video player architecture, the ui is horrendous. anyone is free to contribute a nicer ui or create a fork

## Performance
Currently, 30 fps and above is quite easily achievable, better than the older nvid which used the more complex vp8 codec.
60 fps realtime may become possible in the future when overclocked; YV12 -> RGB565 conversion currently takes the most
time per frame. For some odd reason, using 24bpp color with pre-rotated video is actually faster than RGB565. Xvid's 
conversion function for RGB565 is probably not that well optimized.

# License
This project is licensed under GPLv2 (because of xvid).  
Copyright (C) 2026 giraf-fe