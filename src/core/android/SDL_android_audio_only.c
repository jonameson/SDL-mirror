/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"
#include "SDL_stdinc.h"
#include "SDL_assert.h"
#include "SDL_hints.h"
#include "SDL_log.h"
#include "SDL_main.h"

#ifdef __ANDROID__

#include "SDL_system.h"
#include "SDL_android_audio_only.h"

#include "keyinfotable.h"

#include <android/log.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
/* #define LOG_TAG "SDL_android" */
/* #define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__) */
/* #define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__) */
#define LOGI(...) do {} while (0)
#define LOGE(...) do {} while (0)


#define SDL_JAVA_PREFIX                                 org_libsdl_app
#define CONCAT1(prefix, class, function)                CONCAT2(prefix, class, function)
#define CONCAT2(prefix, class, function)                Java_ ## prefix ## _ ## class ## _ ## function
#define SDL_JAVA_INTERFACE(function)                    CONCAT1(SDL_JAVA_PREFIX, SDLActivity, function)
#define SDL_JAVA_AUDIO_INTERFACE(function)              CONCAT1(SDL_JAVA_PREFIX, SDLAudioManager, function)
#define SDL_JAVA_CONTROLLER_INTERFACE(function)         CONCAT1(SDL_JAVA_PREFIX, SDLControllerManager, function)
#define SDL_JAVA_INTERFACE_INPUT_CONNECTION(function)   CONCAT1(SDL_JAVA_PREFIX, SDLInputConnection, function)

/* Java class SDLAudioManager */
JNIEXPORT void JNICALL SDL_JAVA_AUDIO_INTERFACE(nativeSetupJNI)(
        JNIEnv *env, jclass jcls);

/* Uncomment this to log messages entering and exiting methods in this file */
/* #define DEBUG_JNI */

static void Android_JNI_ThreadDestroyed(void*);

/*******************************************************************************
 This file links the Java side of Android with libsdl
*******************************************************************************/
#include <jni.h>


/*******************************************************************************
                               Globals
*******************************************************************************/
static pthread_key_t mThreadKey;
static JavaVM* mJavaVM;

/* audio manager */
static jclass mAudioManagerClass;

/* method signatures */
static jmethodID midAudioOpen;
static jmethodID midAudioWriteShortBuffer;
static jmethodID midAudioWriteByteBuffer;
static jmethodID midAudioClose;
static jmethodID midCaptureOpen;
static jmethodID midCaptureReadShortBuffer;
static jmethodID midCaptureReadByteBuffer;
static jmethodID midCaptureClose;

/*******************************************************************************
                 Functions called by JNI
*******************************************************************************/

/* Library init */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv *env;
    mJavaVM = vm;
    LOGI("JNI_OnLoad called");
    if ((*mJavaVM)->GetEnv(mJavaVM, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
        LOGE("Failed to get the environment using GetEnv()");
        return -1;
    }
    /*
     * Create mThreadKey so we can keep track of the JNIEnv assigned to each thread
     * Refer to http://developer.android.com/guide/practices/design/jni.html for the rationale behind this
     */
    if (pthread_key_create(&mThreadKey, Android_JNI_ThreadDestroyed) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "SDL", "Error initializing pthread key");
    }
    Android_JNI_SetupThread();

    return JNI_VERSION_1_4;
}

void checkJNIReady()
{
    if (!mAudioManagerClass) {
        // We aren't fully initialized, let's just return.
        return;
    }

    SDL_SetMainReady();
}

/* Audio initialization -- called before SDL_main() to initialize JNI bindings */
JNIEXPORT void JNICALL SDL_JAVA_AUDIO_INTERFACE(nativeSetupJNI)(JNIEnv* mEnv, jclass cls)
{
    __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "AUDIO nativeSetupJNI()");

    Android_JNI_SetupThread();

    mAudioManagerClass = (jclass)((*mEnv)->NewGlobalRef(mEnv, cls));

    midAudioOpen = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "audioOpen", "(IZZI)I");
    midAudioWriteShortBuffer = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "audioWriteShortBuffer", "([S)V");
    midAudioWriteByteBuffer = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "audioWriteByteBuffer", "([B)V");
    midAudioClose = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "audioClose", "()V");
    midCaptureOpen = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "captureOpen", "(IZZI)I");
    midCaptureReadShortBuffer = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "captureReadShortBuffer", "([SZ)I");
    midCaptureReadByteBuffer = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "captureReadByteBuffer", "([BZ)I");
    midCaptureClose = (*mEnv)->GetStaticMethodID(mEnv, mAudioManagerClass,
                                "captureClose", "()V");

    if (!midAudioOpen || !midAudioWriteShortBuffer || !midAudioWriteByteBuffer || !midAudioClose ||
       !midCaptureOpen || !midCaptureReadShortBuffer || !midCaptureReadByteBuffer || !midCaptureClose) {
        __android_log_print(ANDROID_LOG_WARN, "SDL", "Missing some Java callbacks, do you have the latest version of SDLAudioManager.java?");
    }

    checkJNIReady();
}

