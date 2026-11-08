#pragma once
// ============================================================
// OpenFlight — минимальная 3D-математика (column-major, как в OpenGL/Vulkan)
// Ориентация модели: +X вперёд, +Y вверх, +Z вправо
// Мир: +X восток, +Y высота, -Z север. Курс 0° = север.
// ============================================================

#include <cmath>

constexpr float PI_F = 3.14159265358979f;
constexpr float DEG2RAD = PI_F / 180.0f;
constexpr float RAD2DEG = 180.0f / PI_F;

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    float length() const { return sqrtf(x * x + y * y + z * z); }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y,
                z * o.x - x * o.z,
                x * o.y - y * o.x};
    }

    Vec3 normalized() const {
        float l = length();
        if (l < 1e-8f) return {0, 0, 0};
        return {x / l, y / l, z / l};
    }
};

// Матрица 4x4, column-major (m[col*4 + row]) — совместима с OpenGL/Vulkan
struct Mat4 {
    float m[16];

    Mat4() { identity(); }

    void identity() {
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int c = 0; c < 4; c++) {
            for (int rr = 0; rr < 4; rr++) {
                float s = 0.0f;
                for (int k = 0; k < 4; k++)
                    s += a.m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        }
        return r;
    }

    // Применить к локальной точке (x,y,z,1)
    Vec3 transformPoint(const Vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }

    static Mat4 perspective(float fovYDeg, float aspect, float zNear, float zFar) {
        Mat4 r;
        r.identity();
        float f = 1.0f / tanf(fovYDeg * DEG2RAD * 0.5f);
        r.m[0]  = f / aspect;
        r.m[5]  = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        r.m[15] = 0.0f;
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();   // вперёд
        Vec3 s = f.cross(up).normalized();      // вправо
        Vec3 u = s.cross(f);                    // истинный верх
        Mat4 r;
        r.identity();
        r.m[0]  =  s.x; r.m[4]  =  s.y; r.m[8]  =  s.z;
        r.m[1]  =  u.x; r.m[5]  =  u.y; r.m[9]  =  u.z;
        r.m[2]  = -f.x; r.m[6]  = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] =  f.dot(eye);
        return r;
    }

    static Mat4 translation(float x, float y, float z) {
        Mat4 r; r.identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }

    static Mat4 scale(float sx, float sy, float sz) {
        Mat4 r; r.identity();
        r.m[0] = sx; r.m[5] = sy; r.m[10] = sz;
        return r;
    }

    static Mat4 rotateY(float deg) {
        Mat4 r; r.identity();
        float a = deg * DEG2RAD, c = cosf(a), s = sinf(a);
        r.m[0] = c; r.m[2] = -s;
        r.m[8] = s; r.m[10] = c;
        return r;
    }

    // Матрица тела: позиция + курс/тангаж/крен.
    // Курс 0° = север (-Z), 90° = восток (+X), по часовой стрелке.
    static Mat4 bodyPose(const Vec3& pos, float yawDeg, float pitchDeg, float rollDeg) {
        float th = yawDeg * DEG2RAD, ph = pitchDeg * DEG2RAD, rl = rollDeg * DEG2RAD;
        float sy = sinf(th), cy = cosf(th);
        float sp = sinf(ph), cp = cosf(ph);
        float sr = sinf(rl), cr = cosf(rl);

        Vec3 fwd(sy * cp, sp, -cy * cp);        // вперёд (ось X модели)
        Vec3 rightH(cy, 0.0f, sy);              // горизонтальное "вправо"
        Vec3 up0(-sy * sp, cp, cy * sp);        // верх до крена
        Vec3 right = rightH * cr - up0 * sr;    // с креном
        Vec3 up    = rightH * sr + up0 * cr;

        Mat4 r;
        r.m[0] = fwd.x;   r.m[1] = fwd.y;   r.m[2] = fwd.z;   r.m[3] = 0.0f;
        r.m[4] = up.x;    r.m[5] = up.y;    r.m[6] = up.z;    r.m[7] = 0.0f;
        r.m[8] = right.x; r.m[9] = right.y; r.m[10] = right.z; r.m[11] = 0.0f;
        r.m[12] = pos.x;  r.m[13] = pos.y;  r.m[14] = pos.z;  r.m[15] = 1.0f;
        return r;
    }
};

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float wrap360(float deg) {
    float d = fmodf(deg, 360.0f);
    if (d < 0) d += 360.0f;
    return d;
}
