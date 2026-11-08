#include "world.h"
#include <cmath>

namespace world {

Vec3 runwayDir() {
    float a = RUNWAY_HEADING * DEG2RAD;
    return Vec3(sinf(a), 0.0f, -cosf(a));
}

Vec3 runwayRight() {
    float a = RUNWAY_HEADING * DEG2RAD;
    return Vec3(cosf(a), 0.0f, sinf(a));
}

// Прямоугольная полоса в системе координат ВПП:
// u — вдоль ВПП (м от центра), v — поперёк (м вправо от оси), y — высота
static void strip(MeshBuilder& b, float u0, float u1, float v0, float v1,
                  float y, float r, float g, float bl) {
    Vec3 dir = runwayDir(), right = runwayRight();
    Vec3 A = dir * u0 + right * v0 + Vec3(0, y, 0);
    Vec3 B = dir * u0 + right * v1 + Vec3(0, y, 0);
    Vec3 C = dir * u1 + right * v1 + Vec3(0, y, 0);
    Vec3 D = dir * u1 + right * v0 + Vec3(0, y, 0);
    b.quad(A, B, C, D, r, g, bl);
}

bool buildWorld(MeshBuilder& b) {
    Vec3 dir = runwayDir(), right = runwayRight();
    auto W = [&](float u, float v) { return dir * u + right * v; };

    // ---------- ЗЕМЛЯ (сетка 160x160, ячейка 500 м, итог 80x80 км) ----------
    const int N = 160;
    const float CELL = 500.0f;
    const float HALF = N * CELL * 0.5f;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            float x0 = -HALF + i * CELL, x1 = x0 + CELL;
            float z0 = -HALF + j * CELL, z1 = z0 + CELL;
            float xc = x0 + CELL * 0.5f, zc = z0 + CELL * 0.5f;
            // детерминированный "шум" для разнообразия травы
            float n1 = fmodf(sinf(i * 12.9898f + j * 78.233f) * 43758.5453f, 1.0f);
            if (n1 < 0) n1 = -n1;
            float n2 = 0.5f + 0.5f * sinf(xc * 0.0006f) * cosf(zc * 0.0008f);
            float gr = 0.30f + 0.05f * n1 + 0.03f * n2;
            float gg = 0.37f + 0.05f * n1 + 0.03f * n2;
            float gb = 0.21f + 0.04f * n1 + 0.02f * n2;
            // возле аэропорта трава чуть желтее (вытоптанная зона)
            float u = xc * dir.x + zc * dir.z;
            float v = xc * right.x + zc * right.z;
            if (fabsf(u) < 2500.0f && fabsf(v) < 1300.0f) {
                gr = gr * 0.5f + 0.34f * 0.5f;
                gg = gg * 0.5f + 0.40f * 0.5f;
                gb = gb * 0.5f + 0.24f * 0.5f;
            }
            b.quad(Vec3(x0, 0, z0), Vec3(x0, 0, z1), Vec3(x1, 0, z1), Vec3(x1, 0, z0),
                   gr, gg, gb);
        }
    }

    // ---------- ВПП 14/32 ----------
    strip(b, -1900, 1900, -30, 30, 0.7f, 0.33f, 0.33f, 0.35f);

    // разметка (белая, y = 1.8)
    const float W_R = 0.92f, W_G = 0.92f, W_B = 0.90f;
    // осевая линия: штрихи 30 м через 22 м
    for (float u = -1656.0f; u <= 1656.0f; u += 52.0f)
        strip(b, u - 15.0f, u + 15.0f, -0.45f, 0.45f, 1.8f, W_R, W_G, W_B);
    // пороговые полосы: 8 штук с каждого торца
    const float bars[4] = {3.75f, 11.25f, 18.75f, 26.25f};
    for (float v : bars) {
        strip(b, 1864.0f, 1894.0f, v - 0.9f, v + 0.9f, 1.8f, W_R, W_G, W_B);
        strip(b, 1864.0f, 1894.0f, -v - 0.9f, -v + 0.9f, 1.8f, W_R, W_G, W_B);
        strip(b, -1894.0f, -1864.0f, v - 0.9f, v + 0.9f, 1.8f, W_R, W_G, W_B);
        strip(b, -1894.0f, -1864.0f, -v - 0.9f, -v + 0.9f, 1.8f, W_R, W_G, W_B);
    }
    // зоны приземления (большой прицельный прямоугольник) в 300 м от торцов
    for (float su : {1.0f, -1.0f}) {
        strip(b, su * 1577.0f, su * 1622.0f, 6.0f, 12.0f, 1.8f, W_R, W_G, W_B);
        strip(b, su * 1577.0f, su * 1622.0f, -12.0f, -6.0f, 1.8f, W_R, W_G, W_B);
    }
    // боковые линии
    strip(b, -1870.0f, 1870.0f, 28.9f, 29.8f, 1.8f, W_R, W_G, W_B);
    strip(b, -1870.0f, 1870.0f, -29.8f, -28.9f, 1.8f, W_R, W_G, W_B);

    // ---------- РУЛЁЖНЫЕ ДОРОЖКИ ----------
    const float T_R = 0.40f, T_G = 0.40f, T_B = 0.42f;
    // параллельная РД
    strip(b, -1750.0f, 1550.0f, 240.0f, 263.0f, 0.7f, T_R, T_G, T_B);
    strip(b, -1740.0f, 1540.0f, 250.6f, 251.4f, 1.6f, 0.85f, 0.78f, 0.30f); // осевая жёлтая
    // соединительные РД
    for (float cu : {-1500.0f, -200.0f, 1100.0f}) {
        strip(b, cu - 11.5f, cu + 11.5f, 30.0f, 240.0f, 0.7f, T_R, T_G, T_B);
        strip(b, cu - 0.4f, cu + 0.4f, 32.0f, 238.0f, 1.6f, 0.85f, 0.78f, 0.30f);
    }

    // ---------- ПЕРРОН ----------
    strip(b, -620.0f, 110.0f, 300.0f, 860.0f, 0.7f, 0.45f, 0.45f, 0.47f);
    // места стоянок (тонкие белые линии)
    for (float su = -500.0f; su <= 0.0f; su += 125.0f)
        strip(b, su - 0.4f, su + 0.4f, 330.0f, 700.0f, 1.6f, 0.85f, 0.85f, 0.83f);

    // ---------- ЗДАНИЯ ----------
    // основной терминал (повёрнут вдоль ВПП)
    const float BY = 90.0f - RUNWAY_HEADING; // yaw коробки для выравнивания по ВПП
    b.box(W(-260.0f, 1000.0f) + Vec3(0, 10.7f, 0), 420.0f, 21.0f, 55.0f, BY, 0.75f, 0.76f, 0.79f);
    // стеклянный фасад
    b.box(W(-260.0f, 969.0f) + Vec3(0, 6.5f, 0), 424.0f, 9.0f, 3.0f, BY, 0.22f, 0.34f, 0.52f);
    // второй корпус
    b.box(W(180.0f, 1000.0f) + Vec3(0, 8.7f, 0), 220.0f, 16.0f, 45.0f, BY, 0.72f, 0.73f, 0.77f);
    b.box(W(180.0f, 975.0f) + Vec3(0, 5.0f, 0), 224.0f, 7.0f, 3.0f, BY, 0.24f, 0.35f, 0.53f);
    // вышка ДМД
    b.box(W(380.0f, 820.0f) + Vec3(0, 19.0f, 0), 9.0f, 38.0f, 9.0f, BY, 0.68f, 0.69f, 0.72f);
    b.box(W(380.0f, 820.0f) + Vec3(0, 42.0f, 0), 15.0f, 7.0f, 15.0f, BY, 0.20f, 0.30f, 0.47f);
    b.box(W(380.0f, 820.0f) + Vec3(0, 46.5f, 0), 11.0f, 2.5f, 11.0f, BY, 0.60f, 0.60f, 0.63f);
    // ангары
    b.box(W(650.0f, 1120.0f) + Vec3(0, 9.7f, 0), 100.0f, 19.0f, 75.0f, BY, 0.55f, 0.57f, 0.60f);
    b.box(W(790.0f, 1120.0f) + Vec3(0, 9.7f, 0), 100.0f, 19.0f, 75.0f, BY, 0.52f, 0.54f, 0.58f);
    // служебные постройки
    b.box(W(-100.0f, 640.0f) + Vec3(0, 4.0f, 0), 40.0f, 7.0f, 25.0f, BY, 0.62f, 0.63f, 0.66f);
    b.box(W(-420.0f, 560.0f) + Vec3(0, 3.5f, 0), 30.0f, 6.0f, 20.0f, BY, 0.60f, 0.61f, 0.64f);

    return true;
}

