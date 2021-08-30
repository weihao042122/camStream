#
# 1. Set the path and clear environment
# 	TARGET_PATH := $(call my-dir)
# 	include $(ENV_CLEAR)
#
# 2. Set the source files and headers files
#	TARGET_SRC := xxx_1.c xxx_2.c
#	TARGET_INc := xxx_1.h xxx_2.h
#
# 3. Set the output target
#	TARGET_MODULE := xxx
#
# 4. Include the main makefile
#	include $(BUILD_BIN)
#
# Before include the build makefile, you can set the compilaion
# flags, e.g. TARGET_ASFLAGS TARGET_CFLAGS TARGET_CPPFLAGS
#

LOCAL_PATH:= $(call my-dir)

#libstlport_cflags := -D_GNU_SOURCE
#libstlport_cppflags := -fuse-cxa-atexit
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH) $(CAMLINUX_BUILD_TOP)/frameworks/av/media/CedarX-Projects/LIBRARY/CODEC/VIDEO/ENCODER/include/ $(CAMLINUX_BUILD_TOP)/external/jpeg/

LOCAL_SRC_FILES :=  camStream.c yuyv2rgb.c yuyv2bmpgray.c h264_codec.c camera.c AVC.c Network.c RTPEnc.c Utils.c
LOCAL_CFLAGS += -D_GNU_SOURCE --std=c99

LOCAL_MODULE_TAGS :=  optional

LOCAL_MODULE := camStream
LOCAL_SHARED_LIBRARIES := libvencoder libcutils libjpeg
include $(BUILD_EXECUTABLE)

