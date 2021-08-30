#ifndef _H264_CODEC_H_
#define _H264_CODEC_H_


enum{
    H264CODEC_MODE_STREAM,
    H264CODEC_MODE_CB,
};

typedef void(*H264CODEC_DATA_CB)(void *p, int size);
void h264_codec_init(int width, int height, int mode, void *arg);
void h264_codec_deinit();
void h264_codec_yuv2h264(const void *p, int size);


#endif /*_H264_CODEC_H_*/