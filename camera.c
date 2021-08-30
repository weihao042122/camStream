#include "camera.h"
#include <fcntl.h> /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "debug.h"

#include <linux/videodev2.h>

static const char *dev_name = "/dev/video0";
static enum io_method io = IO_METHOD_MMAP;
static int fd = -1;
struct buffer *buffers;
static unsigned int n_buffers;
int frame_count = 1;
static int exit_flag = 0;

CAMERA_DATA_CB get_one_frame_cb;

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define MMAP_BUF_CNT 3


static int xioctl(int fh, int request, void *arg)
{
    int r;

    do
    {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

static int read_frame(void)
{
    struct v4l2_buffer buf;
    unsigned int i;

    switch (io)
    {
    case IO_METHOD_READ:
        if (-1 == read(fd, buffers[0].start, buffers[0].length))
        {
            switch (errno)
            {
            case EAGAIN:
                return 0;

            case EIO:
                /* Could ignore EIO, see spec. */

                /* fall through */
            default:
                errno_exit("read");
            }
        }

        get_one_frame_cb(buffers[0].start, buffers[0].length);
        break;

    case IO_METHOD_MMAP:
        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
        {
            switch (errno)
            {
            case EAGAIN:
                return 0;

            case EIO:
                /* Could ignore EIO, see spec. */

                /* fall through */

            default:
                errno_exit("VIDIOC_DQBUF");
            }
        }

        assert(buf.index < n_buffers);

        get_one_frame_cb(buffers[buf.index].start, buf.bytesused);

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");
        break;

    case IO_METHOD_USERPTR:
        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
        {
            switch (errno)
            {
            case EAGAIN:
                return 0;

            case EIO:
                /* Could ignore EIO, see spec. */

                /* fall through */

            default:
                errno_exit("VIDIOC_DQBUF");
            }
        }

        for (i = 0; i < n_buffers; ++i)
            if (buf.m.userptr == (unsigned long)buffers[i].start && buf.length == buffers[i].length)
                break;

        assert(i < n_buffers);

        get_one_frame_cb((void *)buf.m.userptr, buf.bytesused);

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");
        break;
    }

    return 1;
}

//VIDIOC_STREAMOFF
void stop_capturing(void)
{
    enum v4l2_buf_type type;

    switch (io)
    {
    case IO_METHOD_READ:
        /* Nothing to do. */
        break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
            errno_exit("VIDIOC_STREAMOFF");
        break;
    }
}


//VIDIOC_STREAMON
void start_capturing(void)
{
    unsigned int i;
    enum v4l2_buf_type type;

    switch (io)
    {
    case IO_METHOD_READ:
        /* Nothing to do. */
        break;

    case IO_METHOD_MMAP:
        for (i = 0; i < n_buffers; ++i)
        {
            struct v4l2_buffer buf;

            CLEAR(buf);
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                errno_exit("VIDIOC_QBUF");
        }
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
            errno_exit("VIDIOC_STREAMON");
        break;

    case IO_METHOD_USERPTR:
        for (i = 0; i < n_buffers; ++i)
        {
            struct v4l2_buffer buf;

            CLEAR(buf);
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_USERPTR;
            buf.index = i;
            buf.m.userptr = (unsigned long)buffers[i].start;
            buf.length = buffers[i].length;

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                errno_exit("VIDIOC_QBUF");
        }
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
            errno_exit("VIDIOC_STREAMON");
        break;
    }
}

//释放申请的内存
void uninit_device(void)
{
    unsigned int i;

    switch (io)
    {
    case IO_METHOD_READ:
        free(buffers[0].start);
        break;

    case IO_METHOD_MMAP:
        for (i = 0; i < n_buffers; ++i)
            if (-1 == munmap(buffers[i].start, buffers[i].length))
                errno_exit("munmap");
        break;

    case IO_METHOD_USERPTR:
        for (i = 0; i < n_buffers; ++i)
            free(buffers[i].start);
        break;
    }

    free(buffers);
}

static void init_read(unsigned int buffer_size)
{
    buffers = calloc(1, sizeof(*buffers));

    if (!buffers)
    {
        fprintf(stderr, "Out of memory\\n");
        exit(EXIT_FAILURE);
    }

    buffers[0].length = buffer_size;
    buffers[0].start = malloc(buffer_size);

    if (!buffers[0].start)
    {
        fprintf(stderr, "Out of memory\\n");
        exit(EXIT_FAILURE);
    }
}

static void init_mmap(void)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);

    req.count = MMAP_BUF_CNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
    {
        if (EINVAL == errno)
        {
            fprintf(stderr, "%s does not support "
                            "memory mappingn",
                    dev_name);
            exit(EXIT_FAILURE);
        }
        else
        {
            errno_exit("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2)
    {
        fprintf(stderr, "Insufficient buffer memory on %s\\n",
                dev_name);
        exit(EXIT_FAILURE);
    }

    buffers = calloc(req.count, sizeof(*buffers));

    if (!buffers)
    {
        fprintf(stderr, "Out of memory\\n");
        exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers)
    {
        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
            errno_exit("VIDIOC_QUERYBUF");

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start =
            mmap(NULL /* start anywhere */,
                 buf.length,
                 PROT_READ | PROT_WRITE /* required */,
                 MAP_SHARED /* recommended */,
                 fd, buf.m.offset);
        if (MAP_FAILED == buffers[n_buffers].start)
            errno_exit("mmap");
    }
}

static void init_userp(unsigned int buffer_size)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
    {
        if (EINVAL == errno)
        {
            fprintf(stderr, "%s does not support "
                            "user pointer i/on",
                    dev_name);
            exit(EXIT_FAILURE);
        }
        else
        {
            errno_exit("VIDIOC_REQBUFS");
        }
    }

    buffers = calloc(4, sizeof(*buffers));

    if (!buffers)
    {
        fprintf(stderr, "Out of memory\\n");
        exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < 4; ++n_buffers)
    {
        buffers[n_buffers].length = buffer_size;
        buffers[n_buffers].start = malloc(buffer_size);

        if (!buffers[n_buffers].start)
        {
            fprintf(stderr, "Out of memory\\n");
            exit(EXIT_FAILURE);
        }
    }
}
static void get_device_info()
{
    struct v4l2_capability cap;
    struct v4l2_fmtdesc fmt_desc;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap))
    {
        if (EINVAL == errno)
        {
            fprintf(stderr, "%s is no V4L2 device\\n",
                    dev_name);
            exit(EXIT_FAILURE);
        }
        else
        {
            errno_exit("VIDIOC_QUERYCAP");
        }
    }
    // Print capability infomations
    fprintf(stderr, "Capability Informations:\n");
    fprintf(stderr, " driver: %s\n", cap.driver);
    fprintf(stderr, " card: %s\n", cap.card);
    fprintf(stderr, " bus_info: %s\n", cap.bus_info);
    fprintf(stderr, " version: 0x%08X\n", cap.version);
    fprintf(stderr, " capabilities: 0x%08X\n", cap.capabilities);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr, "%s is no video capture device\\n",
                dev_name);
        exit(EXIT_FAILURE);
    }

    switch (io)
    {
    case IO_METHOD_READ:
        if (!(cap.capabilities & V4L2_CAP_READWRITE))
        {
            fprintf(stderr, "%s does not support read i/o\\n",
                    dev_name);
            exit(EXIT_FAILURE);
        }
        break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
        if (!(cap.capabilities & V4L2_CAP_STREAMING))
        {
            fprintf(stderr, "%s does not support streaming i/o\\n",
                    dev_name);
            exit(EXIT_FAILURE);
        }
        break;
    }

    fprintf(stderr, "=======================\n");
    memset(&fmt_desc, 0, sizeof(struct v4l2_fmtdesc));
    fmt_desc.index = 0;
    fmt_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (-1 != xioctl(fd, VIDIOC_ENUM_FMT, &fmt_desc))
    {
        fprintf(stderr, "fmt_desc.index=%d\n", fmt_desc.index);
        fprintf(stderr, "fmt_desc.type=%d\n", fmt_desc.type);
        fprintf(stderr, "fmt_desc.flags=%d\n", fmt_desc.flags);
        fprintf(stderr, "fmt_desc.description:[%s]\n", fmt_desc.description);
        fprintf(stderr, "fmt_desc.pixelformat=0x%x\n", fmt_desc.pixelformat);
        fprintf(stderr, "+++++++\n");
        struct v4l2_frmivalenum frmival;
        CLEAR(frmival);
        frmival.index = 0;
        frmival.pixel_format = fmt_desc.pixelformat;
        frmival.width = 640;
        frmival.height = 480;
        while (-1 != xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival))
        {
            fprintf(stderr, "\tfrmival.index=%d\n", frmival.index);
            fprintf(stderr, "\tfrmival.pixel_format=%x\n", frmival.pixel_format);
            fprintf(stderr, "\tfrmival.width=%d\n", frmival.width);
            fprintf(stderr, "\tfrmival.height=%d\n", frmival.height);
            fprintf(stderr, "\tfrmival.type=%d\n", frmival.type);
            fprintf(stderr, "\tfrmival.discrete.numerator=%d\n", frmival.discrete.numerator);
            fprintf(stderr, "\tfrmival.discrete.denominator=%d\n", frmival.discrete.denominator);
            frmival.index++;
            fprintf(stderr, "+++++++\n");
        }
        fprintf(stderr, "\n");
        fmt_desc.index++;
    }
    fprintf(stderr, "=======================\n");
}

