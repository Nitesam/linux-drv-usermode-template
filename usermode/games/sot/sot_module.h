#ifndef SOT_MODULE_H
#define SOT_MODULE_H

#include "../../game_interface.h"
#include "sot_esp.h"
#include "sot_reader.h"

#include "imgui.h"

class SotModule : public GameModule {
public:
    SotModule() = default;

    const char* game_name()     override { return "Sea of Thieves"; }
    const char* process_name()  override { return "SotGame.exe"; }
    const char* module_filter() override { return "SotGame.exe"; }
    std::vector<const char*> alt_process_names() override {
        return {
            "SotGame",
            "SoTGame.exe",
            "SoTGame",
            "SotGame-Win64-Shipping.exe",
            "SotGame-Win64-Shipping",
            "SoTGame-Win64-Shipping.exe",
            "SoTGame-Win64-Shipping",
            "SeaOfThieves.exe",
            "SeaOfThieves",
            "SoT",
        };
    }

    void update(MemClient& client, int pid, uint64_t base) override {
        if (!reader_)
            reader_ = std::make_unique<SotReader>(client);
        state_ = reader_->update(pid, base);
    }

    bool is_valid()     override { return state_.valid; }
    int  player_count() override { return state_.player_count; }

    std::string status_text() override {
        std::string s;
        if (state_.has_camera) {
            char buf[128];
            snprintf(buf, sizeof(buf), "FOV: %.0f  Pos: (%.0f, %.0f, %.0f)",
                state_.camera.FOV,
                state_.camera.Location.X,
                state_.camera.Location.Y,
                state_.camera.Location.Z);
            s = buf;
        }
        char buf2[128];
        snprintf(buf2, sizeof(buf2), "  Players: %d  Ships: %zu  Objects: %zu",
            state_.player_count, state_.ships.size(), state_.objects.size());
        s += buf2;
        return s;
    }

    void render_controls() override {
        ImGui::SetNextItemWidth(140);
        ImGui::SliderFloat("Max Dist (m)", &esp_.settings.max_distance, 100.0f, 10000.0f, "%.0f");
    }

    std::string save_settings() override { return esp_.settings.to_json(); }
    void load_settings(const std::string& json) override { esp_.settings.from_json(json); }

