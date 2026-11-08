#include "aircraft.h"

namespace aircraft {

static const float WHITE_R = 0.93f, WHITE_G = 0.94f, WHITE_B = 0.96f;

void buildAirframe(MeshBuilder& b) {
    // ---------- ФЮЗЕЛЯЖ ----------
    // основная труба
    b.cylinderX(-15.0f, 16.0f, 0.0f, 0.0f, 1.9f, 1.9f, 16,
                WHITE_R, WHITE_G, WHITE_B, 1.0f, true, false, false);
    // носовая часть с наклоном вниз (радиопрозрачный обтекатель серый)
    b.cylinderX(16.0f, 23.9f, 0.0f, -0.40f, 1.9f, 0.30f, 16,
                WHITE_R, WHITE_G, WHITE_B, 1.0f, true, true, true, 0.36f, 0.37f, 0.40f);
    // хвостовая часть (сдвиг вверх — "горбатая" корма Ту-154)
    b.cylinderX(-23.6f, -15.0f, 1.05f, 0.0f, 0.22f, 1.9f, 16,
                WHITE_R, WHITE_G, WHITE_B, 1.0f, false, true, true);
    // фонарь кабины (тёмное кольцо поверх носа)
    b.cylinderX(19.4f, 20.4f, -0.05f, -0.10f, 1.93f, 1.90f, 16,
                0.10f, 0.12f, 0.16f, 1.0f, false, false, false);

    // ---------- КРЫЛЬЯ (низкорасположенные, стреловидные) ----------
    // правое
    b.prismY({Vec3(3.8f, -1.05f, 0.5f), Vec3(-9.8f, -1.05f, 18.8f),
              Vec3(-12.0f, -1.05f, 18.8f), Vec3(-5.8f, -1.05f, 0.5f)},
             0.55f, 0.80f, 0.82f, 0.85f);
    // левое (зеркально)
    b.prismY({Vec3(3.8f, -1.05f, -0.5f), Vec3(-5.8f, -1.05f, -0.5f),
              Vec3(-12.0f, -1.05f, -18.8f), Vec3(-9.8f, -1.05f, -18.8f)},
             0.55f, 0.80f, 0.82f, 0.85f);
    // зализ крыла с фюзеляжем
    b.box(Vec3(0.0f, -1.35f, 0.0f), 13.0f, 1.1f, 6.4f, 0.0f, 0.74f, 0.75f, 0.78f);

    // ---------- КИЛЬ (Т-образное оперение) ----------
    b.prismZ({Vec3(-14.0f, 1.8f, 0.0f), Vec3(-21.5f, 10.4f, 0.0f),
              Vec3(-24.2f, 10.4f, 0.0f), Vec3(-23.4f, 1.8f, 0.0f)},
             0.55f, WHITE_R, WHITE_G, WHITE_B);
    // форкиль
    b.prismZ({Vec3(-8.5f, 0.9f, 0.0f), Vec3(-14.5f, 3.2f, 0.0f),
              Vec3(-14.0f, 0.9f, 0.0f)},
             0.40f, WHITE_R, WHITE_G, WHITE_B);
    // стабилизатор на верхушке киля
    b.prismY({Vec3(-16.8f, 10.1f, 0.3f), Vec3(-21.6f, 10.1f, 6.3f),
              Vec3(-23.1f, 10.1f, 6.3f), Vec3(-22.3f, 10.1f, 0.3f)},
             0.35f, 0.86f, 0.87f, 0.90f);
    b.prismY({Vec3(-16.8f, 10.1f, -0.3f), Vec3(-22.3f, 10.1f, -0.3f),
              Vec3(-23.1f, 10.1f, -6.3f), Vec3(-21.6f, 10.1f, -6.3f)},
             0.35f, 0.86f, 0.87f, 0.90f);

    // ---------- ДВИГАТЕЛИ (3 х Д-30КУ-154) ----------
    const float ENG_R = 0.62f, ENG_G = 0.63f, ENG_B = 0.66f;
    // правый
    b.cylinderX(14.0f, 18.6f, -0.55f, -0.55f, 1.05f, 1.00f, 12,
                ENG_R, ENG_G, ENG_B, 1.0f, false, true, true, 0.06f, 0.06f, 0.07f, 2.95f);
    // левый
    b.cylinderX(14.0f, 18.6f, -0.55f, -0.55f, 1.05f, 1.00f, 12,
                ENG_R, ENG_G, ENG_B, 1.0f, false, true, true, 0.06f, 0.06f, 0.07f, -2.95f);
    // средний (в основании киля, S-канал)
    b.cylinderX(14.8f, 19.2f, 1.15f, 1.15f, 0.95f, 0.92f, 12,
                ENG_R, ENG_G, ENG_B, 1.0f, false, true, true, 0.06f, 0.06f, 0.07f, 0.0f);
    // пилоны боковых двигателей
    b.box(Vec3(16.3f, -0.30f, 2.2f), 2.5f, 0.8f, 1.4f, 0.0f, 0.60f, 0.61f, 0.64f);
    b.box(Vec3(16.3f, -0.30f, -2.2f), 2.5f, 0.8f, 1.4f, 0.0f, 0.60f, 0.61f, 0.64f);
    // воздухозаборник среднего двигателя (горб на "спине")
    b.box(Vec3(13.2f, 0.75f, 0.0f), 4.5f, 1.2f, 1.7f, 0.0f, 0.72f, 0.73f, 0.76f);

    // антенна
    b.box(Vec3(6.0f, 2.05f, 0.0f), 0.7f, 0.45f, 1.8f, 0.0f, 0.50f, 0.50f, 0.53f);
}

void buildGear(MeshBuilder& b) {
    const float STRUT_R = 0.42f, STRUT_G = 0.43f, STRUT_B = 0.46f;
    const float WHEEL_R = 0.10f, WHEEL_G = 0.10f, WHEEL_B = 0.12f;

    // ---------- ПЕРЕДНЯЯ ОПОРА ----------
    b.box(Vec3(14.3f, -2.30f, 0.0f), 0.30f, 1.5f, 0.30f, 0.0f, STRUT_R, STRUT_G, STRUT_B);
    b.cylinderZ(Vec3(14.3f, -2.85f, 0.42f), 0.55f, 0.34f, 10, WHEEL_R, WHEEL_G, WHEEL_B);
    b.cylinderZ(Vec3(14.3f, -2.85f, -0.42f), 0.55f, 0.34f, 10, WHEEL_R, WHEEL_G, WHEEL_B);

    // ---------- ОСНОВНЫЕ ОПОРЫ ----------
    for (float sz : {1.0f, -1.0f}) {
        float z = 2.7f * sz;
        b.box(Vec3(-2.6f, -2.30f, z), 0.35f, 1.5f, 0.35f, 0.0f, STRUT_R, STRUT_G, STRUT_B);
        // тележка
        b.box(Vec3(-2.6f, -2.85f, z), 1.7f, 0.25f, 0.30f, 0.0f, STRUT_R, STRUT_G, STRUT_B);
        // колёса (передняя и задняя пара тележки)
        b.cylinderZ(Vec3(-3.3f, -2.85f, z), 0.55f, 0.36f, 10, WHEEL_R, WHEEL_G, WHEEL_B);
        b.cylinderZ(Vec3(-1.9f, -2.85f, z), 0.55f, 0.36f, 10, WHEEL_R, WHEEL_G, WHEEL_B);
    }
}

void buildShadow(MeshBuilder& b) {
    b.ellipse(Vec3(0.0f, 0.0f, 0.0f), 26.0f, 20.0f, 22, 0.0f, 0.0f, 0.0f, 0.30f);
}

} // namespace aircraft
