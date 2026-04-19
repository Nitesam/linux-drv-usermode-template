#ifndef SOT_ESP_H
#define SOT_ESP_H

#include <cstdio>
#include <cmath>

#include "imgui.h"
#include "sot_offsets.h"

class SotEspRenderer {
public:
    SotEspSettings settings;

    void render(ImDrawList* dl, const SotWorldState& state, int sw, int sh) {
        if (!state.valid || !state.has_camera)
            return;

        const auto& cam = state.camera;

        // ── Players ──────────────────────────────────────────────
        if (settings.show_players) {
            for (const auto& p : state.players) {
                if (!p.valid) continue;
                if (p.type == ESotActorType::Skeleton && !settings.show_skeletons) continue;
                if (p.distance > settings.max_distance) continue;

                float sx, sy;
                if (!world_to_screen(cam, p.position, sw, sh, sx, sy))
                    continue;

                bool is_skeleton = (p.type == ESotActorType::Skeleton);
                ImU32 col = p.is_local ? IM_COL32(100, 255, 100, 255)
                          : is_skeleton ? IM_COL32(200, 200, 100, 255)
                          : IM_COL32(255, 80, 80, 255);
                ImU32 shadow = IM_COL32(0, 0, 0, 200);

                char label[128];
                snprintf(label, sizeof(label), "%s [%.0fm]", p.name, p.distance);

                ImVec2 tpos(sx, sy);
                dl->AddText(ImVec2(tpos.x + 1, tpos.y + 1), shadow, label);
                dl->AddText(tpos, col, label);

                // Small marker
                float r = 4.0f;
                if (p.distance < 100.0f) r = 6.0f;
                dl->AddCircleFilled(ImVec2(sx, sy - 12), r, col);
            }
        }

        // ── Ships ────────────────────────────────────────────────
        if (settings.show_ships) {
            for (const auto& s : state.ships) {
                if (!s.valid) continue;
                if (s.distance > settings.max_distance) continue;

                float sx, sy;
                if (!world_to_screen(cam, s.position, sw, sh, sx, sy))
                    continue;

                ImU32 col = IM_COL32(80, 180, 255, 255);
                ImU32 shadow = IM_COL32(0, 0, 0, 200);

                char label[96];
                snprintf(label, sizeof(label), "%s [%.0fm]",
                    SotShipTypeName(s.ship_type), s.distance);

                dl->AddText(ImVec2(sx + 1, sy + 1), shadow, label);
                dl->AddText(ImVec2(sx, sy), col, label);

                // Ship icon — diamond
                float r = 6.0f;
                ImVec2 pts[4] = {
                    {sx, sy - r - 14}, {sx + r, sy - 14},
                    {sx, sy + r - 14}, {sx - r, sy - 14}
                };
                dl->AddConvexPolyFilled(pts, 4, col);
            }
        }

        // ── World objects ────────────────────────────────────────
        for (const auto& o : state.objects) {
            if (o.distance > settings.max_distance) continue;

            bool show = false;
            ImU32 col = IM_COL32(255, 255, 255, 200);

            switch (o.type) {
                case ESotActorType::Chest:
                    show = settings.show_chests;
                    col = IM_COL32(255, 215, 0, 255); // gold
                    break;
                case ESotActorType::Barrel:
                    show = settings.show_barrels;
                    col = IM_COL32(180, 120, 60, 220);
                    break;
                case ESotActorType::Mermaid:
                    show = settings.show_mermaids;
                    col = IM_COL32(0, 255, 200, 255);
                    break;
                case ESotActorType::Fort:
                case ESotActorType::WorldEvent:
                    show = settings.show_events;
                    col = IM_COL32(255, 100, 255, 255);
                    break;
                case ESotActorType::Animal:
                    show = settings.show_animals;
                    col = IM_COL32(150, 220, 100, 220);
                    break;
                case ESotActorType::Cannon:
                    show = settings.show_cannons;
                    col = IM_COL32(200, 200, 200, 220);
                    break;
                case ESotActorType::Rowboat:
                    show = settings.show_rowboats;
                    col = IM_COL32(160, 140, 100, 220);
                    break;
                case ESotActorType::Shipwreck:
                    show = settings.show_shipwrecks;
                    col = IM_COL32(139, 90, 43, 255);
                    break;
                case ESotActorType::Seagulls:
                    show = settings.show_seagulls;
                    col = IM_COL32(220, 220, 220, 200);
                    break;
                default:
                    break;
            }

            if (!show) continue;

            float sx, sy;
            if (!world_to_screen(cam, o.position, sw, sh, sx, sy))
                continue;

            ImU32 shadow = IM_COL32(0, 0, 0, 180);
            char label[96];
            snprintf(label, sizeof(label), "%s [%.0fm]",
                SotActorTypeName(o.type), o.distance);

            dl->AddText(ImVec2(sx + 1, sy + 1), shadow, label);
            dl->AddText(ImVec2(sx, sy), col, label);

            // Small square marker
            float r = 3.0f;
            dl->AddRectFilled(ImVec2(sx - r, sy - r - 12), ImVec2(sx + r, sy + r - 12), col);
        }
    }