void buildClouds(MeshBuilder& b) {
    // детерминированные кластеры вокруг аэропорта (x, высота, z)
    const float clusters[9][3] = {
        { 3000.0f, 1100.0f, -2500.0f},
        {-4000.0f, 1300.0f,  5000.0f},
        { 8000.0f,  950.0f,  3000.0f},
        {-7000.0f, 1500.0f, -6000.0f},
        {12000.0f, 1200.0f, -3000.0f},
        {-12000.0f, 1000.0f, 8000.0f},
        { 5000.0f, 1400.0f,  9000.0f},
        {-3000.0f,  900.0f, -11000.0f},
        { 10000.0f, 1300.0f, 10000.0f},
    };
    for (auto& cl : clusters) {
        Vec3 base(cl[0], cl[1], cl[2]);
        for (int k = 0; k < 8; k++) {
            float dx = (float)((k * 37) % 90) - 45.0f;
            float dz = (float)((k * 53) % 80) - 40.0f;
            float dy = (float)((k * 29) % 30) - 10.0f;
            float rad = 75.0f + (float)((k * 31) % 55);
            b.sphere(base + Vec3(dx, dy, dz), rad, 0.55f, 5, 8, 0.97f, 0.975f, 0.99f);
        }
    }
}

bool isOnPavement(const Vec3& p) {
    Vec3 dir = runwayDir(), right = runwayRight();
    float u = p.dot(dir);
    float v = p.dot(right);
    // ВПП
    if (fabsf(u) <= 1910.0f && fabsf(v) <= 32.0f) return true;
    // зона перрона и РД
    if (v >= 235.0f && v <= 865.0f && u >= -630.0f && u <= 1160.0f) return true;
    // соединительные РД
    for (float cu : {-1500.0f, -200.0f, 1100.0f})
        if (fabsf(u - cu) <= 14.0f && v >= 28.0f && v <= 265.0f) return true;
    return false;
}

} // namespace world
