#pragma once
// ============================================================
// OpenFlight — физика полёта Ту-154М (этап 0.05).
// Модель: точка массы + угловые скорости с аэродинамической
// устойчивостью. Реальные коэффициенты Ту-154М:
//   масса, площадь крыла, тяга 3х Д-30КУ-154, скорости сваливания.
// Полная модель моментов (6DOF) — следующий этап, раздел 44 концепции.
// ============================================================

#include "math3d.h"
#include "world.h"

struct Controls {
    float throttle  = 0.0f;  // 0..1 — РУД
    float pitchCmd  = 0.0f;  // -1..1 (плюс = штурвал на себя)
    float rollCmd   = 0.0f;  // -1..1 (плюс = крен вправо)
    float rudderCmd = 0.0f;  // -1..1 (плюс = руль вправо)
    bool  brakes    = false; // стояночный/колёсный тормоз
    bool  gearDown  = true;  // шасси
    int   flaps     = 0;     // 0..3 -> 0/15/28/45 градусов
    int   camMode   = 0;     // камера (использует движок): 0 чейз, 1 кабина, 2 вышка
};

struct Telemetry {
    float speedKmh = 0;   // воздушная скорость (здесь = путевая, ветра нет)
    float altM     = 0;   // высота центра фюзеляжа, м
    float aglM     = 0;   // высота над землёй
    float vsMs     = 0;   // вертикальная скорость, м/с
    float hdgDeg   = 0;   // курс 0..360
    float thrPct   = 0;   // тяга, %
    float flapDeg  = 0;   // закрылки, град
    bool  onGround = true;
    bool  stall    = false;
    bool  hardLanding = false; // зафиксирована жёсткая посадка
    bool  overspeed   = false;
};

class Physics {
public:
    // --- Ту-154М ---
    static constexpr float MASS       = 78000.0f;   // кг (заправка ~50%)
    static constexpr float WING_AREA  = 201.45f;    // м2
    static constexpr float MAX_THRUST = 103000.0f;  // Н на двигатель (Д-30КУ-154)
    static constexpr float IDLE_THRUST= 3500.0f;    // Н на двигатель
    static constexpr int   ENGINES    = 3;

    // аэродинамика
    static constexpr float CL0        = 0.18f;
    static constexpr float CL_ALPHA   = 4.6f;       // 1/рад
    static constexpr float ALPHA_CRIT = 0.28f;      // рад (~16°) — сваливание
    static constexpr float CD0        = 0.021f;
    static constexpr float K_INDUCED  = 0.042f;

    // земля
    static constexpr float ROLL_MU    = 0.015f;     // трение качения
    static constexpr float BRAKE_MU   = 0.38f;      // тормоза
    static constexpr float GRASS_MU   = 0.055f;     // трава

    static constexpr float FLAP_DEG[4] = {0.0f, 15.0f, 28.0f, 45.0f};

    Vec3 pos;      // мировая позиция центра фюзеляжа
    Vec3 vel;      // мировая скорость
    float yaw = 140.0f, pitch = 0.0f, roll = 0.0f;
    float pRate = 0, qRate = 0, rRate = 0;   // угловые скорости, град/с
    float engineFrac = 0.0f;                  // обороты двигателей 0..1
    float flapCur = 0.0f;                     // текущее положение закрылков
    bool  onGround = true;
    bool  stall = false;
    bool  hardLanding = false;
    bool  overspeed = false;

    void reset();
    void step(double dt, const Controls& c);
    Telemetry telemetry() const;

private:
    float airDensity(float altitudeM) const;
};
