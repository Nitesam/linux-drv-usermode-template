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

inline DbdUEVector DbdTransformPoint(const DbdFTransform& t, const DbdUEVector& local) {
    double x = local.X * ((std::isfinite(t.ScaleX) && std::abs(t.ScaleX) > 0.001) ? t.ScaleX : 1.0);
    double y = local.Y * ((std::isfinite(t.ScaleY) && std::abs(t.ScaleY) > 0.001) ? t.ScaleY : 1.0);
    double z = local.Z * ((std::isfinite(t.ScaleZ) && std::abs(t.ScaleZ) > 0.001) ? t.ScaleZ : 1.0);

    double qx = t.RotX, qy = t.RotY, qz = t.RotZ, qw = t.RotW;
    double len2 = qx*qx + qy*qy + qz*qz + qw*qw;
    if (!std::isfinite(len2) || len2 < 0.5 || len2 > 2.0) {
        qx = qy = qz = 0.0;
        qw = 1.0;
    } else {
        double inv_len = 1.0 / std::sqrt(len2);
        qx *= inv_len; qy *= inv_len; qz *= inv_len; qw *= inv_len;
    }

    double t2x = qx * 2.0, t2y = qy * 2.0, t2z = qz * 2.0;
    double wt2x = qw * t2x, wt2y = qw * t2y, wt2z = qw * t2z;
    double xt2x = qx * t2x, xt2y = qx * t2y, xt2z = qx * t2z;
    double yt2y = qy * t2y, yt2z = qy * t2z, zt2z = qz * t2z;

    return {
        t.PosX + (1.0 - (yt2y + zt2z)) * x + (xt2y - wt2z) * y + (xt2z + wt2y) * z,
        t.PosY + (xt2y + wt2z) * x + (1.0 - (xt2x + zt2z)) * y + (yt2z - wt2x) * z,
        t.PosZ + (xt2z - wt2y) * x + (yt2z + wt2x) * y + (1.0 - (xt2x + yt2y)) * z,
    };
}

inline void DbdDrawObb(ImDrawList* dl, const DbdObjectData& obj,
                       const dbd_w2s::CameraAxes& axes,
                       const dbd_w2s::ScreenSize& ss,
                       float y_off, ImU32 col) {
    if (!obj.has_obb)
        return;

    const DbdUEVector& e = obj.obb_extent;
    if (!DbdIsFiniteVec(e) || e.X <= 0.0 || e.Y <= 0.0 || e.Z <= 0.0)
        return;

    const DbdUEVector local[8] = {
        {-e.X, -e.Y, -e.Z}, { e.X, -e.Y, -e.Z}, { e.X,  e.Y, -e.Z}, {-e.X,  e.Y, -e.Z},
        {-e.X, -e.Y,  e.Z}, { e.X, -e.Y,  e.Z}, { e.X,  e.Y,  e.Z}, {-e.X,  e.Y,  e.Z},
    };
    DbdUEVector world[8]{};
    dbd_w2s::ScreenPoint screen[8]{};
    bool front[8]{};
    bool any_visible = false;

    for (int i = 0; i < 8; ++i) {
        world[i] = DbdTransformPoint(obj.obb_transform, local[i]);
        if (!DbdIsFiniteVec(world[i]))
            return;
        front[i] = dbd_w2s::IsInFront(world[i], axes);
        screen[i] = dbd_w2s::Project(world[i], axes, ss);
        screen[i].Y += y_off;
        any_visible = any_visible || (front[i] && dbd_w2s::IsOnScreen(screen[i], ss));
    }
    if (!any_visible)
        return;

    static const int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7},
    };
    ImU32 shadow = IM_COL32(0, 0, 0, 180);
    for (const auto& edge : edges) {
        int a = edge[0], b = edge[1];
        if (!front[a] || !front[b])
            continue;
        ImVec2 pa(screen[a].X, screen[a].Y);
        ImVec2 pb(screen[b].X, screen[b].Y);
        dl->AddLine(ImVec2(pa.x + 1.0f, pa.y + 1.0f), ImVec2(pb.x + 1.0f, pb.y + 1.0f), shadow, 2.5f);
        dl->AddLine(pa, pb, col, 1.5f);
    }
}

inline void DbdDrawRaisedText(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 col, float font_size) {
    ImFont* font = ImGui::GetFont();
    ImU32 outline = IM_COL32(0, 0, 0, 235);

    dl->AddText(font, font_size, ImVec2(pos.x - 1.0f, pos.y), outline, text);
    dl->AddText(font, font_size, ImVec2(pos.x + 1.0f, pos.y), outline, text);
    dl->AddText(font, font_size, ImVec2(pos.x, pos.y - 1.0f), outline, text);
    dl->AddText(font, font_size, ImVec2(pos.x, pos.y + 1.0f), outline, text);
    dl->AddText(font, font_size, pos, col, text);
}

