#ifndef _CAMERA_H_
#define _CAMERA_H_
#include <stdlib.h>
enum io_method
{
    IO_METHOD_READ,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
};

struct buffer
{
    void *start;
    size_t length;
};

typedef void (*CAMERA_DATA_CB)(void const *p, int size);
void open_device(void);
void init_device(int *cam_width, int *cam_height, int force_format, int mjpeg_frame, int list_frame_rate, CAMERA_DATA_CB cb);
void uninit_device(void);
void start_capturing(void);
void stop_capturing(void);
void close_device(void);

void camera_get_frame_loop(void);
void camera_set_frame_cnt(int cnt);
void camera_stop(void);
void camera_set_devname(const char *name);
void camera_io_method_set(enum io_method io);
#endif /*_CAMERA_H_*/