#include "h264_codec.h"
#include "vencoder.h"
#include <time.h>
#include <stdio.h>

#include "debug.h"
#include <cutils/log.h>
#undef LOG_TAG
#define LOG_TAG "codecTest"

VencBaseConfig baseConfig;
VencAllocateBufferParam bufferParam;
VideoEncoder *pVideoEnc = NULL;
VencInputBuffer inputBuffer;
VencOutputBuffer outputBuffer;
VencHeaderData sps_pps_data;
VencH264Param h264Param;

int h264codec_mode = 0;
FILE* h264codec_out = 0;
H264CODEC_DATA_CB h264codec_data_cb = 0;

void h264_codec_init(int width, int height, int mode, void *arg){
    h264codec_mode = mode;
    if(mode == H264CODEC_MODE_STREAM)
        h264codec_out = (FILE*)arg;
    else
        h264codec_data_cb = (H264CODEC_DATA_CB)arg;

    h264Param.bEntropyCodingCABAC = 1;
    h264Param.nBitrate = 4 * 1024 * 1024; /* bps */
    h264Param.nFramerate = 30;            /* fps */
    h264Param.nCodingMode = VENC_FRAME_CODING;
    //	h264Param.nCodingMode = VENC_FIELD_CODING;

    h264Param.nMaxKeyInterval = 30;
    h264Param.sProfileLevel.nProfile = VENC_H264ProfileMain;
    h264Param.sProfileLevel.nLevel = VENC_H264Level31;
    h264Param.sQPRange.nMinqp = 10;
    h264Param.sQPRange.nMaxqp = 40;

    memset(&baseConfig, 0, sizeof(VencBaseConfig));
    memset(&bufferParam, 0, sizeof(VencAllocateBufferParam));

    baseConfig.nInputWidth = width;
    baseConfig.nInputHeight = height;
    baseConfig.nStride = width;

    baseConfig.nDstWidth = width;
    baseConfig.nDstHeight = height;

    baseConfig.eInputFormat = VENC_PIXEL_YUYV422;
    bufferParam.nSizeY = baseConfig.nInputWidth * baseConfig.nInputHeight;
    bufferParam.nSizeC = baseConfig.nInputWidth * baseConfig.nInputHeight;
    bufferParam.nBufferNum = 4;

    pVideoEnc = VideoEncCreate(VENC_CODEC_H264);

    int value;
    VideoEncSetParameter(pVideoEnc, VENC_IndexParamH264Param, &h264Param);
    value = 0;
    VideoEncSetParameter(pVideoEnc, VENC_IndexParamIfilter, &value);
    value = 0; //degree
    VideoEncSetParameter(pVideoEnc, VENC_IndexParamRotation, &value);

    VideoEncInit(pVideoEnc, &baseConfig);

    unsigned int head_num = 0;
    VideoEncGetParameter(pVideoEnc, VENC_IndexParamH264SPSPPS, &sps_pps_data);

    if (h264codec_mode == H264CODEC_MODE_STREAM)
        fwrite(sps_pps_data.pBuffer, 1, sps_pps_data.nLength, h264codec_out);
    else
        h264codec_data_cb(sps_pps_data.pBuffer, sps_pps_data.nLength);

    logd("sps_pps_data.nLength: %d", sps_pps_data.nLength);
    for(head_num=0; head_num<sps_pps_data.nLength; head_num++)
        logd("the sps_pps :%02x\n", *(sps_pps_data.pBuffer+head_num));
    logd("VIDEO ENC INIT FINISH");

    AllocInputBuffer(pVideoEnc, &bufferParam);

}

void h264_codec_deinit(){
    ReleaseAllocInputBuffer(pVideoEnc);
    VideoEncUnInit(pVideoEnc);
    VideoEncDestroy(pVideoEnc);
}
void h264_codec_yuv2h264(const void *p, int size){
    GetOneAllocInputBuffer(pVideoEnc, &inputBuffer);

    const char *pp = (const char *)p;
    memcpy(inputBuffer.pAddrVirY, pp, baseConfig.nInputWidth * baseConfig.nInputHeight);
    memcpy(inputBuffer.pAddrVirC, pp + baseConfig.nInputWidth * baseConfig.nInputHeight, baseConfig.nInputWidth * baseConfig.nInputHeight);

    inputBuffer.bEnableCorp = 0;
    inputBuffer.sCropInfo.nLeft = 240;
    inputBuffer.sCropInfo.nTop = 240;
    inputBuffer.sCropInfo.nWidth = 240;
    inputBuffer.sCropInfo.nHeight = 240;

    FlushCacheAllocInputBuffer(pVideoEnc, &inputBuffer);

    AddOneInputBuffer(pVideoEnc, &inputBuffer);
    VideoEncodeOneFrame(pVideoEnc);

    AlreadyUsedInputBuffer(pVideoEnc, &inputBuffer);
    ReturnOneAllocInputBuffer(pVideoEnc, &inputBuffer);

    int cnt = 1, i;
    if (h264Param.nCodingMode == VENC_FIELD_CODING)
        cnt = 2;
    for( i = 0; i < cnt; i++){
        GetOneBitstreamFrame(pVideoEnc, &outputBuffer);
        if (h264codec_mode == H264CODEC_MODE_STREAM)
            fwrite(outputBuffer.pData0, 1, outputBuffer.nSize0, h264codec_out);
        else
            h264codec_data_cb(outputBuffer.pData0, outputBuffer.nSize0);

        if (outputBuffer.nSize1)
        {
            if (h264codec_mode == H264CODEC_MODE_STREAM)
                fwrite(outputBuffer.pData1, 1, outputBuffer.nSize1, h264codec_out);
            else
                h264codec_data_cb(outputBuffer.pData1, outputBuffer.nSize1);
        }
        FreeOneBitStreamFrame(pVideoEnc, &outputBuffer);
    }
}
