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

inline ImU32 DbdObjectColor(EDbdObjectType t) {
    switch (t) {
        case EDbdObjectType::Generator:     return IM_COL32(255, 215,   0, 255);
        case EDbdObjectType::Totem:         return IM_COL32(255, 105, 180, 255);
        case EDbdObjectType::Pallet:        return IM_COL32(  0, 206, 209, 255);
        case EDbdObjectType::Hook:          return IM_COL32(255,  69,   0, 255);
        case EDbdObjectType::Hatch:         return IM_COL32(148,   0, 211, 255);
        case EDbdObjectType::Locker:        return IM_COL32(128, 128, 128, 255);
        case EDbdObjectType::Chest:         return IM_COL32(218, 165,  32, 255);
        case EDbdObjectType::Window:        return IM_COL32(135, 206, 235, 255);
        case EDbdObjectType::Trap:          return IM_COL32(220,  20,  60, 255);
        case EDbdObjectType::EscapeDoor:    return IM_COL32( 50, 205,  50, 255);
        case EDbdObjectType::BreakableDoor: return IM_COL32(210, 105,  30, 255);
        default:                            return IM_COL32(200, 200, 200, 255);
    }
}

struct DbdEspSettings {
    bool  show_name     = true;
    bool  show_distance = true;
    float max_distance  = 200.0f;

    bool  show_objects   = true;
    float obj_max_dist   = 150.0f;
    bool  obj_show[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        true,  true,  true,  true,  true,
        true,  true,  true,  true,  true, true
    };

    bool  show_debug_overlay = false;
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

        if (settings.show_objects) {
            for (const auto& obj : state.objects) {
                int ti = static_cast<int>(obj.type);
                if (ti < 0 || ti >= static_cast<int>(EDbdObjectType::OBJ_COUNT))
                    continue;
                if (!settings.obj_show[ti])
                    continue;
                if (obj.distance > settings.obj_max_dist && obj.distance > 0)
                    continue;
                if (!dbd_w2s::IsInFront(obj.position, axes))
                    continue;

                auto sp = dbd_w2s::Project(obj.position, axes, ss);
                if (!dbd_w2s::IsOnScreen(sp, ss))
                    continue;

                ImU32 col = DbdObjectColor(obj.type);
                const char* name = DbdObjectTypeName(obj.type);

                char label[64];
                snprintf(label, sizeof(label), "%s [%dm]", name, static_cast<int>(obj.distance));

                auto ts = ImGui::CalcTextSize(label);
                float lx = sp.X - ts.x * 0.5f;
                float ly = sp.Y - ts.y - 3.0f;

                dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 180), label);
                dl->AddText(ImVec2(lx, ly), col, label);

                dl->AddCircleFilled(ImVec2(sp.X, sp.Y), 2.5f, col);
            }
        }

        if (settings.show_debug_overlay) {
            float x = 10.0f, y = 10.0f;
            float line_h = 16.0f;
            ImU32 bg   = IM_COL32(0, 0, 0, 160);
            ImU32 text = IM_COL32(0, 255, 100, 255);
            ImU32 warn = IM_COL32(255, 200, 50, 255);

            int surv = 0, kill = 0;
            for (auto& p : state.players) {
                if (p.type == EDbdActorType::Survivor) surv++;
                else kill++;
            }

            char lines[8][128];
            int n = 0;
            snprintf(lines[n++], 128, "Base: 0x%lX", state.base_address);
            snprintf(lines[n++], 128, "GWorld: %s", state.valid ? "OK" : "FAIL");
            snprintf(lines[n++], 128, "Camera: %s (FOV=%.0f)", state.has_camera ? "OK" : "NO", state.camera.FOV);
            snprintf(lines[n++], 128, "Players: %d (S:%d K:%d)", state.player_count, surv, kill);
            snprintf(lines[n++], 128, "Objects: %zu", state.objects.size());
            snprintf(lines[n++], 128, "Pos: (%.0f, %.0f, %.0f)",
                     state.camera.Location.X, state.camera.Location.Y, state.camera.Location.Z);

            float max_w = 0;
            for (int i = 0; i < n; i++) {
                auto ts = ImGui::CalcTextSize(lines[i]);
                if (ts.x > max_w) max_w = ts.x;
            }
            dl->AddRectFilled(ImVec2(x - 4, y - 2),
                              ImVec2(x + max_w + 8, y + n * line_h + 4),
                              bg, 4.0f);

            for (int i = 0; i < n; i++) {
                ImU32 c = text;
                if (i == 1 && !state.valid) c = warn;
                if (i == 2 && !state.has_camera) c = warn;
                if (i == 4 && state.objects.empty()) c = warn;
                dl->AddText(ImVec2(x, y + i * line_h), c, lines[i]);
            }
        }
    }

    void render_controls()
    {
        ImGui::Checkbox("Show Name", &settings.show_name);
        ImGui::Checkbox("Show Distance", &settings.show_distance);
        ImGui::SliderFloat("Max Distance (m)", &settings.max_distance, 50.0f, 500.0f, "%.0f");

        ImGui::Separator();
        ImGui::Checkbox("Show Objects", &settings.show_objects);
        if (settings.show_objects) {
            ImGui::SliderFloat("Object Max Dist", &settings.obj_max_dist, 20.0f, 300.0f, "%.0f");
            ImGui::Indent(10.0f);
            for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); ++i) {
                ImU32 col = DbdObjectColor(static_cast<EDbdObjectType>(i));
                ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
                ImGui::PushStyleColor(ImGuiCol_CheckMark, cv);
                ImGui::Checkbox(DbdObjectTypeName(static_cast<EDbdObjectType>(i)), &settings.obj_show[i]);
                ImGui::PopStyleColor();
            }
            ImGui::Unindent(10.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Debug On Screen", &settings.show_debug_overlay);
    }
};

#endif