/* SDL main function prototype */
typedef int (*SDL_main_func)(int argc, char *argv[]);

/* Start up the SDL app */
JNIEXPORT int JNICALL SDL_JAVA_INTERFACE(nativeRunMain)(JNIEnv* env, jclass cls, jstring library, jstring function, jobject array)
{
    int status = -1;
    const char *library_file;
    void *library_handle;

    __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "nativeRunMain()");

    library_file = (*env)->GetStringUTFChars(env, library, NULL);
    library_handle = dlopen(library_file, RTLD_GLOBAL);
    if (library_handle) {
        const char *function_name;
        SDL_main_func SDL_main;

        function_name = (*env)->GetStringUTFChars(env, function, NULL);
        SDL_main = (SDL_main_func)dlsym(library_handle, function_name);
        if (SDL_main) {
            int i;
            int argc;
            int len;
            char **argv;

            /* Prepare the arguments. */
            len = (*env)->GetArrayLength(env, array);
            argv = SDL_stack_alloc(char*, 1 + len + 1);
            argc = 0;
            /* Use the name "app_process" so PHYSFS_platformCalcBaseDir() works.
               https://bitbucket.org/MartinFelis/love-android-sdl2/issue/23/release-build-crash-on-start
             */
            argv[argc++] = SDL_strdup("app_process");
            for (i = 0; i < len; ++i) {
                const char* utf;
                char* arg = NULL;
                jstring string = (*env)->GetObjectArrayElement(env, array, i);
                if (string) {
                    utf = (*env)->GetStringUTFChars(env, string, 0);
                    if (utf) {
                        arg = SDL_strdup(utf);
                        (*env)->ReleaseStringUTFChars(env, string, utf);
                    }
                    (*env)->DeleteLocalRef(env, string);
                }
                if (!arg) {
                    arg = SDL_strdup("");
                }
                argv[argc++] = arg;
            }
            argv[argc] = NULL;


            /* Run the application. */
            status = SDL_main(argc, argv);

            /* Release the arguments. */
            for (i = 0; i < argc; ++i) {
                SDL_free(argv[i]);
            }
            SDL_stack_free(argv);

        } else {
            __android_log_print(ANDROID_LOG_ERROR, "SDL", "nativeRunMain(): Couldn't find function %s in library %s", function_name, library_file);
        }
        (*env)->ReleaseStringUTFChars(env, function, function_name);

        dlclose(library_handle);

    } else {
        __android_log_print(ANDROID_LOG_ERROR, "SDL", "nativeRunMain(): Couldn't load library %s", library_file);
    }
    (*env)->ReleaseStringUTFChars(env, library, library_file);

    /* Do not issue an exit or the whole application will terminate instead of just the SDL thread */
    /* exit(status); */

    return status;
}

/* Drop file */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeDropFile)(
                                    JNIEnv* env, jclass jcls,
                                    jstring filename)
{
}

JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeResize)(
                                    JNIEnv* env, jclass jcls,
                                    jint width, jint height, jint format, jfloat rate)
{
}

/* Paddown */
JNIEXPORT jint JNICALL SDL_JAVA_CONTROLLER_INTERFACE(onNativePadDown)(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jint keycode)
{
    return 0;
}

/* Padup */
JNIEXPORT jint JNICALL SDL_JAVA_CONTROLLER_INTERFACE(onNativePadUp)(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jint keycode)
{
    return 0;
}

/* Joy */
JNIEXPORT void JNICALL SDL_JAVA_CONTROLLER_INTERFACE(onNativeJoy)(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jint axis, jfloat value)
{
}

