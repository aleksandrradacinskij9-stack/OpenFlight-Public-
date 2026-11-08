#include "engine.h"
#include "aircraft.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "OpenFlight", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenFlight", __VA_ARGS__)

Engine& Engine::instance() {
    static Engine e;
    return e;
}

void Engine::setError(int code, const std::string& text) {
    errorCode_.store(code);
    std::lock_guard<std::mutex> lk(errMtx_);
    errorText_ = text;
    LOGE("Ошибка движка %d: %s", code, text.c_str());
}

std::string Engine::errorText() {
    std::lock_guard<std::mutex> lk(errMtx_);
    return errorText_;
}

void Engine::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread(&Engine::threadFunc, this);
    LOGI("Поток рендера запущен");
}

void Engine::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    LOGI("Поток рендера остановлен");
}

void Engine::setSurface(ANativeWindow* win) {
    std::lock_guard<std::mutex> lk(stateMtx_);
    if (pendingWindow_) ANativeWindow_release(pendingWindow_);
    pendingWindow_ = win;
    surfaceAck_ = false;
}

void Engine::clearSurface() {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        pendingClear_ = true;
        surfaceAck_ = false;
    }
    // коротко ждём, чтобы поток успел перестать пользоваться окном
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(stateMtx_);
            if (surfaceAck_) break;
        }
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(400)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Engine::setSurfaceSize(uint32_t w, uint32_t h) {
    // размер учитывается при пересоздании swapchain через capability запросы
    (void)w; (void)h;
}

void Engine::setPaused(bool p) {
    paused_.store(p);
}

void Engine::setControls(const Controls& c) {
    std::lock_guard<std::mutex> lk(stateMtx_);
    controls_ = c;
}

void Engine::addOrbit(float dx, float dy) {
    std::lock_guard<std::mutex> lk(stateMtx_);
    // ограничиваем и сам шаг: защита от редких гигантских скачков ввода
    orbitYaw_   = clampf(orbitYaw_   + clampf(dx, -25.0f, 25.0f), -120.0f, 120.0f);
    orbitPitch_ = clampf(orbitPitch_ + clampf(dy, -20.0f, 20.0f), -35.0f, 50.0f);
    lastOrbitMs_ = nowMs();
}

void Engine::resetOrbit() {
    std::lock_guard<std::mutex> lk(stateMtx_);
    orbitYaw_ = 0.0f;
    orbitPitch_ = 0.0f;
}

void Engine::getTelemetry(float out[12]) {
    std::lock_guard<std::mutex> lk(stateMtx_);
    for (int i = 0; i < 12; i++) out[i] = telemetry_[i];
}

