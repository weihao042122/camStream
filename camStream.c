#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h> /* getopt_long() */

#include <fcntl.h> /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>


#include "h264_codec.h"
#include "camera.h"
#include "debug.h"
#include "Utils.h"
#include "RTPEnc.h"
#include "Network.h"


#define SHOT_SAVE_DIR "/data/shot/"
#define CAMSTREAM_PIPE_FILE "/camStream_fifo"

#define SRC_WIDTH 640
#define SRC_HEIGHT 480

#define DST_WIDTH 640
#define DST_HEIGHT 480



static int stream_buf;
static int force_format;
static char *outfile = NULL;
static int list_frame_rate = 0;
static int cam_width = 0;
static int cam_height = 0;
static int mjpeg_frame = 0;
static int shot_cnt = 0;
static int shot_flag = 0;
static int yuv_stream_flag = 0;
static int h264_stream_flag = 0;
static int rtp_flag = 0;
RTPMuxContext rtpMuxContext;
UDPContext udpContext = {
    .dstPort = 1234         // destination port
};

#if 0
extern unsigned char *compressYUV422toJPEG(unsigned char *src, int width, int height, int *olen);
#else
void compressYUV422toJPEG(unsigned char* src, int width, int height, FILE* out_file);
#endif


static void shot_store(const void *p, int size, char *file)
{
    char filename[40];
    if (file == NULL)
        sprintf(filename, "%s%d.jpeg", SHOT_SAVE_DIR, ++shot_cnt);
    else
        sprintf(filename, "%s", file);
    FILE *fp = fopen(filename, "wb");
    fwrite(p, size, 1, fp);
    fclose(fp);
}

static void shot_jpeg_store(const void *p, int size, char *file)
{
    char filename[40];
    if (file == NULL)
        sprintf(filename, "%s%d.jpeg", SHOT_SAVE_DIR, ++shot_cnt);
    else
        sprintf(filename, "%s", file);
    FILE *fp = fopen(filename, "wb");
    compressYUV422toJPEG((unsigned char *)p, cam_width, cam_height, fp);
    fclose(fp);
}

void h264codec_rtp_data_cb(void *p, int len){
    rtpSendH264HEVC(&rtpMuxContext, &udpContext, p, len);
}
//process_image(数据指针，大小)
static void process_image(const void *p, int size)
{
    // 	fprintf(stderr, "size=%d \n", size);
    if (outfile)
    {
        if (mjpeg_frame)
        {
            shot_store(p, size, outfile);
        }
        else
        {
            shot_jpeg_store(p, size, outfile);
        }
    }
    else if (stream_buf)
    {
        if (mjpeg_frame)
        {
            fwrite(p, size, 1, stdout);
            if (shot_flag)
                shot_store(p, size, NULL);
        }
        else
        {
            if (yuv_stream_flag)
            {
                fwrite(p, size, 1, stdout);
            }
            else if (h264_stream_flag)
            {
                h264_codec_yuv2h264(p, size);
            }
            else
            {
#if 0
                int olen = 0;
                unsigned char *jpegBuf = compressYUV422toJPEG((unsigned char*)p, cam_width, cam_height, &olen);
                if (jpegBuf){
                    fwrite(jpegBuf, olen, 1, stdout);
                    if (shot_flag)
                        shot_store(jpegBuf , olen, NULL);
                    free(jpegBuf);
                }
#else
                compressYUV422toJPEG((unsigned char *)p, cam_width, cam_height, stdout);
#endif
            }
        }
        shot_flag = 0;
    }

    fflush(stderr);
    //     fprintf(stderr, ".");
    fflush(stdout);
}

static void *fifo_read_thread_func(void *arg)
{
    mkfifo(CAMSTREAM_PIPE_FILE, 777);
    mkdir(SHOT_SAVE_DIR, 777);
    FILE *fp = fopen(CAMSTREAM_PIPE_FILE, "r+");
    if (fp == NULL)
    {
        fprintf(stderr, "open %s failed", CAMSTREAM_PIPE_FILE);
        exit(EXIT_FAILURE);
    }
    char tbuf[32];

    while (1)
    {
        if (fgets(tbuf, sizeof(tbuf), fp) == NULL)
        {
            fprintf(stderr, "fifo read error, exit");
            continue;
        }
        if (strncmp(tbuf, "shot ", strlen("shot ")) == 0)
        {
            fprintf(stderr, "get cmd:%s\n", "shot");
            shot_flag = 1;
        }
        else if (strncmp(tbuf, "exit", strlen("exit")) == 0)
        {
            fprintf(stderr, "get cmd:%s\n", "shot");
            camera_stop();
        }
        else
        {
            fprintf(stderr, "get fifo :%s", tbuf);
        }
    }
    pthread_exit(NULL);
    return 0;
}