void init_device(int *cam_width, int *cam_height, int force_format, int mjpeg_frame, int list_frame_rate, CAMERA_DATA_CB cb)
{
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    get_one_frame_cb = cb;
    unsigned int min;
    exit_flag = 0;
    /* Select video input, video standard and tune here. */

    CLEAR(cropcap);

    struct v4l2_input inp;

    inp.index = 0;
    if (-1 == ioctl(fd, VIDIOC_S_INPUT, &inp))
    {
        fprintf(stderr, "VIDIOC_S_INPUT %d error!\n", 0);
        errno_exit("VIDIOC_S_INPUT");
    }

    /* set v4l2_captureparm.timeperframe */
    struct v4l2_streamparm stream_parm;
    stream_parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_G_PARM, &stream_parm))
    {
        fprintf(stderr, "VIDIOC_G_PARM error\n");
    }
    else
    {
        stream_parm.parm.capture.capturemode = 0x0002;
        stream_parm.parm.capture.timeperframe.numerator = 1;
        stream_parm.parm.capture.timeperframe.denominator = 20;
        if (-1 == xioctl(fd, VIDIOC_S_PARM, &stream_parm))
        {
            fprintf(stderr, "VIDIOC_S_PARM error\n");
        }
        else
        {
            fprintf(stderr, "stream_parm.capture.timeperframe.denominator=%d\n", stream_parm.parm.capture.timeperframe.denominator);
            fprintf(stderr, "stream_parm.capture.timeperframe.numerator=%d\n", stream_parm.parm.capture.timeperframe.numerator);
        }
    }

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap))
    {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */
        errno = 0;
        if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop))
        {
            switch (errno)
            {
            case EINVAL:
                /* Cropping not supported. */
                break;
            default:
                /* Errors ignored. */
                break;
            }
        }
    }
    else
    {
        /* Errors ignored. */
    }

    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (force_format)
    {
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = mjpeg_frame ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV; //V4L2_PIX_FMT_MJPEG;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;           //V4L2_FIELD_INTERLACED;//V4L2_FIELD_NONE;
        fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB; //V4L2_COLORSPACE_SRGB;  //V4L2_COLORSPACE_JPEG;
                                                       //         fmt.fmt.pix.rot_angle = 0;
                                                       //         fmt.fmt.pix.subchannel = NULL;

        if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
            errno_exit("VIDIOC_S_FMT");

        /* Note VIDIOC_S_FMT may change width and height. */
    }

    {
        /* Preserve original settings as set by v4l2-ctl for example */
        if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
            errno_exit("VIDIOC_G_FMT");
    }
    *cam_width = fmt.fmt.pix.width;
    *cam_height = fmt.fmt.pix.height;
    // Print Stream Format
    fprintf(stderr, "Stream Format Informations:\n");
    fprintf(stderr, " type: %d\n", fmt.type);
    fprintf(stderr, " width: %d\n", fmt.fmt.pix.width);
    fprintf(stderr, " height: %d\n", fmt.fmt.pix.height);
    char fmtstr[8];
    memset(fmtstr, 0, 8);
    memcpy(fmtstr, &fmt.fmt.pix.pixelformat, 4);
    fprintf(stderr, " pixelformat: %08x[%s]\n", fmt.fmt.pix.pixelformat, fmtstr);
    fprintf(stderr, " field: %d\n", fmt.fmt.pix.field);
    fprintf(stderr, " bytesperline: %d\n", fmt.fmt.pix.bytesperline);
    fprintf(stderr, " sizeimage: %d\n", fmt.fmt.pix.sizeimage);
    fprintf(stderr, " colorspace: %d\n", fmt.fmt.pix.colorspace);
    fprintf(stderr, " priv: %d\n", fmt.fmt.pix.priv);
    fprintf(stderr, "====================\n");
    if (list_frame_rate)
        get_device_info();

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
        fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
        fmt.fmt.pix.sizeimage = min;

    switch (io)
    {
    case IO_METHOD_READ:
        init_read(fmt.fmt.pix.sizeimage);
        break;

    case IO_METHOD_MMAP:
        init_mmap();
        break;

    case IO_METHOD_USERPTR:
        init_userp(fmt.fmt.pix.sizeimage);
        break;
    }
}