double Engine::nowMs() const {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================
// ПОТОК РЕНДЕРА
// ============================================================
void Engine::threadFunc() {
    Physics phys;
    phys.reset();
    VulkanRenderer renderer;
    Controls ctrl;          // снимок управления
    float gearAnim = 1.0f;  // 1 выпущено, 0 убрано
    bool deviceOk = false;
    double acc = 0.0;
    const double FIXED_DT = 1.0 / 120.0;

    // камера сразу за самолётом на старте
    {
        Vec3 dir = world::runwayDir();
        chaseCam_ = phys.pos - dir * 34.0f + Vec3(0, 12.0f, 0);
    }

    auto lastT = std::chrono::steady_clock::now();

    while (running_.load()) {
        // ---- обмен с UI-потоком ----
        ANativeWindow* newWin = nullptr;
        bool doClear = false;
        {
            std::lock_guard<std::mutex> lk(stateMtx_);
            if (pendingClear_) { doClear = true; pendingClear_ = false; }
            if (pendingWindow_) { newWin = pendingWindow_; pendingWindow_ = nullptr; }
            ctrl = controls_;
        }

        if (doClear) {
            renderer.destroySurface();
            std::lock_guard<std::mutex> lk(stateMtx_);
            surfaceAck_ = true;
        }

        if (newWin) {
            if (!deviceOk) {
                std::string err;
                deviceOk = renderer.initDevice(err);
                if (!deviceOk) { setError(1, err); continue; }
            }
            std::string err;
            uint32_t w = (uint32_t)ANativeWindow_getWidth(newWin);
            uint32_t h = (uint32_t)ANativeWindow_getHeight(newWin);
            if (!renderer.initSurface(newWin, w, h, err)) {
                setError(2, err);
            } else {
                LOGI("Surface готов: %ux%u", w, h);
            }
        }

        if (!renderer.surfaceReady()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            lastT = std::chrono::steady_clock::now();
            continue;
        }
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            lastT = std::chrono::steady_clock::now();
            continue;
        }

        // ---- время и физика ----
        auto now = std::chrono::steady_clock::now();
        double dt = std::min(0.05, std::chrono::duration<double>(now - lastT).count());
        lastT = now;

        acc += dt;
        int steps = 0;
        while (acc >= FIXED_DT && steps < 8) {
            phys.step(FIXED_DT, ctrl);
            acc -= FIXED_DT;
            steps++;
        }

        // анимация уборки/выпуска шасси
        float gearTarget = ctrl.gearDown ? 1.0f : 0.0f;
        float gStep = 0.35f * (float)dt;
        if (gearAnim < gearTarget) gearAnim = std::min(gearTarget, gearAnim + gStep);
        else gearAnim = std::max(gearTarget, gearAnim - gStep);

        // ---- камера и отрисовка ----
        Vec3 camPos;
        Mat4 view;
        float fov = 60.0f, viewPitch = 0.0f;
        updateCamera(dt, phys, ctrl.camMode, camPos, view, fov, viewPitch);

        uint32_t sw = renderer.swapWidth(), sh = renderer.swapHeight();
        float aspect = (sh > 0) ? (float)sw / (float)sh : 1.777f;
        // near 0.6 — крыло/хвост вблизи камеры не исчезают; far 60000 — край сетки земли
        Mat4 proj = Mat4::perspective(fov, aspect, 0.6f, 60000.0f);
        Mat4 viewProj = Mat4::multiply(proj, view);

        // положение истинного горизонта на экране (NDC) — для градиента неба.
        // viewPitch > 0 — смотрим вверх, горизонт уходит вниз экрана.
        float halfTan = tanf(fov * 0.5f * DEG2RAD);
        float horizonNdc = clampf(-tanf(viewPitch * DEG2RAD) / std::max(halfTan, 0.001f),
                                  -2.5f, 2.5f);

        Mat4 pose = Mat4::bodyPose(phys.pos, phys.yaw, phys.pitch, phys.roll);
        Mat4 gearPose = Mat4::multiply(pose, Mat4::scale(1.0f, 0.05f + 0.95f * gearAnim, 1.0f));
        float agl = phys.pos.y - aircraft::GEAR_HEIGHT;
        Mat4 shadowPose = Mat4::multiply(
            Mat4::translation(phys.pos.x, 0.5f, phys.pos.z),
            Mat4::scale(1.0f + agl * 0.05f, 1.0f, 1.0f + agl * 0.04f));

        Mat4 identity;
        std::vector<DrawItem> items;
        items.push_back({&renderer.worldMesh(), identity, 0});
        items.push_back({&renderer.cloudMesh(), identity, 0});
        items.push_back({&renderer.planeMesh(), pose, 0});
        if (gearAnim > 0.03f)
            items.push_back({&renderer.gearMesh(), gearPose, 0});
        if (agl < 40.0f)
            items.push_back({&renderer.shadowMesh(), shadowPose, 1});

        renderer.drawFrame(camPos, viewProj, horizonNdc, items);

        // ---- телеметрия для HUD ----
        Telemetry t = phys.telemetry();
        {
            std::lock_guard<std::mutex> lk(stateMtx_);
            telemetry_[0]  = t.speedKmh;
            telemetry_[1]  = t.altM;
            telemetry_[2]  = t.aglM;
            telemetry_[3]  = t.vsMs;
            telemetry_[4]  = t.hdgDeg;
            telemetry_[5]  = t.thrPct;
            telemetry_[6]  = t.flapDeg;
            telemetry_[7]  = t.onGround ? 1.0f : 0.0f;
            telemetry_[8]  = t.stall ? 1.0f : 0.0f;
            telemetry_[9]  = ctrl.brakes ? 1.0f : 0.0f;
            telemetry_[10] = gearAnim;
            telemetry_[11] = t.hardLanding ? 1.0f : 0.0f;
        }
    }

    renderer.destroyAll();
}