    void render_table() override {
        if (!state_.valid && !state_.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Error: %s", state_.error.c_str());
        }

        // ── Players table ────────────────────────────────────────
        ImGui::SeparatorText("Players & Skeletons");

        if (ImGui::BeginTable("SotPlayerTable", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed, 25);
            ImGui::TableSetupColumn("Type",      ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Name",      ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Dist (m)",  ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Position",  ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("Class",     ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            int idx = 0;
            for (const auto& p : state_.players) {
                if (!p.valid) continue;
                ImGui::TableNextRow();

                bool is_player = (p.type == ESotActorType::Player);
                ImVec4 type_col = is_player
                    ? ImVec4(0.2f, 0.8f, 0.4f, 1.0f)
                    : ImVec4(0.8f, 0.8f, 0.3f, 1.0f);

                ImGui::TableNextColumn(); ImGui::Text("%d", ++idx);
                ImGui::TableNextColumn();
                ImGui::TextColored(type_col, "%s", SotActorTypeName(p.type));
                ImGui::TableNextColumn();
                if (p.is_local)
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s (You)", p.name);
                else
                    ImGui::Text("%s", p.name);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", p.distance);
                ImGui::TableNextColumn();
                ImGui::Text("(%.0f, %.0f, %.0f)", p.position.X, p.position.Y, p.position.Z);
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", p.actor_class);
            }
            ImGui::EndTable();
        }

        // ── Ships table ──────────────────────────────────────────
        if (!state_.ships.empty()) {
            ImGui::SeparatorText("Ships");

            if (ImGui::BeginTable("SotShipTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 25);
                ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Dist (m)", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 200);
                ImGui::TableSetupColumn("Class",    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                int idx = 0;
                for (const auto& s : state_.ships) {
                    if (!s.valid) continue;
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn(); ImGui::Text("%d", ++idx);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s",
                        SotShipTypeName(s.ship_type));
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f", s.distance);
                    ImGui::TableNextColumn();
                    ImGui::Text("(%.0f, %.0f, %.0f)", s.position.X, s.position.Y, s.position.Z);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", s.actor_class);
                }
                ImGui::EndTable();
            }
        }

        // ── Objects table ────────────────────────────────────────
        if (!state_.objects.empty()) {
            ImGui::SeparatorText("World Objects");

            if (ImGui::BeginTable("SotObjTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                    ImVec2(0, 200)))
            {
                ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 25);
                ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Dist (m)", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 200);
                ImGui::TableSetupColumn("Class",    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                int idx = 0;
                for (const auto& o : state_.objects) {
                    if (o.distance > esp_.settings.max_distance) continue;
                    ImGui::TableNextRow();

                    ImU32 tc = IM_COL32(255, 255, 255, 255);
                    switch (o.type) {
                        case ESotActorType::Chest:      tc = IM_COL32(255, 215, 0, 255); break;
                        case ESotActorType::Mermaid:    tc = IM_COL32(0, 255, 200, 255); break;
                        case ESotActorType::WorldEvent: tc = IM_COL32(255, 100, 255, 255); break;
                        case ESotActorType::Shipwreck:  tc = IM_COL32(180, 120, 60, 255); break;
                        default: break;
                    }

                    ImGui::TableNextColumn(); ImGui::Text("%d", ++idx);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tc), "%s",
                        SotActorTypeName(o.type));
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f", o.distance);
                    ImGui::TableNextColumn();
                    ImGui::Text("(%.0f, %.0f, %.0f)", o.position.X, o.position.Y, o.position.Z);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", o.class_name);
                }
                ImGui::EndTable();
            }
        }
    }

    void render_esp(ImDrawList* draw_list, int screen_w, int screen_h) override {
        esp_.render(draw_list, state_, screen_w, screen_h);
    }

    void render_esp_controls() override {
        esp_.render_controls();
    }

    void render_debug_panel() override {
        auto ok   = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        auto fail = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        auto dim  = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        auto white = ImVec4(1, 1, 1, 1);
        auto& d = state_.debug;

        if (ImGui::BeginTable("##sot_dbg", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableSetupColumn("Value");

            auto row_hex = [&](const char* key, ImVec4 col, uint64_t val) {
                ImGui::TableNextColumn(); ImGui::TextColored(dim, "%s", key);
                ImGui::TableNextColumn(); ImGui::TextColored(col, "0x%lX", val);
            };
            auto row_int = [&](const char* key, uint32_t val) {
                ImGui::TableNextColumn(); ImGui::TextColored(dim, "%s", key);
                ImGui::TableNextColumn(); ImGui::Text("%u", val);
            };
            auto row_signed = [&](const char* key, int32_t val) {
                ImGui::TableNextColumn(); ImGui::TextColored(dim, "%s", key);
                ImGui::TableNextColumn(); ImGui::Text("%d", val);
            };
            auto row_text = [&](const char* key, ImVec4 col, const char* val) {
                ImGui::TableNextColumn(); ImGui::TextColored(dim, "%s", key);
                ImGui::TableNextColumn(); ImGui::TextColored(col, "%s", val);
            };

            row_hex("Base", white, state_.base_address);
            row_hex("GWorld", d.gworld ? ok : fail, d.gworld);
            row_hex("PersistentLevel", d.persistent_level ? ok : fail, d.persistent_level);
            row_hex("GameState", d.game_state ? ok : fail, d.game_state);
            row_hex("LocalPawn", d.local_pawn ? ok : fail, d.local_pawn);
            row_hex("CameraManager", d.camera_manager ? ok : fail, d.camera_manager);
            row_text("GNames", d.gnames_ok ? ok : fail, d.gnames_ok ? "OK" : "FAILED");
            if (d.local_pawn_class[0] != '\0')
                row_text("LocalPawn Class", ok, d.local_pawn_class);
            if (d.local_pawn_probe[0] != '\0')
                row_text("LocalPawn Probe", dim, d.local_pawn_probe);

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "Camera");
            ImGui::TableNextColumn();
            if (state_.has_camera)
                ImGui::TextColored(ok, "FOV=%.0f (%.0f,%.0f,%.0f)",
                    state_.camera.FOV,
                    state_.camera.Location.X, state_.camera.Location.Y, state_.camera.Location.Z);
            else
                ImGui::TextColored(fail, "NO");

            if (d.camera_source[0] != '\0')
                row_text("Camera Source", state_.has_camera ? ok : fail, d.camera_source);

            row_int("Actors Scanned", d.actor_scan_count);
            if (d.actor_array_offset != 0) {
                row_hex("Actor Array Off", ok, d.actor_array_offset);
                row_int("Actor Array Count", d.actor_array_count);
                row_signed("Actor Array Score", d.actor_array_score);
            }
            row_int("Class Resolve Fail", d.class_resolve_fail_count);
            row_int("Broken Class Name", d.broken_class_name_count);
            row_int("Unknown Type", d.unknown_type_count);
            row_int("Classified OK", d.classified_actor_count);
            row_int("Position Fail", d.position_fail_count);
            row_int("BP_ Actors", d.bp_actor_count);
            row_int("BP_ Classes", static_cast<uint32_t>(d.bp_classes.size()));
            row_int("Players", d.player_count);
            row_int("Ships", d.ship_count);
            row_int("Objects", d.object_count);

            if (d.class_resolve_error[0] != '\0')
                row_text("Resolve Error", fail, d.class_resolve_error);

            ImGui::EndTable();
        }

        if (!d.bp_classes.empty() &&
            ImGui::CollapsingHeader("BP_ Classes Found", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextColored(dim, "Unique class names starting with BP_ in the current scan:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##sot_bp_classes")) {
                std::string all;
                for (const auto& cls : d.bp_classes) {
                    all += cls;
                    all += '\n';
                }
                if (!all.empty())
                    ImGui::SetClipboardText(all.c_str());
            }

            ImGui::BeginChild("##sot_bp_class_list", ImVec2(0, 180), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(d.bp_classes.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f),
                                       "%3d  %s", i + 1, d.bp_classes[(size_t)i].c_str());
                }
            }
            ImGui::EndChild();
        }

        if (d.position_fail_sample_count > 0 && ImGui::CollapsingHeader("Position Fail Samples")) {
            ImGui::TextColored(dim, "Classified actors discarded for invalid position:");
            for (uint32_t i = 0; i < d.position_fail_sample_count; i++)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  %s", d.position_fail_samples[i]);
        }

        if (d.unknown_actor_count > 0 && ImGui::CollapsingHeader("Unknown Actors")) {
            ImGui::TextColored(dim, "Unclassified actors with interesting names:");
            for (uint32_t i = 0; i < d.unknown_actor_count; i++)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  %s", d.unknown_actors[i]);
        }
    }