    void render_controls() {
        ImGui::SeparatorText("ESP Filters");

        ImGui::SetNextItemWidth(140);
        ImGui::SliderFloat("Max Distance (m)", &settings.max_distance, 100.0f, 10000.0f, "%.0f");

        ImGui::Columns(3, "##esp_cols", false);

        ImGui::Checkbox("Players",    &settings.show_players);
        ImGui::Checkbox("Ships",      &settings.show_ships);
        ImGui::Checkbox("Skeletons",  &settings.show_skeletons);
        ImGui::Checkbox("Chests",     &settings.show_chests);
        ImGui::Checkbox("Barrels",    &settings.show_barrels);

        ImGui::NextColumn();
        ImGui::Checkbox("Events",     &settings.show_events);
        ImGui::Checkbox("Mermaids",   &settings.show_mermaids);
        ImGui::Checkbox("Animals",    &settings.show_animals);
        ImGui::Checkbox("Shipwrecks", &settings.show_shipwrecks);

        ImGui::NextColumn();
        ImGui::Checkbox("Cannons",    &settings.show_cannons);
        ImGui::Checkbox("Rowboats",   &settings.show_rowboats);
        ImGui::Checkbox("Seagulls",   &settings.show_seagulls);

        ImGui::Columns(1);
    }

private:
    // ── World to Screen ──────────────────────────────────────────
    // UE-style W2S using camera forward/right/up axes derived from pitch/yaw.
    static bool world_to_screen(const SotMinimalViewInfo& cam,
                                const SotVector& world,
                                int sw, int sh,
                                float& out_x, float& out_y)
    {
        float dx = world.X - cam.Location.X;
        float dy = world.Y - cam.Location.Y;
        float dz = world.Z - cam.Location.Z;

        float pitch = cam.Rotation.Pitch * (3.14159265f / 180.0f);
        float yaw   = cam.Rotation.Yaw   * (3.14159265f / 180.0f);
        float cp = cosf(pitch);
        float sp = sinf(pitch);
        float cy = cosf(yaw);
        float sy = sinf(yaw);

        float ax_x_x = cp * cy;
        float ax_x_y = cp * sy;
        float ax_x_z = sp;

        float ax_y_x = -sy;
        float ax_y_y = cy;
        float ax_y_z = 0.0f;

        float ax_z_x = -(sp * cy);
        float ax_z_y = -(sp * sy);
        float ax_z_z = cp;

        float tx = dx * ax_y_x + dy * ax_y_y + dz * ax_y_z;
        float ty = dx * ax_z_x + dy * ax_z_y + dz * ax_z_z;
        float tz = dx * ax_x_x + dy * ax_x_y + dz * ax_x_z;

        if (tz < 1.0f)
            return false;

        float fov_rad = cam.FOV * (3.14159265f / 180.0f);
        float tan_half_fov = tanf(fov_rad / 2.0f);
        if (tan_half_fov < 0.001f)
            return false;

        float cx = (float)sw / 2.0f;
        float cy_s = (float)sh / 2.0f;

        out_x = cx + tx * (cx / tan_half_fov) / tz;
        out_y = cy_s + (-ty) * (cx / tan_half_fov) / tz;

        if (out_x < -200 || out_x > sw + 200 || out_y < -200 || out_y > sh + 200)
            return false;

        return true;
    }
};

#endif