/* POV Hat */
JNIEXPORT void JNICALL SDL_JAVA_CONTROLLER_INTERFACE(onNativeHat)(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jint hat_id, jint x, jint y)
{
}


JNIEXPORT jint JNICALL SDL_JAVA_CONTROLLER_INTERFACE(nativeAddJoystick)(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jstring device_name, jstring device_desc,
                                    jint vendor_id, jint product_id, jboolean is_accelerometer,
                                    jint button_mask, jint naxes, jint nhats, jint nballs)
{
    return 0;
}

JNIEXPORT jint JNICALL SDL_JAVA_CONTROLLER_INTERFACE(nativeRemoveJoystick)(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id)
{
    return 0;
}

JNIEXPORT jint JNICALL SDL_JAVA_CONTROLLER_INTERFACE(nativeAddHaptic)(
    JNIEnv* env, jclass jcls, jint device_id, jstring device_name)
{
    return 0;
}

JNIEXPORT jint JNICALL SDL_JAVA_CONTROLLER_INTERFACE(nativeRemoveHaptic)(
    JNIEnv* env, jclass jcls, jint device_id)
{
    return 0;
}


/* Surface Created */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeSurfaceChanged)(JNIEnv* env, jclass jcls)
{
}

/* Surface Destroyed */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeSurfaceDestroyed)(JNIEnv* env, jclass jcls)
{
}

/* Keydown */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeKeyDown)(
                                    JNIEnv* env, jclass jcls,
                                    jint keycode)
{
}

/* Keyup */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeKeyUp)(
                                    JNIEnv* env, jclass jcls,
                                    jint keycode)
{
}

/* Keyboard Focus Lost */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeKeyboardFocusLost)(
                                    JNIEnv* env, jclass jcls)
{
}


/* Touch */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeTouch)(
                                    JNIEnv* env, jclass jcls,
                                    jint touch_device_id_in, jint pointer_finger_id_in,
                                    jint action, jfloat x, jfloat y, jfloat p)
{
}

/* Mouse */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeMouse)(
                                    JNIEnv* env, jclass jcls,
                                    jint button, jint action, jfloat x, jfloat y)
{
}

/* Accelerometer */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeAccel)(
                                    JNIEnv* env, jclass jcls,
                                    jfloat x, jfloat y, jfloat z)
{
}

/* Clipboard */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeClipboardChanged)(
                                    JNIEnv* env, jclass jcls)
{
}

/* Low memory */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(nativeLowMemory)(
                                    JNIEnv* env, jclass cls)
{
}

/* Quit */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(nativeQuit)(
                                    JNIEnv* env, jclass cls)
{
}

/* Pause */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(nativePause)(
                                    JNIEnv* env, jclass cls)
{
}

/* Resume */
JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(nativeResume)(
                                    JNIEnv* env, jclass cls)
{
}

JNIEXPORT void JNICALL SDL_JAVA_INTERFACE_INPUT_CONNECTION(nativeCommitText)(
                                    JNIEnv* env, jclass cls,
                                    jstring text, jint newCursorPosition)
{
}

JNIEXPORT void JNICALL SDL_JAVA_INTERFACE_INPUT_CONNECTION(nativeGenerateScancodeForUnichar)(
                                    JNIEnv* env, jclass cls,
                                    jchar chUnicode)
{
}


JNIEXPORT void JNICALL SDL_JAVA_INTERFACE_INPUT_CONNECTION(nativeSetComposingText)(
                                    JNIEnv* env, jclass cls,
                                    jstring text, jint newCursorPosition)
{
}

JNIEXPORT jstring JNICALL SDL_JAVA_INTERFACE(nativeGetHint)(
                                    JNIEnv* env, jclass cls,
                                    jstring name)
{
    return NULL;
}

JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(nativeSetenv)(
                                    JNIEnv* env, jclass cls,
                                    jstring name, jstring value)
{

}

/*******************************************************************************
             Functions called by SDL into Java
*******************************************************************************/

static int s_active = 0;
struct LocalReferenceHolder
{
    JNIEnv *m_env;
    const char *m_func;
};

static struct LocalReferenceHolder LocalReferenceHolder_Setup(const char *func)
{
    struct LocalReferenceHolder refholder;
    refholder.m_env = NULL;
    refholder.m_func = func;
#ifdef DEBUG_JNI
    SDL_Log("Entering function %s", func);
#endif
    return refholder;
}