inline void DbdDrawGeneratorOverlay(ImDrawList* dl, const DbdObjectData& obj,
                                    const dbd_w2s::CameraAxes& axes,
                                    const dbd_w2s::ScreenSize& ss,
                                    float y_off, const char* percent_text,
                                    const char* status_text, float pct,
                                    ImU32 col) {
    DbdUEVector center = obj.position;
    if (obj.has_obb)
        center = {obj.obb_transform.PosX, obj.obb_transform.PosY, obj.obb_transform.PosZ};
    if (!dbd_w2s::IsInFront(center, axes))
        return;

    auto sp = dbd_w2s::Project(center, axes, ss);
    sp.Y += y_off;
    if (!dbd_w2s::IsOnScreen(sp, ss))
        return;

    float font_size = ImGui::GetFontSize();
    float status_font_size = ImGui::GetFontSize() * 0.82f;
    ImFont* font = ImGui::GetFont();
    ImVec2 pct_ts = font->CalcTextSizeA(font_size, 10000.0f, 0.0f, percent_text);
    ImVec2 pct_pos(sp.X - pct_ts.x * 0.5f, sp.Y - pct_ts.y * 0.5f - 5.0f);
    DbdDrawRaisedText(dl, pct_pos, percent_text, col, font_size);

    if (status_text && status_text[0]) {
        ImVec2 st_ts = font->CalcTextSizeA(status_font_size, 10000.0f, 0.0f, status_text);
        ImVec2 st_pos(sp.X - st_ts.x * 0.5f, pct_pos.y - st_ts.y - 2.0f);
        DbdDrawRaisedText(dl, st_pos, status_text, IM_COL32(255, 235, 80, 255), status_font_size);
    }

    float bar_w = std::max(48.0f, pct_ts.x + 18.0f);
    float bar_h = 5.0f;
    float bar_x = sp.X - bar_w * 0.5f;
    float bar_y = pct_pos.y + pct_ts.y + 4.0f;
    float fill = std::clamp(pct / 100.0f, 0.0f, 1.0f);
    uint8_t r = static_cast<uint8_t>(255 * (1.0f - fill));
    uint8_t g = static_cast<uint8_t>(185 + 70 * fill);

    dl->AddRectFilled(ImVec2(bar_x - 2.0f, bar_y - 2.0f),
                      ImVec2(bar_x + bar_w + 2.0f, bar_y + bar_h + 2.0f),
                      IM_COL32(0, 0, 0, 220), 2.0f);
    dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                      IM_COL32(18, 18, 18, 235), 2.0f);
    if (fill > 0.005f) {
        dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w * fill, bar_y + bar_h),
                          IM_COL32(r, g, 0, 245), 2.0f);
        dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w * fill, bar_y + 2.0f),
                          IM_COL32(255, 255, 170, 110), 2.0f);
    }
    dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                IM_COL32(255, 255, 210, 210), 2.0f, 0, 1.5f);
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
        case EDbdObjectType::BloodPump:     return IM_COL32(255,  50, 220, 255);
        default:                            return IM_COL32(200, 200, 200, 255);
    }
}

struct DbdEspSettings {
    bool  show_name     = true;
    bool  show_distance = true;
    bool  show_boxes    = true;
    bool  show_skeleton  = true;
    float max_distance  = 200.0f;

    bool  show_objects   = true;
    bool  hide_dull_totems = false;
    float obj_dist[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        200.0f,  // Generator
        100.0f,  // Totem
        60.0f,   // Pallet
        100.0f,  // Hook
        200.0f,  // Hatch
        60.0f,   // Locker
        80.0f,   // Chest
        60.0f,   // Window
        100.0f,  // Trap
        200.0f,  // EscapeDoor
        80.0f,   // BreakableDoor
        200.0f,  // BloodPump
    };
    bool  obj_show[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        true,  true,  true,  true,  true,
        true,  true,  true,  true,  true, true, true
    };
    bool  obj_box_name[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        true,  true,  true,  true,  true,
        true,  true,  true,  true,  true, true, true
    };

    bool  show_debug_overlay = false;
    float esp_y_offset = 0.0f;
    bool  auto_skillcheck = false;

    bool  aura_enabled = false;
    bool  aura_survivors = true;
    bool  aura_killer = true;
    float aura_surv_color[4] = {0.0f, 1.0f, 0.0f, 0.5f};
    float aura_killer_color[4] = {1.0f, 0.0f, 0.0f, 0.75f};
    bool  aura_obj[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        true, true, true, true, true, false, true, true, true, true, false, true
    };
    float aura_obj_color[static_cast<int>(EDbdObjectType::OBJ_COUNT)][4] = {
        {0.13f, 0.83f, 0.69f, 0.5f},
        {0.09f, 0.12f, 1.0f, 0.25f},
        {0.86f, 0.86f, 0.0f, 0.35f},
        {0.31f, 0.50f, 0.88f, 0.5f},
        {0.58f, 0.0f, 0.83f, 0.5f},
        {0.5f, 0.5f, 0.5f, 0.5f},
        {0.85f, 0.65f, 0.13f, 0.5f},
        {0.95f, 0.50f, 0.0f, 0.35f},
        {0.86f, 0.08f, 0.24f, 0.5f},
        {0.2f, 0.8f, 0.2f, 0.5f},
        {0.82f, 0.41f, 0.12f, 0.5f},
        {0.8f, 0.1f, 0.1f, 0.6f},
    };

