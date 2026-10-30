LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := mcpe
LOCAL_SRC_FILES := prebuilt/libminecraftpe.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := fmod
LOCAL_SRC_FILES := prebuilt/libfmod.so
include $(PREBUILT_SHARED_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE    := Mod
LOCAL_CFLAGS := -w -s -Wno-error=format-security -fvisibility=hidden -fpermissive -fno-exceptions
LOCAL_CPPFLAGS := -w -s -Wno-error=format-security -fvisibility=hidden -Werror -std=c++14
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fpermissive -Wall -fno-exceptions
LOCAL_LDFLAGS += -Wl,--gc-sections,--strip-all,-llog 
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv2 -lGLESv3
LOCAL_SHARED_LIBRARIES := mcpe fmod
LOCAL_STATIC_LIBRARIES  :=  android_native_app_glue
LOCAL_CPP_FEATURES      := rtti exceptions
LOCAL_ARM_MODE := arm
LOCAL_C_INCLUDES := $(LOCAL_PATH) \
$(LOCAL_PATH)/mcpe

LOCAL_SRC_FILES := main.cpp \
    Hook/VtableHook.cpp \
    Substrate/hde64.c \
    Substrate/SubstrateDebug.cpp \
    Substrate/SubstrateHook.cpp \
    Substrate/SubstratePosixMemory.cpp \
	Substrate/SymbolFinder.cpp \
	xhook/xh_core.c \
	xhook/xh_elf.c \
	xhook/xh_jni.c \
	xhook/xh_log.c \
	xhook/xh_util.c \
	xhook/xh_version.c \
	xhook/xhook.c \
    settings/GraphicsOptions.cpp \
    features/Features.cpp \
    features/ErrorReports.cpp \
    features/UiDiagnostics.cpp \
    features/UiDebug.cpp \
    features/MouseInput.cpp \
    features/ForceSprint.cpp \
    features/Hud.cpp \
    features/FpsCounter.cpp \
    features/TargetInfo.cpp \
    features/VSync.cpp \
    features/DynamicLight.cpp \
    features/TitleBranding.cpp \
    settings/GraphicsSettings.cpp \
    engine/BinaryIdentity.cpp \
    engine/Engine.cpp \
    settings/SettingsBridge.cpp \
    mcpe/MinecraftGame.cpp



LOCAL_C_INCLUDES := $(LOCAL_PATH) \
                    $(LOCAL_PATH)/mcpe


include $(BUILD_SHARED_LIBRARY)
$(call import-module,android/native_app_glue)