static SDL_bool LocalReferenceHolder_Init(struct LocalReferenceHolder *refholder, JNIEnv *env)
{
    const int capacity = 16;
    if ((*env)->PushLocalFrame(env, capacity) < 0) {
        SDL_SetError("Failed to allocate enough JVM local references");
        return SDL_FALSE;
    }
    ++s_active;
    refholder->m_env = env;
    return SDL_TRUE;
}

static void LocalReferenceHolder_Cleanup(struct LocalReferenceHolder *refholder)
{
#ifdef DEBUG_JNI
    SDL_Log("Leaving function %s", refholder->m_func);
#endif
    if (refholder->m_env) {
        JNIEnv* env = refholder->m_env;
        (*env)->PopLocalFrame(env, NULL);
        --s_active;
    }
}

static SDL_bool LocalReferenceHolder_IsActive(void)
{
    return s_active > 0;
}

ANativeWindow* Android_JNI_GetNativeWindow(void)
{
    return NULL;
}

void Android_JNI_SetActivityTitle(const char *title)
{
}

void Android_JNI_SetWindowStyle(SDL_bool fullscreen)
{
}

void Android_JNI_SetOrientation(int w, int h, int resizable, const char *hint)
{
}

SDL_bool Android_JNI_GetAccelerometerValues(float values[3])
{
    return SDL_FALSE;
}

static void Android_JNI_ThreadDestroyed(void* value)
{
    /* The thread is being destroyed, detach it from the Java VM and set the mThreadKey value to NULL as required */
    JNIEnv *env = (JNIEnv*) value;
    if (env != NULL) {
        (*mJavaVM)->DetachCurrentThread(mJavaVM);
        pthread_setspecific(mThreadKey, NULL);
    }
}

JNIEnv* Android_JNI_GetEnv(void)
{
    /* From http://developer.android.com/guide/practices/jni.html
     * All threads are Linux threads, scheduled by the kernel.
     * They're usually started from managed code (using Thread.start), but they can also be created elsewhere and then
     * attached to the JavaVM. For example, a thread started with pthread_create can be attached with the
     * JNI AttachCurrentThread or AttachCurrentThreadAsDaemon functions. Until a thread is attached, it has no JNIEnv,
     * and cannot make JNI calls.
     * Attaching a natively-created thread causes a java.lang.Thread object to be constructed and added to the "main"
     * ThreadGroup, making it visible to the debugger. Calling AttachCurrentThread on an already-attached thread
     * is a no-op.
     * Note: You can call this function any number of times for the same thread, there's no harm in it
     */

    JNIEnv *env;
    int status = (*mJavaVM)->AttachCurrentThread(mJavaVM, &env, NULL);
    if(status < 0) {
        LOGE("failed to attach current thread");
        return 0;
    }

    /* From http://developer.android.com/guide/practices/jni.html
     * Threads attached through JNI must call DetachCurrentThread before they exit. If coding this directly is awkward,
     * in Android 2.0 (Eclair) and higher you can use pthread_key_create to define a destructor function that will be
     * called before the thread exits, and call DetachCurrentThread from there. (Use that key with pthread_setspecific
     * to store the JNIEnv in thread-local-storage; that way it'll be passed into your destructor as the argument.)
     * Note: The destructor is not called unless the stored value is != NULL
     * Note: You can call this function any number of times for the same thread, there's no harm in it
     *       (except for some lost CPU cycles)
     */
    pthread_setspecific(mThreadKey, (void*) env);

    return env;
}

int Android_JNI_SetupThread(void)
{
    Android_JNI_GetEnv();
    return 1;
}

int Android_JNI_GetDisplayDPI(float *ddpi, float *xdpi, float *ydpi)
{
    return 0;
}

/*
 * Audio support
 */
static jboolean audioBuffer16Bit = JNI_FALSE;
static jobject audioBuffer = NULL;
static void* audioBufferPinned = NULL;
static jboolean captureBuffer16Bit = JNI_FALSE;
static jobject captureBuffer = NULL;

