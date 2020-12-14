#!/bin/sh


cd ../3rd_party/ffmpeg*

diff -uN libavformat/hlsenc.c ../../patch/ffmpeg/libavformat/hlsenc.c > ../../patch/ffmpeg_hlsen_c.patch
diff -uN libavformat/http.c ../../patch/ffmpeg/libavformat/http.c > ../../patch/ffmpeg_http_c.patch
diff -uN libavformat/rtsp.c ../../patch/ffmpeg/libavformat/rtsp.c > ../../patch/ffmpeg_rtsp_c.patch
diff -uN libavformat/rtsp.h ../../patch/ffmpeg/libavformat/rtsp.h > ../../patch/ffmpeg_rtsp_h.patch



