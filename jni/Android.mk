LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := wgpu_native
LOCAL_SRC_FILES := wgpu/lib/libwgpu_native.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE           := main
LOCAL_SRC_FILES        := main.c
LOCAL_C_INCLUDES       := $(LOCAL_PATH)/wgpu/include
LOCAL_LDLIBS           := -llog -landroid
LOCAL_STATIC_LIBRARIES := wgpu_native
include $(BUILD_SHARED_LIBRARY)