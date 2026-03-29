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
    bool  show_boxes    = true;
    bool  show_skeleton  = true;
    float max_distance  = 200.0f;

    bool  show_objects   = true;
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
    };
    bool  obj_show[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        true,  true,  true,  true,  true,
        true,  true,  true,  true,  true, true
    };

    bool  show_debug_overlay = false;
    float esp_y_offset = 0.0f;

    bool  aura_enabled = false;
    bool  aura_survivors = true;
    bool  aura_killer = true;
    float aura_surv_color[4] = {0.0f, 1.0f, 0.0f, 0.5f};
    float aura_killer_color[4] = {1.0f, 0.0f, 0.0f, 0.75f};
    bool  aura_obj[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        true, true, true, true, true, false, true, true, true, true, false
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
    };

    DbdAuraConfig get_aura_config() const {
        DbdAuraConfig cfg;
        cfg.enabled = aura_enabled;
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
        n += snprintf(buf + n, sizeof(buf) - n, "  \"esp_y_offset\": %.1f,\n", esp_y_offset);
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
            snprintf(key, sizeof(key), "  \"obj_dist_%d\": %.1f", i, obj_dist[i]);
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
        show_debug_overlay = gb("show_debug_overlay", show_debug_overlay);
        esp_y_offset = gf("esp_y_offset", esp_y_offset);
        aura_enabled = gb("aura_enabled", aura_enabled);
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

            std::string label;
            if (settings.show_name && p.name[0])
                label = p.name;
            if (p.prestige >= 0 || p.level >= 0) {
                char pbuf[32];
                if (p.prestige > 0)
                    snprintf(pbuf, sizeof(pbuf), " P%d", p.prestige);
                else
                    pbuf[0] = 0;
                char lvbuf[32] = {};
                if (p.level >= 0)
                    snprintf(lvbuf, sizeof(lvbuf), " Lv%d", p.level);
                label += pbuf;
                label += lvbuf;
            }
            if (settings.show_distance && p.distance > 0) {
                char dbuf[32];
                snprintf(dbuf, sizeof(dbuf), " [%dm]", static_cast<int>(p.distance));
                label += dbuf;
            }

            if (!label.empty()) {
                float label_y = std::min(sp_head.Y, sp_feet.Y);
                auto ts = ImGui::CalcTextSize(label.c_str());
                float lx = sp_head.X - ts.x * 0.5f;
                float ly = label_y - ts.y - 4.0f;

                dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 200), label.c_str());
                dl->AddText(ImVec2(lx, ly), col, label.c_str());
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
                switch (obj.type) {
                    case EDbdObjectType::Generator: {
                        float pct = (obj.gen_progress >= 0 && obj.gen_max_charge > 0)
                            ? (obj.gen_progress / obj.gen_max_charge) * 100.0f : 0;
                        if (pct > 100) pct = 100;
                        if (obj.gen_blocked)
                            snprintf(label, sizeof(label), "Gen [BLOCKED] [%dm]", (int)obj.distance);
                        else if (pct >= 99.5f) {
                            snprintf(label, sizeof(label), "Gen [DONE] [%dm]", (int)obj.distance);
                            col = IM_COL32(50, 255, 50, 255);
                        } else
                            snprintf(label, sizeof(label), "Gen %.0f%% [%dm]", pct, (int)obj.distance);

                        float bar_w = 40.0f, bar_h = 5.0f;
                        float bar_x = sp.X - bar_w * 0.5f;
                        float bar_y = sp.Y - 28.0f;
                        float fill = pct / 100.0f;
                        uint8_t r = (uint8_t)(255 * (1.0f - fill));
                        uint8_t g = (uint8_t)(200 + 55 * fill);
                        dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                                          IM_COL32(20, 20, 20, 200));
                        if (fill > 0.005f)
                            dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w * fill, bar_y + bar_h),
                                              IM_COL32(r, g, 0, 230));
                        dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                                    IM_COL32(180, 180, 180, 150), 0, 0, 1.0f);
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

                auto ts = ImGui::CalcTextSize(label);
                float lx = sp.X - ts.x * 0.5f;
                float ly_off = (obj.type == EDbdObjectType::Generator) ? -38.0f : -3.0f;
                float ly = sp.Y + ly_off - ts.y;

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

        dl->PopClipRect();
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
                }
            }
            ImGui::Unindent(10.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Debug On Screen", &settings.show_debug_overlay);

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
                char cbid[32];
                snprintf(cbid, sizeof(cbid), "##auraobj%d", i);
                ImGui::Checkbox(DbdObjectTypeName(static_cast<EDbdObjectType>(i)), &settings.aura_obj[i]);
                if (settings.aura_obj[i]) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4(cbid, settings.aura_obj_color[i],
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                }
            }
            ImGui::Unindent(10.0f);
        }
    }
};

#endif
