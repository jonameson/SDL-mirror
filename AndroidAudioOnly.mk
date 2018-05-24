LOCAL_PATH := $(call my-dir)

###########################
#
# SDL shared library
#
###########################

include $(CLEAR_VARS)

LOCAL_MODULE := SDL2

LOCAL_C_INCLUDES := $(LOCAL_PATH)/sdl/include

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES := \
	$(subst $(LOCAL_PATH)/,, \
	$(LOCAL_PATH)/sdl/src/atomic/SDL_atomic.c \
	$(LOCAL_PATH)/sdl/src/atomic/SDL_spinlock.c.arm \
	$(LOCAL_PATH)/sdl/src/core/android/SDL_android_audio_only.c \
	$(wildcard $(LOCAL_PATH)/sdl/src/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/audio/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/audio/android/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/audio/dummy/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/cpuinfo/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/dynapi/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/events/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/file/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/haptic/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/haptic/dummy/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/joystick/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/joystick/dummy/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/loadso/dummy/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/power/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/filesystem/dummy/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/render/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/render/*/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/stdlib/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/thread/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/thread/pthread/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/timer/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/timer/unix/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/video/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/video/yuv2rgb/*.c) \
	$(wildcard $(LOCAL_PATH)/sdl/src/video/dummy/*.c))

# We have added a custom flag in the SDL config for android
# to disable as much stuff as possible so only audio is used.
LOCAL_CFLAGS += -DSDL_AUDIO_ONLY

LOCAL_LDLIBS := -ldl -llog -landroid

include $(BUILD_SHARED_LIBRARY)