static void usage(FILE *fp, int argc, char **argv)
{
    fprintf(fp,
            "Usage: %s [options]\n\n"
            "Version 1.3\n"
            "Options:\n"
            "-d | --device name   Video device name [%s]\n"
            "-h | --help          Print this message\n"
            "-m | --mmap          Use memory mapped buffers [default]\n"
            "-r | --read          Use read() calls\n"
            "-u | --userp         Use application allocated buffers\n"
            "-s | --stream        Outputs stream to stdout\n"
            "-y | --yuv           yuyv format for output stream\n"
            "-2 | --h264          h264 format for output stream\n"
            "-f | --format        Force input format to 640x480 YUYV[0] or mjpeg[1]\n"
            "-c | --count         Number of frames to grab [%i]\n"
            "-o | --out           output one frame of jpeg to file \n"
            "-l | --list          list camera infomations\n"
            "-t | --rtp           rtp stream set udp target ip\n"
            "%s -f 0 -s -2 -c 100 > ttt.h264\n"
            "%s -f 0 -s -t 192.168.1.113 -c 100\n"
            "",
            argv[0], "/dev/video0", 1, argv[0], argv[0]);
}

static const char short_options[] = "d:hmrusy2f:c:o:lt:";

static const struct option
    long_options[] = {
        {"device", required_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},
        {"mmap", no_argument, NULL, 'm'},
        {"read", no_argument, NULL, 'r'},
        {"userp", no_argument, NULL, 'u'},
        {"stream", no_argument, NULL, 's'},
        {"yuv", no_argument, NULL, 'y'},
        {"h264", no_argument, NULL, '2'},
        {"format", required_argument, NULL, 'f'},
        {"count", required_argument, NULL, 'c'},
        {"out", required_argument, NULL, 'o'},
        {"list", no_argument, NULL, 'l'},
        {"rtp", required_argument, NULL, 't'},
        {0, 0, 0, 0}};

int main(int argc, char **argv)
{
    

    for (;;)
    {
        int idx;
        int c;

        c = getopt_long(argc, argv,
                        short_options, long_options, &idx);

        if (-1 == c)
            break;

        switch (c)
        {
        case 0: /* getopt_long() flag */
            break;

        case 'd':
            camera_set_devname(optarg);
            break;

        case 'h':
            usage(stdout, argc, argv);
            exit(EXIT_SUCCESS);

        case 'm':
            camera_io_method_set(IO_METHOD_MMAP);
            break;

        case 'r':
            camera_io_method_set(IO_METHOD_READ);
            break;

        case 'u':
            camera_io_method_set(IO_METHOD_USERPTR);
            break;

        case 's':
            stream_buf++;
            break;
        case 'y':
            yuv_stream_flag++;
            break;
        case '2':
            h264_stream_flag++;
            h264_codec_init(SRC_WIDTH, SRC_HEIGHT, H264CODEC_MODE_STREAM, (void *)stdout);
            break;
        case 'f':
            errno = 0;
            force_format++;
            mjpeg_frame = strtol(optarg, NULL, 0);
            if (errno)
                errno_exit(optarg);
            break;

        case 'c':
            errno = 0;
            if (!outfile){
                camera_set_frame_cnt(strtol(optarg, NULL, 0));
                if (errno)
                    errno_exit(optarg);
            } else {
                camera_set_frame_cnt(1);
            }
            break;
        case 'o':
            outfile = optarg;
            camera_set_frame_cnt(1);
            break;
        case 'l':
            list_frame_rate++;
            ;
            break;
        case 't':{
            udpContext.dstIp = optarg;
            rtp_flag = 1;
            int res = udpInit(&udpContext);
            if (res){
                printf("udpInit error.\n");
                return -1;
            }
            initRTPMuxContext(&rtpMuxContext);

            h264_stream_flag++;
            h264_codec_init(SRC_WIDTH, SRC_HEIGHT, H264CODEC_MODE_CB, (void*)h264codec_rtp_data_cb);

            break;
        }
        default:
            usage(stderr, argc, argv);
            exit(EXIT_FAILURE);
        }
    }

    open_device();
    init_device(&cam_width, &cam_height, force_format, mjpeg_frame, list_frame_rate, process_image);
    start_capturing();
    pthread_t mPthread;
    pthread_create(&mPthread, NULL, fifo_read_thread_func, NULL);

    camera_get_frame_loop();

    stop_capturing();
    uninit_device();
    close_device();
    fprintf(stderr, "\\n");
    return 0;
}
