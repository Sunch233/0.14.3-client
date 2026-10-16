#include "GraphicsSettings.h"
#include <jni.h>
extern "C" JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_MainActivity_nativeConfigureGraphics(
        JNIEnv* env,jclass,jstring path) {
    if(!path) return;
    const char* value=env->GetStringUTFChars(path,nullptr);
    if(!value) return;
    graphicssettings::Configure(value);
    env->ReleaseStringUTFChars(path,value);
}
