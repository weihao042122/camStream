#ifndef _DEBUG_H_
#define _DEBUG_H_
#include <cutils/log.h>
#undef LOG_TAG
#define LOG_TAG "codecTest"

#define logd(fmt, ...) ALOGD(fmt, ##__VA_ARGS__)
#define loge(fmt, ...) ALOGE(fmt, ##__VA_ARGS__)

#define errno_exit(s) do{   \
    fprintf(stderr, "%d:%s error %d, %s\\n",__LINE__, s, errno, strerror(errno));\
    exit(EXIT_FAILURE);\
}while(0)

#endif /*_DEBUG_H_*/