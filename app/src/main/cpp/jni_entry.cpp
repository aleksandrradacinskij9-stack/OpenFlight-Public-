// ============================================================
// OpenFlight — JNI-мост между Java UI и C++ движком
// ============================================================
#include <jni.h>
#include <android/native_window_jni.h>
#include "engine.h"

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_openflight_sim_MainActivity_nativeStart(JNIEnv*, jobject) {
    Engine::instance().start();
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_openflight_sim_MainActivity_nativeIsError(JNIEnv*, jobject) {
    return Engine::instance().errorCode() != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_openflight_sim_MainActivity_nativeGetError(JNIEnv* env, jobject) {
    std::string s = Engine::instance().errorText();
    if (s.empty()) s = "Неизвестная ошибка";
    return env->NewStringUTF(s.c_str());
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeOnSurfaceCreated(JNIEnv* env, jobject, jobject surface) {
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    Engine::instance().setSurface(win);
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeOnSurfaceChanged(JNIEnv*, jobject, jint w, jint h) {
    Engine::instance().setSurfaceSize((uint32_t)w, (uint32_t)h);
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeOnSurfaceDestroyed(JNIEnv*, jobject) {
    Engine::instance().clearSurface();
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeOnResume(JNIEnv*, jobject) {
    Engine::instance().setPaused(false);
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeOnPause(JNIEnv*, jobject) {
    Engine::instance().setPaused(true);
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeOnDestroy(JNIEnv*, jobject) {
    Engine::instance().stop();
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeSetControls(
        JNIEnv*, jobject,
        jfloat throttle, jfloat pitchCmd, jfloat rollCmd, jfloat rudderCmd,
        jboolean brakes, jboolean gearDown, jint flaps, jint camMode) {
    Controls c;
    c.throttle   = throttle;
    c.pitchCmd   = pitchCmd;
    c.rollCmd    = rollCmd;
    c.rudderCmd  = rudderCmd;
    c.brakes     = (brakes == JNI_TRUE);
    c.gearDown   = (gearDown == JNI_TRUE);
    c.flaps      = flaps;
    c.camMode    = camMode;
    Engine::instance().setControls(c);
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeAddOrbit(JNIEnv*, jobject, jfloat dx, jfloat dy) {
    Engine::instance().addOrbit(dx, dy);
}

JNIEXPORT void JNICALL
Java_com_openflight_sim_MainActivity_nativeResetOrbit(JNIEnv*, jobject) {
    Engine::instance().resetOrbit();
}

JNIEXPORT jfloatArray JNICALL
Java_com_openflight_sim_MainActivity_nativeGetTelemetry(JNIEnv* env, jobject) {
    float t[12];
    Engine::instance().getTelemetry(t);
    jfloatArray arr = env->NewFloatArray(12);
    if (arr) env->SetFloatArrayRegion(arr, 0, 12, t);
    return arr;
}

} // extern "C"