    std::string to_json() override {
        char buf[16384];
        int n = 0;
        n += snprintf(buf + n, sizeof(buf) - n,
            "{\"valid\":%s,\"has_camera\":%s,\"player_count\":%d,",
            state_.valid ? "true" : "false",
            state_.has_camera ? "true" : "false",
            state_.player_count);

        if (state_.has_camera) {
            n += snprintf(buf + n, sizeof(buf) - n,
                "\"camera\":{\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,\"yaw\":%.1f,\"fov\":%.0f},",
                state_.camera.Location.X, state_.camera.Location.Y, state_.camera.Location.Z,
                state_.camera.Rotation.Yaw, state_.camera.FOV);
        }

        // Players
        n += snprintf(buf + n, sizeof(buf) - n, "\"players\":[");
        bool first = true;
        for (const auto& p : state_.players) {
            if (!p.valid) continue;
            if (!first) n += snprintf(buf + n, sizeof(buf) - n, ",");
            first = false;

            n += snprintf(buf + n, sizeof(buf) - n,
                "{\"name\":\"%s\",\"type\":\"%s\","
                "\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,"
                "\"dist\":%.0f,\"is_local\":%s}",
                p.name, SotActorTypeName(p.type),
                p.position.X, p.position.Y, p.position.Z,
                p.distance, p.is_local ? "true" : "false");
        }
        n += snprintf(buf + n, sizeof(buf) - n, "],");

        // Ships
        n += snprintf(buf + n, sizeof(buf) - n, "\"ships\":[");
        first = true;
        for (const auto& s : state_.ships) {
            if (!s.valid) continue;
            if (!first) n += snprintf(buf + n, sizeof(buf) - n, ",");
            first = false;

            n += snprintf(buf + n, sizeof(buf) - n,
                "{\"type\":\"%s\",\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,\"dist\":%.0f}",
                SotShipTypeName(s.ship_type),
                s.position.X, s.position.Y, s.position.Z, s.distance);
        }
        n += snprintf(buf + n, sizeof(buf) - n, "],");

        // Objects
        n += snprintf(buf + n, sizeof(buf) - n, "\"objects\":[");
        first = true;
        for (const auto& o : state_.objects) {
            if (o.distance > esp_.settings.max_distance) continue;
            if (!first) n += snprintf(buf + n, sizeof(buf) - n, ",");
            first = false;

            n += snprintf(buf + n, sizeof(buf) - n,
                "{\"type\":\"%s\",\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,\"dist\":%.0f}",
                SotActorTypeName(o.type),
                o.position.X, o.position.Y, o.position.Z, o.distance);
        }
        n += snprintf(buf + n, sizeof(buf) - n, "]}");

        return std::string(buf, n);
    }

    std::shared_ptr<GameModule> clone_for_render() override {
        auto snap = std::shared_ptr<SotModule>(new SotModule(state_));
        return snap;
    }

private:
    explicit SotModule(const SotWorldState& s) : state_(s) {}

    std::unique_ptr<SotReader> reader_;
    SotWorldState state_{};
    static inline SotEspRenderer esp_{};
};

#endif
