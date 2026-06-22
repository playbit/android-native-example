LOCAL_PATH := $(call my-dir)
APP_GLUE_DIR := $(NDK_ROOT)/sources/android/native_app_glue

include $(CLEAR_VARS)
LOCAL_MODULE    := wgpu_native
LOCAL_SRC_FILES := wgpu/lib/libwgpu_native.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE           := main
LOCAL_SRC_FILES        := main.c $(APP_GLUE_DIR)/android_native_app_glue.c
LOCAL_C_INCLUDES       := $(LOCAL_PATH)/wgpu/include $(APP_GLUE_DIR)
LOCAL_LDLIBS           := -llog -landroid
LOCAL_STATIC_LIBRARIES := wgpu_native
include $(BUILD_SHARED_LIBRARY)