int Android_JNI_OpenAudioDevice(int iscapture, int sampleRate, int is16Bit, int channelCount, int desiredBufferFrames)
{
    jboolean audioBufferStereo;
    int audioBufferFrames;
    jobject jbufobj = NULL;
    jboolean isCopy;

    JNIEnv *env = Android_JNI_GetEnv();

    if (!env) {
        LOGE("callback_handler: failed to attach current thread");
    }
    Android_JNI_SetupThread();

    audioBufferStereo = channelCount > 1;

    if (iscapture) {
        __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "SDL audio: opening device for capture");
        captureBuffer16Bit = is16Bit;
        if ((*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureOpen, sampleRate, audioBuffer16Bit, audioBufferStereo, desiredBufferFrames) != 0) {
            /* Error during audio initialization */
            __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: error on AudioRecord initialization!");
            return 0;
        }
    } else {
        __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "SDL audio: opening device for output");
        audioBuffer16Bit = is16Bit;
        if ((*env)->CallStaticIntMethod(env, mAudioManagerClass, midAudioOpen, sampleRate, audioBuffer16Bit, audioBufferStereo, desiredBufferFrames) != 0) {
            /* Error during audio initialization */
            __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: error on AudioTrack initialization!");
            return 0;
        }
    }

    /* Allocating the audio buffer from the Java side and passing it as the return value for audioInit no longer works on
     * Android >= 4.2 due to a "stale global reference" error. So now we allocate this buffer directly from this side. */

    if (is16Bit) {
        jshortArray audioBufferLocal = (*env)->NewShortArray(env, desiredBufferFrames * (audioBufferStereo ? 2 : 1));
        if (audioBufferLocal) {
            jbufobj = (*env)->NewGlobalRef(env, audioBufferLocal);
            (*env)->DeleteLocalRef(env, audioBufferLocal);
        }
    }
    else {
        jbyteArray audioBufferLocal = (*env)->NewByteArray(env, desiredBufferFrames * (audioBufferStereo ? 2 : 1));
        if (audioBufferLocal) {
            jbufobj = (*env)->NewGlobalRef(env, audioBufferLocal);
            (*env)->DeleteLocalRef(env, audioBufferLocal);
        }
    }

    if (jbufobj == NULL) {
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: could not allocate an audio buffer!");
        return 0;
    }

    if (iscapture) {
        captureBuffer = jbufobj;
    } else {
        audioBuffer = jbufobj;
    }

    isCopy = JNI_FALSE;

    if (is16Bit) {
        if (!iscapture) {
            audioBufferPinned = (*env)->GetShortArrayElements(env, (jshortArray)audioBuffer, &isCopy);
        }
        audioBufferFrames = (*env)->GetArrayLength(env, (jshortArray)audioBuffer);
    } else {
        if (!iscapture) {
            audioBufferPinned = (*env)->GetByteArrayElements(env, (jbyteArray)audioBuffer, &isCopy);
        }
        audioBufferFrames = (*env)->GetArrayLength(env, (jbyteArray)audioBuffer);
    }

    if (audioBufferStereo) {
        audioBufferFrames /= 2;
    }

    return audioBufferFrames;
}

void * Android_JNI_GetAudioBuffer(void)
{
    return audioBufferPinned;
}

void Android_JNI_WriteAudioBuffer(void)
{
    JNIEnv *mAudioEnv = Android_JNI_GetEnv();

    if (audioBuffer16Bit) {
        (*mAudioEnv)->ReleaseShortArrayElements(mAudioEnv, (jshortArray)audioBuffer, (jshort *)audioBufferPinned, JNI_COMMIT);
        (*mAudioEnv)->CallStaticVoidMethod(mAudioEnv, mAudioManagerClass, midAudioWriteShortBuffer, (jshortArray)audioBuffer);
    } else {
        (*mAudioEnv)->ReleaseByteArrayElements(mAudioEnv, (jbyteArray)audioBuffer, (jbyte *)audioBufferPinned, JNI_COMMIT);
        (*mAudioEnv)->CallStaticVoidMethod(mAudioEnv, mAudioManagerClass, midAudioWriteByteBuffer, (jbyteArray)audioBuffer);
    }

    /* JNI_COMMIT means the changes are committed to the VM but the buffer remains pinned */
}

