#pragma once
// ============================================================
// OpenFlight — движок: поток рендера, физика, камера, ввод.
// Один поток владеет Vulkan; UI-поток общается через мьютексы.
// ============================================================

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <android/native_window.h>
#include "physics.h"
#include "vulkan_renderer.h"

class Engine {
public:
    static Engine& instance();

    void start();                       // запустить поток
    void stop();                        // полностью остановить
    void setSurface(ANativeWindow* win);
    void clearSurface();                //surface ушёл — ждём подтверждения потока
    void setSurfaceSize(uint32_t w, uint32_t h);
    void setPaused(bool p);

    void setControls(const Controls& c);
    void addOrbit(float dx, float dy);   // дельта свайпа, градусы
    void resetOrbit();                   // мгновенный сброс (двойной тап)
    void getTelemetry(float out[12]);

    int  errorCode() const { return errorCode_.load(); }
    std::string errorText();

private:
    Engine() = default;
    void threadFunc();
    void setError(int code, const std::string& text);
    void updateCamera(double dt, const Physics& phys, int camMode,
                      Vec3& camPos, Mat4& view, float& fov, float& pitchOut);
    double nowMs() const;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int>  errorCode_{0};
    std::string errorText_;
    std::mutex errMtx_;

    std::mutex stateMtx_;
    ANativeWindow* pendingWindow_ = nullptr;
    bool pendingClear_ = false;
    bool surfaceAck_ = false;
    Controls controls_;
    float orbitYaw_ = 0.0f;    // облёт, градусы (относительно курса самолёта)
    float orbitPitch_ = 0.0f;
    double lastOrbitMs_ = 0.0; // момент последнего свайпа — для авто-возврата
    float telemetry_[12] = {};

    // состояние камеры (только поток рендера)
    Vec3 chaseCam_{0.0f, 20.0f, -60.0f};
    Vec3 lookPt_{0.0f, 0.0f, 0.0f};
    bool lookInit_ = false;
};
