LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := src/libbtctl.c ../btctl/util.c 
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../btctl $(LOCAL_PATH)/include
LOCAL_SHARED_LIBRARIES := libhardware liblog
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := libbtctl

#include $(BUILD_EXECUTABLE)
include $(BUILD_SHARED_LIBRARY)
