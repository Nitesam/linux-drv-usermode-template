#ifndef DBD_ESP_H
#define DBD_ESP_H

#include <cmath>
#include <algorithm>
#include "imgui.h"
#include "dbd_offsets.h"
#include "dbd_reader.h"

namespace dbd_w2s {

constexpr float kPi = 3.1415926f;
constexpr float kRotToRad = kPi / 180.0f;

struct ScreenPoint { float X{}, Y{}; };
struct ScreenSize  { int Width{}, Height{}; };

struct CameraAxes {
    float axX_x{}, axX_y{}, axX_z{};
    float axY_x{}, axY_y{}, axY_z{};
    float axZ_x{}, axZ_y{}, axZ_z{};
    double camX{}, camY{}, camZ{};
    float fov_factor{};
};

inline CameraAxes BuildAxes(const DbdMinimalViewInfo& cam) {
    float pitch = static_cast<float>(cam.Rotation.Pitch) * kRotToRad;
    float yaw   = static_cast<float>(cam.Rotation.Yaw)   * kRotToRad;

    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cy = std::cos(yaw),   sy = std::sin(yaw);

    CameraAxes a{};
    a.axX_x = cp * cy;  a.axX_y = cp * sy;  a.axX_z = sp;
    a.axY_x = -sy;      a.axY_y = cy;        a.axY_z = 0.0f;
    a.axZ_x = -(sp*cy); a.axZ_y = -(sp*sy);  a.axZ_z = cp;

    a.camX = cam.Location.X;
    a.camY = cam.Location.Y;
    a.camZ = cam.Location.Z;
    a.fov_factor = std::max(std::tan(cam.FOV * kPi / 360.0f), 0.001f);
    return a;
}

inline ScreenPoint Project(const DbdUEVector& world, const CameraAxes& a, const ScreenSize& ss) {
    float dx = static_cast<float>(world.X - a.camX);
    float dy = static_cast<float>(world.Y - a.camY);
    float dz = static_cast<float>(world.Z - a.camZ);

    float tx = dx * a.axY_x + dy * a.axY_y + dz * a.axY_z;
    float ty = dx * a.axZ_x + dy * a.axZ_y + dz * a.axZ_z;
    float tz = dx * a.axX_x + dy * a.axX_y + dz * a.axX_z;
    if (tz < 1.0f) tz = 1.0f;

    float cx = static_cast<float>(ss.Width)  * 0.5f;
    float cy = static_cast<float>(ss.Height) * 0.5f;
    return {
        cx + tx * (cx / a.fov_factor) / tz,
        cy + (-ty) * (cx / a.fov_factor) / tz,
    };
}

inline bool IsInFront(const DbdUEVector& w, const CameraAxes& a) {
    float dx = static_cast<float>(w.X - a.camX);
    float dy = static_cast<float>(w.Y - a.camY);
    float dz = static_cast<float>(w.Z - a.camZ);
    return (dx * a.axX_x + dy * a.axX_y + dz * a.axX_z) >= 1.0f;
}

inline bool IsOnScreen(const ScreenPoint& sp, const ScreenSize& ss) {
    return sp.X >= 0 && sp.X <= ss.Width && sp.Y >= 0 && sp.Y <= ss.Height;
}

}

struct DbdEspSettings {
    bool  show_name     = true;
    bool  show_distance = true;
    float max_distance  = 200.0f;
};

class DbdEspRenderer {
public:
    DbdEspSettings settings;

    void render(ImDrawList* dl, const DbdWorldState& state, int sw, int sh)
    {
        if (!state.valid || !state.has_camera)
            return;

        const dbd_w2s::ScreenSize ss{sw, sh};
        const auto axes = dbd_w2s::BuildAxes(state.camera);

        for (const auto& p : state.players) {
            if (!p.valid)
                continue;
            if (p.distance > settings.max_distance && p.distance > 0)
                continue;
            if (!dbd_w2s::IsInFront(p.position, axes))
                continue;

            DbdUEVector head_pos = p.position;
            head_pos.Z += 90.0;

            auto sp = dbd_w2s::Project(head_pos, axes, ss);
            if (!dbd_w2s::IsOnScreen(sp, ss))
                continue;

            ImU32 col;
            if (p.type == EDbdActorType::Survivor)
                col = IM_COL32(0, 220, 50, 255);
            else
                col = IM_COL32(255, 50, 50, 255);

            std::string label;
            if (settings.show_name)
                label = p.name;
            if (settings.show_distance && p.distance > 0) {
                char dbuf[32];
                snprintf(dbuf, sizeof(dbuf), " [%dm]", static_cast<int>(p.distance));
                label += dbuf;
            }

            if (!label.empty()) {
                auto ts = ImGui::CalcTextSize(label.c_str());
                float lx = sp.X - ts.x * 0.5f;
                float ly = sp.Y - ts.y - 4.0f;

                dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 200), label.c_str());
                dl->AddText(ImVec2(lx, ly), col, label.c_str());
            }

            dl->AddCircleFilled(ImVec2(sp.X, sp.Y), 3.0f, col);
        }
    }

    void render_controls()
    {
        ImGui::Checkbox("Show Name", &settings.show_name);
        ImGui::Checkbox("Show Distance", &settings.show_distance);
        ImGui::SliderFloat("Max Distance (m)", &settings.max_distance, 50.0f, 500.0f, "%.0f");
    }
};

#endif
