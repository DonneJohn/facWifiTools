#ifneq ($(BUILD_TINY_ANDROID),true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= wificonnect.c
LOCAL_MODULE:= facwifitools

# modified by zhangcc, for wifi connect
LOCAL_SHARED_LIBRARIES := \
    libc \
    libcutils \
    liblog \
    libhardware_legacy \
    libnetutils

include $(BUILD_EXECUTABLE)
#endif