    DbdAuraConfig get_aura_config() const {
        DbdAuraConfig cfg;
        cfg.enabled = show_debug_overlay && aura_enabled;
        cfg.survivor_aura = aura_survivors;
        cfg.killer_aura = aura_killer;
        cfg.survivor_color = {aura_surv_color[0], aura_surv_color[1], aura_surv_color[2], aura_surv_color[3]};
        cfg.killer_color = {aura_killer_color[0], aura_killer_color[1], aura_killer_color[2], aura_killer_color[3]};
        for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); i++) {
            cfg.obj_aura[i] = aura_obj[i];
            cfg.obj_color[i] = {aura_obj_color[i][0], aura_obj_color[i][1], aura_obj_color[i][2], aura_obj_color[i][3]};
        }
        return cfg;
    }

    DbdSkillCheckConfig get_skillcheck_config() const {
        DbdSkillCheckConfig cfg;
        cfg.enabled = auto_skillcheck;
        return cfg;
    }

    std::string to_json() const {
        char buf[8192];
        int n = snprintf(buf, sizeof(buf),
            "{\n"
            "  \"show_name\": %s,\n"
            "  \"show_distance\": %s,\n"
            "  \"show_boxes\": %s,\n"
            "  \"show_skeleton\": %s,\n"
            "  \"max_distance\": %.1f,\n"
            "  \"show_objects\": %s,\n"
            "  \"show_debug_overlay\": %s,\n",
            show_name ? "true" : "false",
            show_distance ? "true" : "false",
            show_boxes ? "true" : "false",
            show_skeleton ? "true" : "false",
            max_distance,
            show_objects ? "true" : "false",
            show_debug_overlay ? "true" : "false");
        n += snprintf(buf + n, sizeof(buf) - n, "  \"hide_dull_totems\": %s,\n", hide_dull_totems ? "true" : "false");
        n += snprintf(buf + n, sizeof(buf) - n, "  \"esp_y_offset\": %.1f,\n", esp_y_offset);
        n += snprintf(buf + n, sizeof(buf) - n, "  \"auto_skillcheck\": %s,\n", auto_skillcheck ? "true" : "false");
        n += snprintf(buf + n, sizeof(buf) - n, "  \"aura_enabled\": %s,\n", aura_enabled ? "true" : "false");
        n += snprintf(buf + n, sizeof(buf) - n, "  \"aura_survivors\": %s,\n", aura_survivors ? "true" : "false");
        n += snprintf(buf + n, sizeof(buf) - n, "  \"aura_killer\": %s,\n", aura_killer ? "true" : "false");
        n += snprintf(buf + n, sizeof(buf) - n, "  \"aura_surv_color\": [%.3f,%.3f,%.3f,%.3f],\n",
                     aura_surv_color[0], aura_surv_color[1], aura_surv_color[2], aura_surv_color[3]);
        n += snprintf(buf + n, sizeof(buf) - n, "  \"aura_killer_color\": [%.3f,%.3f,%.3f,%.3f],\n",
                     aura_killer_color[0], aura_killer_color[1], aura_killer_color[2], aura_killer_color[3]);
        for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); i++) {
            n += snprintf(buf + n, sizeof(buf) - n, "  \"aura_obj_%d\": %s,\n", i, aura_obj[i] ? "true" : "false");
            n += snprintf(buf + n, sizeof(buf) - n, "  \"aura_obj_color_%d\": [%.3f,%.3f,%.3f,%.3f],\n",
                         i, aura_obj_color[i][0], aura_obj_color[i][1], aura_obj_color[i][2], aura_obj_color[i][3]);
        }
        for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); i++) {
            char key[64];
            snprintf(key, sizeof(key), "  \"obj_show_%d\": %s,\n", i, obj_show[i] ? "true" : "false");
            n += snprintf(buf + n, sizeof(buf) - n, "%s", key);
            snprintf(key, sizeof(key), "  \"obj_dist_%d\": %.1f,\n", i, obj_dist[i]);
            n += snprintf(buf + n, sizeof(buf) - n, "%s", key);
        }
        for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); i++) {
            char key[64];
            snprintf(key, sizeof(key), "  \"obj_box_name_%d\": %s", i, obj_box_name[i] ? "true" : "false");
            n += snprintf(buf + n, sizeof(buf) - n, "%s%s\n",
                key, (i < static_cast<int>(EDbdObjectType::OBJ_COUNT) - 1) ? "," : "");
        }
        n += snprintf(buf + n, sizeof(buf) - n, "}\n");
        return std::string(buf, n);
    }

    void from_json(const std::string& s) {
        if (s.empty()) return;
        auto gb = [&](const char* k, bool d) {
            auto p = s.find(std::string("\"") + k + "\"");
            if (p == std::string::npos) return d;
            p = s.find(':', p); if (p == std::string::npos) return d;
            auto r = s.substr(p+1, 10);
            if (r.find("true") != std::string::npos) return true;
            if (r.find("false") != std::string::npos) return false;
            return d;
        };
        auto gf = [&](const char* k, float d) {
            auto p = s.find(std::string("\"") + k + "\"");
            if (p == std::string::npos) return d;
            p = s.find(':', p); if (p == std::string::npos) return d;
            return static_cast<float>(atof(s.c_str() + p + 1));
        };
        show_name = gb("show_name", show_name);
        show_distance = gb("show_distance", show_distance);
        show_boxes = gb("show_boxes", show_boxes);
        show_skeleton = gb("show_skeleton", show_skeleton);
        max_distance = gf("max_distance", max_distance);
        show_objects = gb("show_objects", show_objects);
        show_debug_overlay = false;
        esp_y_offset = gf("esp_y_offset", esp_y_offset);
        hide_dull_totems = gb("hide_dull_totems", hide_dull_totems);
        auto_skillcheck = gb("auto_skillcheck", auto_skillcheck);
        aura_enabled = false;
        aura_survivors = gb("aura_survivors", aura_survivors);
        aura_killer = gb("aura_killer", aura_killer);
        auto ga4 = [&](const char* k, float* arr) {
            auto p = s.find(std::string("\"") + k + "\"");
            if (p == std::string::npos) return;
            p = s.find('[', p); if (p == std::string::npos) return;
            int ci = 0;
            size_t pos = p + 1;
            while (ci < 4 && pos < s.size()) {
                arr[ci++] = static_cast<float>(atof(s.c_str() + pos));
                auto next = s.find(',', pos);
                auto end = s.find(']', pos);
                if (next == std::string::npos || (end != std::string::npos && end < next)) break;
                pos = next + 1;
            }
        };
        ga4("aura_surv_color", aura_surv_color);
        ga4("aura_killer_color", aura_killer_color);
        for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); i++) {
            char key[32];
            snprintf(key, sizeof(key), "aura_obj_%d", i);
            aura_obj[i] = gb(key, aura_obj[i]);
            snprintf(key, sizeof(key), "aura_obj_color_%d", i);
            ga4(key, aura_obj_color[i]);
        }
        for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); i++) {
            char key[32];
            snprintf(key, sizeof(key), "obj_show_%d", i);
            obj_show[i] = gb(key, obj_show[i]);
            snprintf(key, sizeof(key), "obj_dist_%d", i);
            obj_dist[i] = gf(key, obj_dist[i]);
            snprintf(key, sizeof(key), "obj_box_name_%d", i);
            obj_box_name[i] = gb(key, obj_box_name[i]);
        }
    }
};

