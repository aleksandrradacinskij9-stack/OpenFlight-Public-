#pragma once
// ============================================================
// OpenFlight — мир: аэропорт Домодедово (UUDD), ВПП 14/32
// Центр ВПП — в начале координат (0, 0, 0).
// ============================================================

#include "mesh.h"

namespace world {

constexpr float RUNWAY_LENGTH  = 3800.0f;  // длина ВПП, м
constexpr float RUNWAY_WIDTH   = 60.0f;    // ширина ВПП, м
constexpr float RUNWAY_HEADING = 140.0f;   // компасный курс ВПП 14/32

// Единичный вектор курса 14 (куда садимся/взлетаем)
Vec3 runwayDir();
// Единичный вектор вправо от курса 14
Vec3 runwayRight();

// Земля + ВПП + разметка + рулёжные дорожки + перрон + здания
bool buildWorld(MeshBuilder& out);

// Облака (позиции прямо в мировых координатах)
void buildClouds(MeshBuilder& out);

// Для физики: стоит ли самолёт на искусственном покрытии (меньше трение)
bool isOnPavement(const Vec3& p);

} // namespace world