void close_device(void)
{
    if (-1 == close(fd))
        errno_exit("close");

    fd = -1;
}

void open_device(void)
{
    struct stat st;

    if (-1 == stat(dev_name, &st))
    {
        fprintf(stderr, "Cannot identify '%s': %d, %s\\n",
                dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode))
    {
        fprintf(stderr, "%s is no devicen", dev_name);
        exit(EXIT_FAILURE);
    }

    fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

    if (-1 == fd)
    {
        fprintf(stderr, "Cannot open '%s': %d, %s\\n",
                dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void camera_get_frame_loop(void)
{
    unsigned int count;

    count = frame_count;

    while (1)
    {
        if ((frame_count != -1) && count-- <= 0)
            break;
        if (exit_flag)
            break;
        for (;;)
        {
            fd_set fds;
            struct timeval tv;
            int r;
            //gettimeofday(&tv, NULL);
            //fprintf(stderr, "%f\n", tv.tv_sec+tv.tv_usec/1000000.0);
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r)
            {
                if (EINTR == errno)
                    continue;
                errno_exit("select");
            }

            if (0 == r)
            {
                fprintf(stderr, "select timeout\\n");
                exit(EXIT_FAILURE);
            }

            if (read_frame())
            {
                break;
            }
            /* EAGAIN - continue select loop. */
        }
    }
}

void camera_set_frame_cnt(int cnt){
    frame_count = cnt;
}

void camera_stop(){
    exit_flag = 0;
}

void camera_set_devname(const char *name){
    dev_name = name;
}
void camera_io_method_set(enum io_method t){
    io = t;
}