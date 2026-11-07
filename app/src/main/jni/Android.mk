LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := mcpe
LOCAL_SRC_FILES := prebuilt/libminecraftpe.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := fmod
LOCAL_SRC_FILES := prebuilt/libfmod.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := mcpe_stub
LOCAL_SRC_FILES := stub.cpp
LOCAL_CPPFLAGS := -std=c++14
LOCAL_SHARED_LIBRARIES := mcpe fmod
LOCAL_LDFLAGS := -Wl,--no-as-needed
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