class DbdEspRenderer {
public:
    DbdEspSettings settings;

    void render(ImDrawList* dl, const DbdWorldState& state, int sw, int sh)
    {
        if (!state.valid || !state.has_camera)
            return;

        dl->PushClipRect(ImVec2(0, 0), ImVec2((float)sw, (float)sh), true);

        const dbd_w2s::ScreenSize ss{sw, sh};
        const auto axes = dbd_w2s::BuildAxes(state.camera);
        const float y_off = settings.esp_y_offset;

        for (const auto& p : state.players) {
            if (!p.valid)
                continue;
            if (p.is_local)
                continue;
            if (p.type == EDbdActorType::Survivor && p.health_states < 0)
                continue;
            if (p.position.X == 0.0 && p.position.Y == 0.0 && p.position.Z == 0.0)
                continue;
            if (p.distance > settings.max_distance && p.distance > 0)
                continue;
            if (!dbd_w2s::IsInFront(p.position, axes))
                continue;

            float half_h = (p.type == EDbdActorType::Killer) ? 96.0f : 88.0f;

            DbdUEVector head_pos = p.position;
            head_pos.Z += half_h;
            DbdUEVector feet_pos = p.position;
            feet_pos.Z -= half_h;

            auto sp_head = dbd_w2s::Project(head_pos, axes, ss);
            auto sp_feet = dbd_w2s::Project(feet_pos, axes, ss);
            sp_head.Y += y_off; sp_feet.Y += y_off;

            if (!dbd_w2s::IsOnScreen(sp_head, ss) && !dbd_w2s::IsOnScreen(sp_feet, ss))
                continue;

            ImU32 col;
            if (p.type == EDbdActorType::Survivor) {
                if (p.health_states == 0)      col = IM_COL32(255, 50, 50, 255);
                else if (p.health_states == 1)  col = IM_COL32(255, 200, 50, 255);
                else                           col = IM_COL32(0, 220, 50, 255);
            } else {
                col = IM_COL32(255, 50, 50, 255);
            }

            if (settings.show_boxes) {
                float box_h = std::abs(sp_feet.Y - sp_head.Y);
                float box_w = box_h * ((p.type == EDbdActorType::Killer) ? 0.45f : 0.38f);
                if (box_h > 4.0f) {
                    float cx = (sp_head.X + sp_feet.X) * 0.5f;
                    float top = std::min(sp_head.Y, sp_feet.Y);
                    float bot = std::max(sp_head.Y, sp_feet.Y);

                    dl->AddRect(ImVec2(cx - box_w * 0.5f, top),
                                ImVec2(cx + box_w * 0.5f, bot),
                                col, 0, 0, 1.5f);

                    if (p.type == EDbdActorType::Survivor && p.health_states >= 0) {
                        float hw = box_w * 0.06f;
                        float fill = (p.health_states == 2) ? 1.0f : (p.health_states == 1) ? 0.5f : 0.15f;
                        float bar_x = cx - box_w * 0.5f - hw - 2;
                        dl->AddRectFilled(ImVec2(bar_x, top), ImVec2(bar_x + hw, bot),
                                          IM_COL32(30, 30, 30, 180));
                        dl->AddRectFilled(ImVec2(bar_x, bot - (bot - top) * fill),
                                          ImVec2(bar_x + hw, bot), col);
                    }
                }
            }

            char label[128];
            int ln = 0;
            if (settings.show_name) {
                if (p.name[0])
                    ln += snprintf(label + ln, sizeof(label) - ln, "%s", p.name);
                else if (p.character_name[0])
                    ln += snprintf(label + ln, sizeof(label) - ln, "%s", p.character_name);
            }
            if (p.prestige > 0)
                ln += snprintf(label + ln, sizeof(label) - ln, " P%d", p.prestige);
            if (p.level >= 0)
                ln += snprintf(label + ln, sizeof(label) - ln, " Lv%d", p.level);
            if (settings.show_distance && p.distance > 0)
                ln += snprintf(label + ln, sizeof(label) - ln, " [%dm]", static_cast<int>(p.distance));

            if (ln > 0) {
                float label_y = std::min(sp_head.Y, sp_feet.Y);
                auto ts = ImGui::CalcTextSize(label);
                float lx = sp_head.X - ts.x * 0.5f;
                float ly = label_y - ts.y - 4.0f;

                dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 200), label);
                dl->AddText(ImVec2(lx, ly), col, label);
            }

