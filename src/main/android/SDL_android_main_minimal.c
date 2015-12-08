/*
    SDL_android_main.c, placed in the public domain by Sam Lantinga  3/13/14
*/
#include "../../SDL_internal.h"

#ifdef __ANDROID__

/* Include the SDL main definition header */
#include "SDL_main.h"

/*******************************************************************************
                 Functions called by JNI
*******************************************************************************/
#include <jni.h>

/* Called before SDL_main() to initialize JNI bindings in SDL library */
extern void SDL_Android_Init(JNIEnv* env, jclass cls);

/**
 * For using the SDL library for audio only. No need for the whole
 */
JNIEXPORT int JNICALL Java_org_libsdl_SDL_nativeInit(JNIEnv* env, jclass cls)
{
    /* This interface could expand with ABI negotiation, callbacks, etc. */
    SDL_Android_Init(env, cls);
    
    SDL_SetMainReady();
    
    return 0;
}

#endif /* __ANDROID__ */

/* vi: set ts=4 sw=4 expandtab: */
