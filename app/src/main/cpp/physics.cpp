#include "physics.h"
#include "aircraft.h"
#include <cmath>
#include <algorithm>

void Physics::reset() {
    // старт: порог ВПП 14 (северо-западный торец), по оси, двигатели на малом
    Vec3 dir = world::runwayDir();
    pos = dir * -1850.0f + Vec3(0.0f, aircraft::GEAR_HEIGHT, 0.0f);
    vel = Vec3(0, 0, 0);
    yaw = world::RUNWAY_HEADING;
    pitch = 0.0f;
    roll = 0.0f;
    pRate = qRate = rRate = 0.0f;
    engineFrac = 0.15f; // двигатели запущены, малый газ
    flapCur = 0.0f;
    onGround = true;
    stall = false;
    hardLanding = false;
    overspeed = false;
}

float Physics::airDensity(float h) const {
    return 1.225f * expf(-std::max(h, 0.0f) / 8500.0f);
}

void Physics::step(double dtIn, const Controls& c) {
    float dt = (float)std::min(dtIn, 0.1);

    // ---- плавное движение закрылков ----
    float flapTarget = FLAP_DEG[std::clamp(c.flaps, 0, 3)];
    float flapRate = 5.0f;
    if (flapCur < flapTarget) flapCur = std::min(flapTarget, flapCur + flapRate * dt);
    else if (flapCur > flapTarget) flapCur = std::max(flapTarget, flapCur - flapRate * dt);

    // ---- двигатель: приёмистость Д-30 ----
    float thr = std::clamp(c.throttle, 0.0f, 1.0f);
    float tau = (thr > engineFrac) ? 3.5f : 2.2f;
    engineFrac += (thr - engineFrac) * (1.0f - expf(-dt / tau));
    if (engineFrac < 0.02f) engineFrac = 0.02f;

    // ---- ориентация самолёта ----
    float th = yaw * DEG2RAD, ph = pitch * DEG2RAD, rl = roll * DEG2RAD;
    float sy = sinf(th), cy = cosf(th), sp = sinf(ph), cp = cosf(ph), sr = sinf(rl), cr = cosf(rl);
    Vec3 fwd(sy * cp, sp, -cy * cp);
    Vec3 rightH(cy, 0.0f, sy);
    Vec3 up0(-sy * sp, cp, cy * sp);
    Vec3 right = rightH * cr - up0 * sr;
    Vec3 up    = rightH * sr + up0 * cr;

    float speed = vel.length();
    Vec3 velDir = speed > 0.5f ? vel * (1.0f / speed) : fwd;

    // угол атаки и скольжения
    float vFwd = vel.dot(fwd);
    float vUp  = vel.dot(up);
    float vRight = vel.dot(right);
    float alpha = atan2f(-vUp, std::max(vFwd, 1.0f));
    float beta  = atan2f(vRight, std::max(vFwd, 1.0f));

    float rho = airDensity(pos.y);
    float q = 0.5f * rho * speed * speed; // скоростной напор

    // ---- коэффициенты аэродинамики ----
    float flapNorm = flapCur / 45.0f;
    float cl = CL0 + CL_ALPHA * alpha + 0.9f * flapNorm;
    stall = false;
    if (alpha > ALPHA_CRIT) {
        // срыв потока: подъёмная сила падает, нос "тонет"
        cl = (CL0 + CL_ALPHA * ALPHA_CRIT + 0.9f * flapNorm) - (alpha - ALPHA_CRIT) * 3.5f;
        cl = std::max(cl, -0.2f);
        stall = speed > 30.0f;
    }
    if (alpha < -0.30f) cl = CL0 + CL_ALPHA * alpha; // отрицательные углы

    float cd = CD0 + K_INDUCED * cl * cl
             + (c.gearDown ? 0.018f : 0.0f)
             + 0.050f * powf(flapNorm, 1.5f);

    // ---- силы ----
    float mass = MASS;
    float grav = 9.81f;
    Vec3 force(0, -grav * mass, 0); // гравитация

    // тяга (уменьшается с высотой)
    float thrustTotal = (IDLE_THRUST + engineFrac * (MAX_THRUST - IDLE_THRUST))
                      * (float)ENGINES * powf(rho / 1.225f, 0.85f);
    force += fwd * thrustTotal;

    if (speed > 0.5f) {
        Vec3 liftDir = right.cross(velDir).normalized(); // перпендикулярно скорости
        float lift = q * WING_AREA * cl;
        force += liftDir * lift;
        force += velDir * (-q * WING_AREA * cd);         // сопротивление
        // боковая сила от скольжения (флюгерный эффект)
        force += right * (-beta * q * WING_AREA * 0.35f);
    }

    // ---- вращение ----
    float qFactor = std::clamp(speed / 100.0f, 0.12f, 1.35f); // эффективность рулей от скорости
    float pCmd = c.rollCmd * 45.0f * qFactor;
    float qCmd = c.pitchCmd * 18.0f * qFactor;
    float rCmd = c.rudderCmd * 12.0f * qFactor;

    // продольная устойчивость: стремится к балансировочному углу атаки
    qCmd -= (alpha - 0.05f) * RAD2DEG * 1.6f * qFactor;
    // при срыве — опускание носа
    if (stall) qCmd -= (alpha - ALPHA_CRIT) * RAD2DEG * 6.0f;
    // флюгерные моменты — мягче (на видео из-за них разворачивало на 98°/с)
    pCmd -= beta * RAD2DEG * 0.8f * qFactor;
    rCmd -= beta * RAD2DEG * 1.6f * qFactor;

    // сглаживание (инерция самолёта)
    float tauP = 0.30f, tauQ = 0.45f, tauR = 0.35f;
    pRate += (pCmd - pRate) * (1.0f - expf(-dt / tauP));
    qRate += (qCmd - qRate) * (1.0f - expf(-dt / tauQ));
    rRate += (rCmd - rRate) * (1.0f - expf(-dt / tauR));
    // Ту-154 — не пилотажный самолёт: жёсткие пределы угловых скоростей
    // (на видео 0.05 был "плоский штопор" 98°/с по рысканию)
    pRate = std::clamp(pRate, -35.0f, 35.0f);
    qRate = std::clamp(qRate, -18.0f, 18.0f);
    rRate = std::clamp(rRate, -22.0f, 22.0f);

    if (onGround) {
        // ---- ЗЕМЛЯ ----
        // руление носовой стойкой (джойстик влево/вправо + педали)
        float steerCmd = std::clamp(c.rollCmd + c.rudderCmd, -1.0f, 1.0f);
        float steerGain = 40.0f * (speed / (speed + 6.0f))
                        * (1.0f - std::clamp((speed - 65.0f) / 25.0f, 0.0f, 1.0f));
        rRate = steerCmd * steerGain;

        // разбег: подъём носа только на достаточной скорости
        if (speed > 55.0f && c.pitchCmd > 0.0f) {
            pitch += c.pitchCmd * 6.0f * dt;
            pitch = std::min(pitch, 12.0f);
        } else {
            pitch -= std::min(pitch, 4.0f * dt); // опускание на стойки
            if (pitch < 0) pitch = 0;
        }
        roll -= std::clamp(roll, -6.0f * dt, 6.0f * dt);
        qRate = 0; pRate = 0;

        // трение: качение + тормоза + трава
        Vec3 hvel(vel.x, 0, vel.z);
        float hs = hvel.length();
        float mu = ROLL_MU + (c.brakes ? BRAKE_MU : 0.0f)
                 + (world::isOnPavement(pos) ? 0.0f : GRASS_MU);
        if (hs > 0.05f) {
            float dec = mu * grav * dt;
            float ns = std::max(0.0f, hs - dec);
            hvel = hvel * (ns / hs);
            vel.x = hvel.x; vel.z = hvel.z;
        } else if (c.brakes || thrustTotal < MASS * grav * ROLL_MU) {
            vel.x = 0; vel.z = 0;
        }
        // боковое сцепление колёс (гасит скольжение вбок)
        float vSideGround = vel.dot(rightH);
        vel -= rightH * vSideGround * std::min(1.0f, 8.0f * dt);
    } else {
        // ---- ВОЗДУХ ----
        yaw   = wrap360(yaw + rRate * dt);
        pitch += qRate * dt;
        pitch = std::clamp(pitch, -45.0f, 45.0f);   // было ±72° — "кобра" на видео
        roll  += pRate * dt;
        roll  = std::clamp(roll, -75.0f, 75.0f);    // было ±130°
        // плавное выравнивание по крену немного помогает новичку
        roll -= roll * 0.15f * dt;
    }

    // ---- интегрирование перемещения ----
    vel += force * (1.0f / mass) * dt;
    pos += vel * dt;

    // ---- контакт с землёй ----
    if (onGround) {
        if (pos.y < aircraft::GEAR_HEIGHT + 0.001f && c.gearDown) {
            pos.y = aircraft::GEAR_HEIGHT;
            if (vel.y < -6.5f) hardLanding = true;
            if (vel.y < 0) vel.y = (vel.y < -3.0f) ? -vel.y * 0.12f : 0.0f;
            // отрыв, когда подъёмная сила превысит вес (уже учтено в vel.y>0)
            if (vel.y > 0.3f && pitch > 1.0f) { onGround = false; hardLanding = false; }
        } else if (!c.gearDown) {
            // посадка с убранным шасси — скользим на "брюхе"
            pos.y = aircraft::GEAR_HEIGHT * 0.55f;
            if (vel.y < 0) vel.y = 0;
            Vec3 hvel(vel.x, 0, vel.z);
            float hs = hvel.length();
            if (hs > 0.05f) {
                float ns = std::max(0.0f, hs - 0.5f * grav * dt);
                hvel = hvel * (ns / hs);
                vel.x = hvel.x; vel.z = hvel.z;
            }
        }
    } else {
        if (c.gearDown && pos.y <= aircraft::GEAR_HEIGHT) {
            pos.y = aircraft::GEAR_HEIGHT;
            if (vel.y < -6.5f) hardLanding = true;
            onGround = true;
            vel.y = (vel.y < -3.0f) ? -vel.y * 0.12f : 0.0f;
        } else if (!c.gearDown && pos.y <= aircraft::GEAR_HEIGHT * 0.55f) {
            pos.y = aircraft::GEAR_HEIGHT * 0.55f;
            onGround = true;
            if (vel.y < 0) vel.y = 0;
        }
    }

    overspeed = speed > 260.0f; // ~940 км/ч
}

Telemetry Physics::telemetry() const {
    Telemetry t;
    t.speedKmh = vel.length() * 3.6f;
    t.altM = pos.y;
    t.aglM = pos.y - aircraft::GEAR_HEIGHT;
    t.vsMs = vel.y;
    t.hdgDeg = wrap360(yaw);
    t.thrPct = engineFrac * 100.0f;
    t.flapDeg = flapCur;
    t.onGround = onGround;
    t.stall = stall;
    t.hardLanding = hardLanding;
    t.overspeed = overspeed;
    return t;
}