int Android_JNI_CaptureAudioBuffer(void *buffer, int buflen)
{
    JNIEnv *env = Android_JNI_GetEnv();
    jboolean isCopy = JNI_FALSE;
    jint br;

    if (captureBuffer16Bit) {
        SDL_assert((*env)->GetArrayLength(env, (jshortArray)captureBuffer) == (buflen / 2));
        br = (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadShortBuffer, (jshortArray)captureBuffer, JNI_TRUE);
        if (br > 0) {
            jshort *ptr = (*env)->GetShortArrayElements(env, (jshortArray)captureBuffer, &isCopy);
            br *= 2;
            SDL_memcpy(buffer, ptr, br);
            (*env)->ReleaseShortArrayElements(env, (jshortArray)captureBuffer, (jshort *)ptr, JNI_ABORT);
        }
    } else {
        SDL_assert((*env)->GetArrayLength(env, (jshortArray)captureBuffer) == buflen);
        br = (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadByteBuffer, (jbyteArray)captureBuffer, JNI_TRUE);
        if (br > 0) {
            jbyte *ptr = (*env)->GetByteArrayElements(env, (jbyteArray)captureBuffer, &isCopy);
            SDL_memcpy(buffer, ptr, br);
            (*env)->ReleaseByteArrayElements(env, (jbyteArray)captureBuffer, (jbyte *)ptr, JNI_ABORT);
        }
    }

    return (int) br;
}

void Android_JNI_FlushCapturedAudio(void)
{
    JNIEnv *env = Android_JNI_GetEnv();
#if 0  /* !!! FIXME: this needs API 23, or it'll do blocking reads and never end. */
    if (captureBuffer16Bit) {
        const jint len = (*env)->GetArrayLength(env, (jshortArray)captureBuffer);
        while ((*env)->CallStaticIntMethod(env, mActivityClass, midCaptureReadShortBuffer, (jshortArray)captureBuffer, JNI_FALSE) == len) { /* spin */ }
    } else {
        const jint len = (*env)->GetArrayLength(env, (jbyteArray)captureBuffer);
        while ((*env)->CallStaticIntMethod(env, mActivityClass, midCaptureReadByteBuffer, (jbyteArray)captureBuffer, JNI_FALSE) == len) { /* spin */ }
    }
#else
    if (captureBuffer16Bit) {
        (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadShortBuffer, (jshortArray)captureBuffer, JNI_FALSE);
    } else {
        (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadByteBuffer, (jbyteArray)captureBuffer, JNI_FALSE);
    }
#endif
}

void Android_JNI_CloseAudioDevice(const int iscapture)
{
    JNIEnv *env = Android_JNI_GetEnv();

    if (iscapture) {
        (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midCaptureClose);
        if (captureBuffer) {
            (*env)->DeleteGlobalRef(env, captureBuffer);
            captureBuffer = NULL;
        }
    } else {
        (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midAudioClose);
        if (audioBuffer) {
            (*env)->DeleteGlobalRef(env, audioBuffer);
            audioBuffer = NULL;
            audioBufferPinned = NULL;
        }
    }
}

/* Test for an exception and call SDL_SetError with its detail if one occurs */
/* If the parameter silent is truthy then SDL_SetError() will not be called. */
static SDL_bool Android_JNI_ExceptionOccurred(SDL_bool silent)
{
    JNIEnv *mEnv = Android_JNI_GetEnv();
    jthrowable exception;

    SDL_assert(LocalReferenceHolder_IsActive());

    exception = (*mEnv)->ExceptionOccurred(mEnv);
    if (exception != NULL) {
        jmethodID mid;

        /* Until this happens most JNI operations have undefined behaviour */
        (*mEnv)->ExceptionClear(mEnv);

        if (!silent) {
            jclass exceptionClass = (*mEnv)->GetObjectClass(mEnv, exception);
            jclass classClass = (*mEnv)->FindClass(mEnv, "java/lang/Class");
            jstring exceptionName;
            const char* exceptionNameUTF8;
            jstring exceptionMessage;

            mid = (*mEnv)->GetMethodID(mEnv, classClass, "getName", "()Ljava/lang/String;");
            exceptionName = (jstring)(*mEnv)->CallObjectMethod(mEnv, exceptionClass, mid);
            exceptionNameUTF8 = (*mEnv)->GetStringUTFChars(mEnv, exceptionName, 0);

            mid = (*mEnv)->GetMethodID(mEnv, exceptionClass, "getMessage", "()Ljava/lang/String;");
            exceptionMessage = (jstring)(*mEnv)->CallObjectMethod(mEnv, exception, mid);

            if (exceptionMessage != NULL) {
                const char* exceptionMessageUTF8 = (*mEnv)->GetStringUTFChars(mEnv, exceptionMessage, 0);
                SDL_SetError("%s: %s", exceptionNameUTF8, exceptionMessageUTF8);
                (*mEnv)->ReleaseStringUTFChars(mEnv, exceptionMessage, exceptionMessageUTF8);
            } else {
                SDL_SetError("%s", exceptionNameUTF8);
            }

            (*mEnv)->ReleaseStringUTFChars(mEnv, exceptionName, exceptionNameUTF8);
        }

        return SDL_TRUE;
    }

    return SDL_FALSE;
}