            dl->AddCircleFilled(ImVec2(sp_head.X, sp_head.Y), 3.0f, col);

            if (settings.show_skeleton && p.bones_mapped && p.bone_count > 10) {
                static const int bone_connections[][2] = {
                    {BONE_HEAD, BONE_NECK}, {BONE_NECK, BONE_TORSO}, {BONE_TORSO, BONE_PELVIS},
                    {BONE_TORSO, BONE_SHOULDER_L}, {BONE_SHOULDER_L, BONE_ELBOW_L}, {BONE_ELBOW_L, BONE_HAND_L},
                    {BONE_TORSO, BONE_SHOULDER_R}, {BONE_SHOULDER_R, BONE_ELBOW_R}, {BONE_ELBOW_R, BONE_HAND_R},
                    {BONE_PELVIS, BONE_HIP_L}, {BONE_HIP_L, BONE_KNEE_L}, {BONE_KNEE_L, BONE_FOOT_L},
                    {BONE_PELVIS, BONE_HIP_R}, {BONE_HIP_R, BONE_KNEE_R}, {BONE_KNEE_R, BONE_FOOT_R},
                };
                int num_conns = sizeof(bone_connections) / sizeof(bone_connections[0]);
                int bc = (int)p.bone_count;

                ImU32 bone_col = (p.type == EDbdActorType::Survivor)
                    ? IM_COL32(100, 255, 100, 200)
                    : IM_COL32(255, 100, 100, 200);

                for (int ci = 0; ci < num_conns; ci++) {
                    int idx_a = p.bone_map[bone_connections[ci][0]];
                    int idx_b = p.bone_map[bone_connections[ci][1]];
                    if (idx_a < 0 || idx_b < 0 || idx_a >= bc || idx_b >= bc)
                        continue;

                    auto& ba = p.bone_positions[idx_a];
                    auto& bb = p.bone_positions[idx_b];
                    if (!dbd_w2s::IsInFront(ba, axes) || !dbd_w2s::IsInFront(bb, axes))
                        continue;

                    auto sa = dbd_w2s::Project(ba, axes, ss);
                    auto sb = dbd_w2s::Project(bb, axes, ss);
                    sa.Y += y_off; sb.Y += y_off;
                    if (!dbd_w2s::IsOnScreen(sa, ss) && !dbd_w2s::IsOnScreen(sb, ss))
                        continue;

                    dl->AddLine(ImVec2(sa.X, sa.Y), ImVec2(sb.X, sb.Y), bone_col, 1.5f);
                }
            }
        }

        if (settings.show_objects) {
            for (const auto& obj : state.objects) {
                int ti = static_cast<int>(obj.type);
                if (ti < 0 || ti >= static_cast<int>(EDbdObjectType::OBJ_COUNT))
                    continue;
                if (!settings.obj_show[ti])
                    continue;
                if (obj.type == EDbdObjectType::Pallet && obj.pallet_state >= 3)
                    continue;
                if (obj.type == EDbdObjectType::Totem && settings.hide_dull_totems && obj.totem_state != 2 && obj.totem_state != 3)
                    continue;
                if (obj.distance > settings.obj_dist[ti] && obj.distance > 0)
                    continue;
                if (!dbd_w2s::IsInFront(obj.position, axes))
                    continue;

                auto sp = dbd_w2s::Project(obj.position, axes, ss);
                sp.Y += y_off;
                if (!dbd_w2s::IsOnScreen(sp, ss))
                    continue;

                ImU32 col = DbdObjectColor(obj.type);
                const char* base_name = DbdObjectTypeName(obj.type);

                char label[128];
                char gen_percent_label[32]{};
                char gen_status_label[32]{};
                bool custom_label_drawn = false;
                float gen_pct = 0.0f;
                switch (obj.type) {
                    case EDbdObjectType::Generator: {
                        gen_pct = (obj.gen_progress >= 0 && obj.gen_max_charge > 0)
                            ? (obj.gen_progress / obj.gen_max_charge) * 100.0f : 0;
                        if (gen_pct > 100) gen_pct = 100;
                        if (gen_pct >= 99.5f)
                            continue;
                        snprintf(gen_percent_label, sizeof(gen_percent_label), "%.0f%%", gen_pct);
                        if (obj.gen_blocked)
                            snprintf(gen_status_label, sizeof(gen_status_label), "BLOCKED");
                        snprintf(label, sizeof(label), "Gen %.0f%% [%dm]", gen_pct, (int)obj.distance);
                        break;
                    }
                    case EDbdObjectType::Pallet: {
                        const char* ps = (obj.pallet_state == 0) ? "UP" :
                                         (obj.pallet_state == 2) ? "DOWN" :
                                         (obj.pallet_state >= 3) ? "GONE" : "";
                        if (obj.pallet_state >= 3)
                            col = IM_COL32(80, 80, 80, 150);
                        else if (obj.pallet_state == 2)
                            col = IM_COL32(255, 200, 50, 255);
                        snprintf(label, sizeof(label), "Pallet %s [%dm]", ps, (int)obj.distance);
                        break;
                    }
                    case EDbdObjectType::Totem: {
                        const char* ts = (obj.totem_state == 1) ? "Dull" :
                                         (obj.totem_state == 2) ? "HEX" :
                                         (obj.totem_state == 3) ? "Boon" : "";
                        if (obj.totem_state == 2)
                            col = IM_COL32(255, 50, 50, 255);
                        else if (obj.totem_state == 3)
                            col = IM_COL32(100, 180, 255, 255);
                        snprintf(label, sizeof(label), "Totem %s [%dm]", ts, (int)obj.distance);
                        break;
                    }
                    case EDbdObjectType::Hatch: {
                        const char* hs = (obj.hatch_state == 1) ? "OPEN" :
                                         (obj.hatch_state == 2) ? "CLOSED" : "";
                        if (obj.hatch_state == 1)
                            col = IM_COL32(50, 255, 50, 255);
                        snprintf(label, sizeof(label), "Hatch %s [%dm]", hs, (int)obj.distance);
                        break;
                    }
                    case EDbdObjectType::Hook: {
                        if (obj.hook_occupied) {
                            snprintf(label, sizeof(label), "Hook [OCCUPIED]%s [%dm]",
                                     obj.hook_basement ? " BM" : "", (int)obj.distance);
                            col = IM_COL32(255, 50, 50, 255);
                        } else {
                            snprintf(label, sizeof(label), "Hook%s [%dm]",
                                     obj.hook_basement ? " BM" : "", (int)obj.distance);
                        }
                        break;
                    }
                    case EDbdObjectType::Chest: {
                        if (obj.chest_opened) {
                            snprintf(label, sizeof(label), "Chest [Opened] [%dm]", (int)obj.distance);
                            col = IM_COL32(100, 100, 100, 150);
                        } else {
                            snprintf(label, sizeof(label), "Chest [%dm]", (int)obj.distance);
                        }
                        break;
                    }
                    case EDbdObjectType::EscapeDoor: {
                        snprintf(label, sizeof(label), "Exit %s [%dm]",
                                 obj.escape_activated ? "OPEN" : "CLOSED", (int)obj.distance);
                        if (obj.escape_activated)
                            col = IM_COL32(50, 255, 50, 255);
                        break;
                    }
                    default:
                        snprintf(label, sizeof(label), "%s [%dm]", base_name, (int)obj.distance);
                        break;
                }

                if (settings.show_boxes && DbdObjectSupportsBox(obj.type))
                    DbdDrawObb(dl, obj, axes, ss, y_off, col);

                const bool draw_label = !DbdObjectSupportsBox(obj.type) || settings.obj_box_name[ti];
                if (draw_label && obj.type == EDbdObjectType::Generator) {
                    DbdDrawGeneratorOverlay(dl, obj, axes, ss, y_off,
                                            gen_percent_label, gen_status_label,
                                            gen_pct, col);
                    custom_label_drawn = true;
                }
                if (draw_label) {
                    if (custom_label_drawn) {
                        dl->AddCircleFilled(ImVec2(sp.X, sp.Y), 2.5f, col);
                        continue;
                    }
                    auto ts = ImGui::CalcTextSize(label);
                    float lx = sp.X - ts.x * 0.5f;
                    float ly_off = (obj.type == EDbdObjectType::Generator) ? -38.0f : -3.0f;
                    float ly = sp.Y + ly_off - ts.y;

                    dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 180), label);
                    dl->AddText(ImVec2(lx, ly), col, label);
                }

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

        dl->PopClipRect();
    }

    void render_lobby_panel(ImDrawList* dl, const DbdWorldState& state, int sw, int sh)
    {
        if (!state.valid || state.players.empty())
            return;

        float panel_x = sw * 0.5f - 220.0f;
        float panel_y = sh * 0.15f;
        float line_h = 20.0f;
        float pad = 12.0f;

        int surv_count = 0, kill_count = 0;
        for (auto& p : state.players) {
            if (p.type == EDbdActorType::Survivor) surv_count++;
            else if (p.type == EDbdActorType::Killer) kill_count++;
        }

        int total_lines = 2 + (int)state.players.size() * 3 + 1;
        float panel_h = total_lines * line_h + pad * 2;
        float panel_w = 440.0f;

        dl->AddRectFilled(ImVec2(panel_x, panel_y),
                          ImVec2(panel_x + panel_w, panel_y + panel_h),
                          IM_COL32(15, 15, 20, 220), 8.0f);
        dl->AddRect(ImVec2(panel_x, panel_y),
                    ImVec2(panel_x + panel_w, panel_y + panel_h),
                    IM_COL32(80, 80, 120, 200), 8.0f, 0, 1.5f);

        float cx = panel_x + pad;
        float cy = panel_y + pad;

        dl->AddText(ImVec2(cx, cy), IM_COL32(220, 180, 50, 255), "LOBBY INFO");
        cy += line_h;

        char summary[64];
        snprintf(summary, sizeof(summary), "Players: %d  (S:%d  K:%d)",
                 (int)state.players.size(), surv_count, kill_count);
        dl->AddText(ImVec2(cx, cy), IM_COL32(180, 180, 180, 200), summary);
        cy += line_h * 1.2f;

        for (const auto& p : state.players) {
            ImU32 role_col;
            const char* role_tag;
            if (p.type == EDbdActorType::Survivor) {
                role_col = IM_COL32(50, 220, 80, 255);
                role_tag = "[SURV]";
            } else if (p.type == EDbdActorType::Killer) {
                role_col = IM_COL32(255, 60, 60, 255);
                role_tag = "[KILL]";
            } else {
                role_col = IM_COL32(180, 180, 180, 200);
                role_tag = "[???]";
            }

            char line1[128];
            if (p.character_name[0]) {
                snprintf(line1, sizeof(line1), "%s %s  (%s)",
                         role_tag, p.character_name, p.name[0] ? p.name : "?");
            } else {
                snprintf(line1, sizeof(line1), "%s %s",
                         role_tag, p.name[0] ? p.name : "Unknown");
            }
            dl->AddText(ImVec2(cx, cy), role_col, line1);
            cy += line_h;

            char line2[128];
            if (p.prestige > 0 && p.level >= 0)
                snprintf(line2, sizeof(line2), "   P%d  Lv%d", p.prestige, p.level);
            else if (p.level >= 0)
                snprintf(line2, sizeof(line2), "   Lv%d", p.level);
            else
                snprintf(line2, sizeof(line2), "   --");
            dl->AddText(ImVec2(cx, cy), IM_COL32(160, 160, 180, 200), line2);
            cy += line_h;

            if (p.perks_valid) {
                char perk_line[256];
                int n = snprintf(perk_line, sizeof(perk_line), "   Perks:");
                for (int pi = 0; pi < DBD_MAX_PERKS; pi++) {
                    if (p.perk_names[pi][0])
                        n += snprintf(perk_line + n, sizeof(perk_line) - n, " %s", p.perk_names[pi]);
                    else if (p.perk_ids[pi] > 0)
                        n += snprintf(perk_line + n, sizeof(perk_line) - n, " #%d", p.perk_ids[pi]);
                    if (pi < 3) n += snprintf(perk_line + n, sizeof(perk_line) - n, ",");
                }
                dl->AddText(ImVec2(cx, cy), IM_COL32(130, 150, 200, 200), perk_line);
            } else {
                dl->AddText(ImVec2(cx, cy), IM_COL32(100, 100, 100, 150), "   Perks: --");
            }
            cy += line_h * 1.1f;
        }
    }

    void render_controls()
    {
        ImGui::Checkbox("Show Name", &settings.show_name);
        ImGui::Checkbox("Show Distance", &settings.show_distance);
        ImGui::Checkbox("Show Boxes", &settings.show_boxes);
        ImGui::Checkbox("Show Skeleton", &settings.show_skeleton);
        ImGui::SliderFloat("Max Distance (m)", &settings.max_distance, 50.0f, 500.0f, "%.0f");
        ImGui::SliderFloat("Y Offset", &settings.esp_y_offset, -50.0f, 50.0f, "%.0f px");

        ImGui::Separator();
        ImGui::Checkbox("Show Objects", &settings.show_objects);
        if (settings.show_objects) {
            ImGui::Indent(10.0f);
            for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); ++i) {
                ImU32 col = DbdObjectColor(static_cast<EDbdObjectType>(i));
                ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
                ImGui::PushStyleColor(ImGuiCol_CheckMark, cv);
                ImGui::Checkbox(DbdObjectTypeName(static_cast<EDbdObjectType>(i)), &settings.obj_show[i]);
                ImGui::PopStyleColor();
                if (settings.obj_show[i]) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    char sid[32];
                    snprintf(sid, sizeof(sid), "##od%d", i);
                    ImGui::SliderFloat(sid, &settings.obj_dist[i], 10.0f, 300.0f, "%.0fm");
                    if (DbdObjectSupportsBox(static_cast<EDbdObjectType>(i))) {
                        ImGui::SameLine();
                        char nid[32];
                        snprintf(nid, sizeof(nid), "Name##obn%d", i);
                        ImGui::Checkbox(nid, &settings.obj_box_name[i]);
                    }
                }
            }
            ImGui::Checkbox("Hide Dull Totems", &settings.hide_dull_totems);
            ImGui::Unindent(10.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Debug On Screen", &settings.show_debug_overlay);
        if (settings.show_debug_overlay) {
            ImGui::Indent(10.0f);

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
            ImGui::Checkbox("Aura [Memory Write]", &settings.aura_enabled);
            ImGui::PopStyleColor();
            if (settings.aura_enabled) {
                ImGui::Indent(10.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 0.8f),
                    "Writes to game memory");

                ImGui::SeparatorText("Players");
                ImGui::Checkbox("Survivor Aura", &settings.aura_survivors);
                if (settings.aura_survivors) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##surv_aura_col", settings.aura_surv_color,
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                }
                ImGui::Checkbox("Killer Aura", &settings.aura_killer);
                if (settings.aura_killer) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##kill_aura_col", settings.aura_killer_color,
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                }

                ImGui::SeparatorText("Objects");
                for (int i = 0; i < static_cast<int>(EDbdObjectType::OBJ_COUNT); ++i) {
                    char cbid[32], label[64];
                    snprintf(cbid, sizeof(cbid), "##auraobj%d", i);
                    snprintf(label, sizeof(label), "%s##aura_%d", DbdObjectTypeName(static_cast<EDbdObjectType>(i)), i);
                    ImGui::Checkbox(label, &settings.aura_obj[i]);
                    if (settings.aura_obj[i]) {
                        ImGui::SameLine();
                        ImGui::ColorEdit4(cbid, settings.aura_obj_color[i],
                            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                    }
                }
                ImGui::Unindent(10.0f);
            }

            ImGui::Unindent(10.0f);
        } else {
            settings.aura_enabled = false;
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.5f, 1.0f));
        ImGui::Checkbox("Auto Great Skill Check", &settings.auto_skillcheck);
        ImGui::PopStyleColor();
        if (settings.auto_skillcheck) {
            ImGui::Indent(10.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 0.8f),
                "Simulates spacebar when needle is in great zone");
            ImGui::Unindent(10.0f);
        }
    }
};

#endif