// ============================================================
// КАМЕРА
// ============================================================
void Engine::updateCamera(double dt, const Physics& phys, int camMode,
                          Vec3& camPos, Mat4& view, float& fov, float& pitchOut) {
    Vec3 target = phys.pos;

    if (camMode == 1) {
        // ---- КАБИНА ----
        Mat4 pose = Mat4::bodyPose(phys.pos, phys.yaw, phys.pitch, phys.roll);
        Vec3 fwd (pose.m[0], pose.m[1], pose.m[2]);
        Vec3 up  (pose.m[4], pose.m[5], pose.m[6]);
        camPos = pose.transformPoint(Vec3(19.2f, 1.55f, 0.0f));
        view = Mat4::lookAt(camPos, camPos + fwd, up);
        fov = 75.0f;
        pitchOut = phys.pitch;
        return;
    }
    if (camMode == 2) {
        // ---- ВЫШКА ДМД ----
        Vec3 dir = world::runwayDir(), right = world::runwayRight();
        camPos = dir * 600.0f + right * 760.0f + Vec3(0.0f, 45.0f, 0.0f);
        view = Mat4::lookAt(camPos, target + Vec3(0, 4.0f, 0), Vec3(0, 1, 0));
        // авто-зум: самолёт остаётся различим и вблизи, и на дальнем конце ВПП
        float d = (target - camPos).length();
        fov = clampf(26000.0f / std::max(d, 1.0f), 7.0f, 26.0f);
        // реальный наклон взгляда — горизонт неба совпадает с истинным
        Vec3 toT = target - camPos;
        pitchOut = atan2f(toT.y, sqrtf(toT.x*toT.x + toT.z*toT.z)) * RAD2DEG;
        return;
    }

    // ---- ЧЕЙЗ (camMode 0) ----
    float orbitYaw, orbitPitch;

    // 1) авто-возврат орбиты: через 1.2 с после последнего свайпа плавно
    //    возвращаем камеру за хвост (тау ~1.0 с по рысканию, ~1.6 с по высоте)
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (nowMs() - lastOrbitMs_ > 1200.0) {
            float decayY = expf(-(float)dt / 1.0f);
            float decayP = expf(-(float)dt / 1.6f);
            orbitYaw_   *= decayY;
            orbitPitch_ *= decayP;
            if (fabsf(orbitYaw_) < 0.05f) orbitYaw_ = 0.0f;
            if (fabsf(orbitPitch_) < 0.05f) orbitPitch_ = 0.0f;
        }
        orbitYaw   = orbitYaw_;
        orbitPitch = orbitPitch_;
    }

    float speedKmh = phys.vel.length() * 3.6f;

    // 2) угол обзора: выше орбита — дальше камера, иначе самолёт "занимает" весь экран
    float elev = clampf(14.0f + orbitPitch, 4.0f, 56.0f) * DEG2RAD;
    float dist = 34.0f
               + 30.0f * powf(sinf(elev), 1.5f)      // +0..30 м от высоты
               + std::min(speedKmh * 0.015f, 14.0f); // +0..14 м от скорости
    dist = std::min(dist, 76.0f);

    // 3) желаемая точка позади самолёта (по его курсу + облёт)
    float yawTot = (phys.yaw + orbitYaw) * DEG2RAD;
    Vec3 backDir(sinf(yawTot), 0.0f, -cosf(yawTot));
    Vec3 desired = target - backDir * (dist * cosf(elev)) + Vec3(0, dist * sinf(elev), 0);
    if (desired.y < 2.5f) desired.y = 2.5f;      // страховка от ухода под рельеф

    // 4) быстрое сглаживание позиции (~0.11 с) — камера не отстаёт на горках
    float kPos = 1.0f - expf(-(float)dt * 9.0f);
    chaseCam_ += (desired - chaseCam_) * kPos;
    if (chaseCam_.y < 2.2f) chaseCam_.y = 2.2f;
    camPos = chaseCam_;

    // 5) взгляд: самолёт + упреждение по скорости — резкие манёвры
    //    не "выбрасывают" самолёт за кадр, крыло не проносится сквозь камеру
    Vec3 lead = phys.vel * 0.12f;
    float leadLen = lead.length();
    if (leadLen > 22.0f) lead = lead * (22.0f / leadLen);
    Vec3 lookTarget = target + lead + Vec3(0, 2.5f, 0);
    if (!lookInit_) { lookPt_ = lookTarget; lookInit_ = true; }
    float kLook = 1.0f - expf(-(float)dt * 14.0f);
    lookPt_ += (lookTarget - lookPt_) * kLook;

    view = Mat4::lookAt(camPos, lookPt_, Vec3(0, 1, 0));
    fov = 58.0f + std::min(speedKmh * 0.008f, 8.0f); // 58..66°, лёгкое расширение на скорости
    Vec3 vd = lookPt_ - camPos;
    pitchOut = atan2f(vd.y, sqrtf(vd.x*vd.x + vd.z*vd.z)) * RAD2DEG;
}
