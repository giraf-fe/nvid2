# nvid2
new ti nspire video player using ndless + MPEG-4 Part 2 codec \
inspired by the older project [nvid](https://github.com/pbfy0/nvid)

uses a stripped down version of xvid, arm assembly paths may come soon to improve performance

ffmpeg command: \
`ffmpeg -i YOUR_VIDEO.extension -map 0:0 -c:v libxvid -bf 0 -gmc 0 -pix_fmt yuv420p -vf "scale=320:240,fps=24" -me_quality 6 -mbd rd -trellis 1 -b:v 500k -f m4v YOUR-VIDEO.tns`
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

## additional notes
 - **b frames are not supported.** this may change in the future
 - you can use ffmpeg's native mpeg4 encoder if you want, but it likely has a different set of flags
 - if you set the output file's file extension as *.m4v, the container format will change and decoding will fail. the -f flag makes it a raw stream, *.tns isn't recognized by ffmpeg so it ignores it
 - the decoder disables all post processing effects to increase decode speed (no deblocking or deringing). 
 - try out a two pass decode on your video
 - all the budget went to the video player architecture, the ui is horrendous. anyone is free to contribute a nicer ui or create a fork

## performance
currently, 30 fps and above is achievable on an TI-Nspire CX II, better than the older nvid which used the more complex vp8 codec.

# License
This project is licensed under GPLv2 (because of xvid).  
Copyright (C) 2026 giraf-fe