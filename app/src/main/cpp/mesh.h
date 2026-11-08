#pragma once
// ============================================================
// OpenFlight — конструктор геометрии (позиция + нормаль + цвет RGBA)
// Все фигуры строятся прямо в коде: внешних моделей на этапе 0.05 нет.
// ============================================================

#include <vector>
#include <cstdint>
#include "math3d.h"

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b, a;
};

class MeshBuilder {
public:
    std::vector<Vertex> verts;

    size_t count() const { return verts.size(); }
    void clear() { verts.clear(); }

    void triangle(const Vec3& a, const Vec3& b, const Vec3& c,
                  float r, float g, float bl, float al = 1.0f) {
        Vec3 n = (b - a).cross(c - a).normalized();
        Vertex v;
        v.nx = n.x; v.ny = n.y; v.nz = n.z;
        v.r = r; v.g = g; v.b = bl; v.a = al;
        const Vec3* pts[3] = {&a, &b, &c};
        for (int i = 0; i < 3; i++) {
            v.px = pts[i]->x; v.py = pts[i]->y; v.pz = pts[i]->z;
            verts.push_back(v);
        }
    }

    // Четырёхугольник; порядок вершин против часовой стрелки, если
    // смотреть с внешней стороны (нормаль считается автоматически)
    void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
              float r, float g, float bl, float al = 1.0f) {
        triangle(a, b, c, r, g, bl, al);
        triangle(a, c, d, r, g, bl, al);
    }

    // Коробка с поворотом вокруг вертикальной оси (yaw, градусы)
    void box(const Vec3& center, float sx, float sy, float sz, float yawDeg,
             float r, float g, float bl, float al = 1.0f) {
        float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
        float a = yawDeg * DEG2RAD, c = cosf(a), s = sinf(a);
        Vec3 p[8];
        int idx = 0;
        for (int iy = -1; iy <= 1; iy += 2)
            for (int iz = -1; iz <= 1; iz += 2)
                for (int ix = -1; ix <= 1; ix += 2) {
                    float lx = ix * hx, ly = iy * hy, lz = iz * hz;
                    p[idx++] = Vec3(center.x + lx * c + lz * s,
                                    center.y + ly,
                                    center.z - lx * s + lz * c);
                }
        // индексы: бит 0 = x, бит 1 = z, бит 2 = y
        auto at = [](Vec3* arr, int x, int y, int z) {
            return arr[x + z * 2 + y * 4];
        };
        // +X
        quad(at(p, 1, 0, 1), at(p, 1, 0, 0), at(p, 1, 1, 0), at(p, 1, 1, 1), r, g, bl, al);
        // -X
        quad(at(p, 0, 0, 0), at(p, 0, 0, 1), at(p, 0, 1, 1), at(p, 0, 1, 0), r, g, bl, al);
        // +Y
        quad(at(p, 0, 1, 1), at(p, 1, 1, 1), at(p, 1, 1, 0), at(p, 0, 1, 0), r, g, bl, al);
        // -Y
        quad(at(p, 0, 0, 0), at(p, 1, 0, 0), at(p, 1, 0, 1), at(p, 0, 0, 1), r, g, bl, al);
        // +Z
        quad(at(p, 0, 0, 1), at(p, 1, 0, 1), at(p, 1, 1, 1), at(p, 0, 1, 1), r, g, bl, al);
        // -Z
        quad(at(p, 1, 0, 0), at(p, 0, 0, 0), at(p, 0, 1, 0), at(p, 1, 1, 0), r, g, bl, al);
    }

    // Труба вдоль X: x0->x1, радиус r0->r1, центр сечения cy0->cy1 (сдвиг хвоста вверх).
    // livery: синяя полоса по борту (только фюзеляж).
    void cylinderX(float x0, float x1, float cy0, float cy1,
                   float r0, float r1, int segments,
                   float r, float g, float bl, float al,
                   bool livery = false,
                   bool capBack = true, bool capFront = true,
                   float capFrontR = -1.0f, float capFrontG = 0, float capFrontB = 0,
                   float cz = 0.0f) {
        if (capFrontR < 0) { capFrontR = r; capFrontG = g; capFrontB = bl; }
        for (int i = 0; i < segments; i++) {
            float t0 = 2.0f * PI_F * i / segments;
            float t1 = 2.0f * PI_F * (i + 1) / segments;
            Vec3 a0(x0, cy0 + r0 * cosf(t0), cz + r0 * sinf(t0));
            Vec3 a1(x0, cy0 + r0 * cosf(t1), cz + r0 * sinf(t1));
            Vec3 b1(x1, cy1 + r1 * cosf(t1), cz + r1 * sinf(t1));
            Vec3 b0(x1, cy1 + r1 * cosf(t0), cz + r1 * sinf(t0));
            // цвет с учётом покраски (один на сегмент — low-poly стиль)
            Vec3 n0(0, cosf(t0), sinf(t0));
            float c0r = r, c0g = g, c0b = bl;
            if (livery) {
                if (n0.y < -0.55f) { c0r = 0.78f; c0g = 0.80f; c0b = 0.84f; }
                else if (fabsf(n0.y) < 0.55f) { c0r = 0.10f; c0g = 0.20f; c0b = 0.45f; }
            }
            quad(a0, a1, b1, b0, c0r, c0g, c0b, al);
        }
        if (capBack) { // задняя крышка (x0), нормаль -X
            Vec3 ctr(x0, cy0, cz);
            for (int i = 0; i < segments; i++) {
                float t0 = 2.0f * PI_F * i / segments;
                float t1 = 2.0f * PI_F * (i + 1) / segments;
                Vec3 p0(x0, cy0 + r0 * cosf(t0), cz + r0 * sinf(t0));
                Vec3 p1(x0, cy0 + r0 * cosf(t1), cz + r0 * sinf(t1));
                Vec3 n(-1, 0, 0);
                verts.push_back({ctr.x, ctr.y, ctr.z, n.x, n.y, n.z, r, g, bl, al});
                verts.push_back({p1.x, p1.y, p1.z, n.x, n.y, n.z, r, g, bl, al});
                verts.push_back({p0.x, p0.y, p0.z, n.x, n.y, n.z, r, g, bl, al});
            }
        }
        if (capFront) { // передняя крышка (x1), нормаль +X
            Vec3 ctr(x1, cy1, cz);
            for (int i = 0; i < segments; i++) {
                float t0 = 2.0f * PI_F * i / segments;
                float t1 = 2.0f * PI_F * (i + 1) / segments;
                Vec3 p0(x1, cy1 + r1 * cosf(t0), cz + r1 * sinf(t0));
                Vec3 p1(x1, cy1 + r1 * cosf(t1), cz + r1 * sinf(t1));
                Vec3 n(1, 0, 0);
                verts.push_back({ctr.x, ctr.y, ctr.z, n.x, n.y, n.z, capFrontR, capFrontG, capFrontB, al});
                verts.push_back({p0.x, p0.y, p0.z, n.x, n.y, n.z, capFrontR, capFrontG, capFrontB, al});
                verts.push_back({p1.x, p1.y, p1.z, n.x, n.y, n.z, capFrontR, capFrontG, capFrontB, al});
            }
        }
    }

    // Колесо: цилиндр вдоль Z (ось вращения — по размаху)
    void cylinderZ(const Vec3& center, float radius, float width, int segments,
                   float r, float g, float bl, float al = 1.0f) {
        float z0 = center.z - width * 0.5f, z1 = center.z + width * 0.5f;
        for (int i = 0; i < segments; i++) {
            float t0 = 2.0f * PI_F * i / segments;
            float t1 = 2.0f * PI_F * (i + 1) / segments;
            Vec3 a0(center.x + radius * sinf(t0), center.y + radius * cosf(t0), z0);
            Vec3 a1(center.x + radius * sinf(t1), center.y + radius * cosf(t1), z0);
            Vec3 b1(center.x + radius * sinf(t1), center.y + radius * cosf(t1), z1);
            Vec3 b0(center.x + radius * sinf(t0), center.y + radius * cosf(t0), z1);
            quad(a0, b0, b1, a1, r, g, bl, al); // нормаль наружу: (sin t, cos t, 0)
        }
        // крышки
        Vec3 n0(0, 0, -1), n1(0, 0, 1);
        for (int i = 0; i < segments; i++) {
            float t0 = 2.0f * PI_F * i / segments;
            float t1 = 2.0f * PI_F * (i + 1) / segments;
            Vec3 p0(center.x + radius * sinf(t0), center.y + radius * cosf(t0), z0);
            Vec3 p1(center.x + radius * sinf(t1), center.y + radius * cosf(t1), z0);
            verts.push_back({center.x, center.y, z0, n0.x, n0.y, n0.z, r, g, bl, al});
            verts.push_back({p1.x, p1.y, p1.z, n0.x, n0.y, n0.z, r, g, bl, al});
            verts.push_back({p0.x, p0.y, p0.z, n0.x, n0.y, n0.z, r, g, bl, al});
            Vec3 q0(center.x + radius * sinf(t0), center.y + radius * cosf(t0), z1);
            Vec3 q1(center.x + radius * sinf(t1), center.y + radius * cosf(t1), z1);
            verts.push_back({center.x, center.y, z1, n1.x, n1.y, n1.z, r, g, bl, al});
            verts.push_back({q0.x, q0.y, q0.z, n1.x, n1.y, n1.z, r, g, bl, al});
            verts.push_back({q1.x, q1.y, q1.z, n1.x, n1.y, n1.z, r, g, bl, al});
        }
    }

    // Плоская призма: полигон в XZ (y — база), выдавливание вниз на thickness
    // (крылья, стабилизаторы). Порядок точек полигон замкнутый.
    void prismY(const std::vector<Vec3>& poly, float thickness,
                float r, float g, float bl, float al = 1.0f) {
        std::vector<Vec3> p = poly;
        // фиксируем ориентацию: нормаль верхней грани должна смотреть в +Y
        Vec3 nn = newell(p);
        if (nn.y < 0) std::reverse(p.begin(), p.end());
        std::vector<Vec3> top(p.size()), bot(p.size());
        for (size_t i = 0; i < p.size(); i++) {
            top[i] = Vec3(p[i].x, p[i].y, p[i].z);
            bot[i] = Vec3(p[i].x, p[i].y - thickness, p[i].z);
        }
        // верхняя грань
        for (size_t i = 1; i < p.size() - 1; i++)
            triangle(top[0], top[i], top[i + 1], r, g, bl, al);
        // нижняя грань
        for (size_t i = 1; i < p.size() - 1; i++)
            triangle(bot[0], bot[i + 1], bot[i], r, g, bl, al);
        // боковые грани
        for (size_t i = 0; i < p.size(); i++) {
            size_t j = (i + 1) % p.size();
            quad(bot[i], bot[j], top[j], top[i], r, g, bl, al);
        }
    }

    // Вертикальная призма: полигон в XY (киль), выдавливание по Z на thickness
    void prismZ(const std::vector<Vec3>& poly, float thickness,
                float r, float g, float bl, float al = 1.0f) {
        std::vector<Vec3> p = poly;
        Vec3 nn = newell(p);
        if (nn.z < 0) std::reverse(p.begin(), p.end());
        float h = thickness * 0.5f;
        std::vector<Vec3> front(p.size()), back(p.size());
        for (size_t i = 0; i < p.size(); i++) {
            front[i] = Vec3(p[i].x, p[i].y, p[i].z + h);
            back[i]  = Vec3(p[i].x, p[i].y, p[i].z - h);
        }
        for (size_t i = 1; i < p.size() - 1; i++)
            triangle(front[0], front[i], front[i + 1], r, g, bl, al);
        for (size_t i = 1; i < p.size() - 1; i++)
            triangle(back[0], back[i + 1], back[i], r, g, bl, al);
        for (size_t i = 0; i < p.size(); i++) {
            size_t j = (i + 1) % p.size();
            quad(back[i], back[j], front[j], front[i], r, g, bl, al);
        }
    }

    // Плоский диск в XZ (тень)
    void ellipse(const Vec3& center, float rx, float rz, int segments,
                 float r, float g, float bl, float al = 1.0f) {
        Vec3 n(0, 1, 0);
        for (int i = 0; i < segments; i++) {
            float t0 = 2.0f * PI_F * i / segments;
            float t1 = 2.0f * PI_F * (i + 1) / segments;
            Vec3 p0(center.x + rx * cosf(t0), center.y, center.z + rz * sinf(t0));
            Vec3 p1(center.x + rx * cosf(t1), center.y, center.z + rz * sinf(t1));
            verts.push_back({center.x, center.y, center.z, n.x, n.y, n.z, r, g, bl, al});
            verts.push_back({p0.x, p0.y, p0.z, n.x, n.y, n.z, r, g, bl, al});
            verts.push_back({p1.x, p1.y, p1.z, n.x, n.y, n.z, r, g, bl, al});
        }
    }

    // Облако-«пух»: приплюснутая сфера
    void sphere(const Vec3& center, float radius, float squashY,
                int latSeg, int lonSeg, float r, float g, float bl, float al = 1.0f) {
        for (int la = 0; la < latSeg; la++) {
            float th0 = PI_F * la / latSeg, th1 = PI_F * (la + 1) / latSeg;
            for (int lo = 0; lo < lonSeg; lo++) {
                float ph0 = 2.0f * PI_F * lo / lonSeg, ph1 = 2.0f * PI_F * (lo + 1) / lonSeg;
                auto pt = [&](float th, float ph) {
                    return Vec3(center.x + radius * sinf(th) * cosf(ph),
                                center.y + radius * cosf(th) * squashY,
                                center.z + radius * sinf(th) * sinf(ph));
                };
                Vec3 a = pt(th0, ph0), b = pt(th0, ph1), c = pt(th1, ph1), d = pt(th1, ph0);
                Vec3 nrm = (a + b + c + d) * 0.25f - center;
                nrm = nrm.normalized();
                float shade = nrm.y < -0.15f ? 0.93f : 1.0f;
                quad(a, b, c, d, r * shade, g * shade, bl * shade, al);
            }
        }
    }

private:
    Vec3 newell(const std::vector<Vec3>& p) {
        Vec3 n(0, 0, 0);
        for (size_t i = 0; i < p.size(); i++) {
            const Vec3& a = p[i];
            const Vec3& b = p[(i + 1) % p.size()];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        return n;
    }
};