static int Internal_Android_JNI_FileOpen(SDL_RWops* ctx)
{
    return 0;
}

int Android_JNI_FileOpen(SDL_RWops* ctx,
        const char* fileName, const char* mode)
{
    return 0;
}

size_t Android_JNI_FileRead(SDL_RWops* ctx, void* buffer,
        size_t size, size_t maxnum)
{
    return 0;
}

size_t Android_JNI_FileWrite(SDL_RWops* ctx, const void* buffer,
        size_t size, size_t num)
{
    return 0;
}

static int Internal_Android_JNI_FileClose(SDL_RWops* ctx, SDL_bool release)
{
    return 0;
}


Sint64 Android_JNI_FileSize(SDL_RWops* ctx)
{
    return 0;
}

Sint64 Android_JNI_FileSeek(SDL_RWops* ctx, Sint64 offset, int whence)
{
    return 0;

}

int Android_JNI_FileClose(SDL_RWops* ctx)
{
    return SDL_FALSE;
}

int Android_JNI_SetClipboardText(const char* text)
{
    return 0;
}

char* Android_JNI_GetClipboardText(void)
{
    return NULL;
}

SDL_bool Android_JNI_HasClipboardText(void)
{
    return SDL_FALSE;
}

/* returns 0 on success or -1 on error (others undefined then)
 * returns truthy or falsy value in plugged, charged and battery
 * returns the value in seconds and percent or -1 if not available
 */
int Android_JNI_GetPowerInfo(int* plugged, int* charged, int* battery, int* seconds, int* percent)
{
    return 0;
}

/* returns number of found touch devices as return value and ids in parameter ids */
int Android_JNI_GetTouchDeviceIds(int **ids)
{
    return 0;
}

/* sets the mSeparateMouseAndTouch field */
void Android_JNI_SetSeparateMouseAndTouch(SDL_bool new_value)
{
}

void Android_JNI_PollInputDevices(void)
{
}

void Android_JNI_PollHapticDevices(void)
{
}

void Android_JNI_HapticRun(int device_id, int length)
{
}


/* See SDLActivity.java for constants. */
#define COMMAND_SET_KEEP_SCREEN_ON    5

/* sends message to be handled on the UI event dispatch thread */
int Android_JNI_SendMessage(int command, int param)
{
    return 0;
}

void Android_JNI_SuspendScreenSaver(SDL_bool suspend)
{
}

void Android_JNI_ShowTextInput(SDL_Rect *inputRect)
{
}

void Android_JNI_HideTextInput(void)
{
}

SDL_bool Android_JNI_IsScreenKeyboardShown()
{
    return SDL_FALSE;
}


int Android_JNI_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid)
{
    return 0;
}

///*
////////////////////////////////////////////////////////////////////////////////
////
//// Functions exposed to SDL applications in SDL_system.h
////////////////////////////////////////////////////////////////////////////////
//*/
//
void *SDL_AndroidGetJNIEnv(void)
{
    return Android_JNI_GetEnv();
}

void *SDL_AndroidGetActivity(void)
{
    return NULL;
}

SDL_bool SDL_IsAndroidTV(void)
{
    return SDL_FALSE;
}

const char * SDL_AndroidGetInternalStoragePath(void)
{
    return NULL;
}

int SDL_AndroidGetExternalStorageState(void)
{
    return 0;
}

const char * SDL_AndroidGetExternalStoragePath(void)
{
    return NULL;
}

void Android_JNI_GetManifestEnvironmentVariables(void)
{
}

int Android_JNI_CreateCustomCursor(SDL_Surface *surface, int hot_x, int hot_y)
{
    return 0;
}


SDL_bool Android_JNI_SetCustomCursor(int cursorID)
{
    return SDL_FALSE;
}

SDL_bool Android_JNI_SetSystemCursor(int cursorID)
{
    return SDL_FALSE;
}

#endif /* __ANDROID__ */

/* vi: set ts=4 sw=4 expandtab: */
