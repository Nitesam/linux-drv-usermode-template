#ifndef DBD_MODULE_H
#define DBD_MODULE_H

#include "../../game_interface.h"
#include "dbd_esp.h"
#include "dbd_reader.h"

#include "imgui.h"

class DbdModule : public GameModule {
public:
    DbdModule() = default;

    const char* game_name()     override { return "Dead by Daylight"; }
    const char* process_name()  override { return "DeadByDaylight-EGS-Shipping.exe"; }
    const char* module_filter() override { return "DeadByDaylight-EGS-Shipping.exe"; }
    std::vector<const char*> alt_process_names() override {
        return {"GameThread"};
    }

    void update(MemClient& client, int pid, uint64_t base) override {
        if (!reader_)
            reader_ = std::make_unique<DbdReader>(client);
        DbdAuraConfig aura_cfg = esp_.settings.get_aura_config();
        state_ = reader_->update(pid, base, &aura_cfg);
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
        int surv = 0, kill = 0;
        for (auto& p : state_.players) {
            if (p.type == EDbdActorType::Survivor) surv++;
            else kill++;
        }
        if (surv > 0 || kill > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  Surv: %d  Kill: %d", surv, kill);
            s += buf;
        }
        return s;
    }

    void render_controls() override {
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Max Dist", &esp_.settings.max_distance, 50.0f, 500.0f, "%.0f");
    }

    std::string save_settings() override { return esp_.settings.to_json(); }
    void load_settings(const std::string& json) override { esp_.settings.from_json(json); }

    void render_table() override {
        if (!state_.valid && !state_.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Error: %s", state_.error.c_str());
        }

        if (!ImGui::BeginTable("DbdPlayerTable", 7,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp))
            return;

        ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Role",      ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Name",      ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Prestige",  ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Level",     ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Perks",     ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int idx = 0;
        for (const auto& p : state_.players) {
            if (!p.valid) continue;

            ImGui::TableNextRow();

            ImVec4 role_col = (p.type == EDbdActorType::Survivor)
                ? ImVec4(0.0f, 0.86f, 0.2f, 1.0f)
                : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);

            ImGui::TableNextColumn(); ImGui::Text("%d", ++idx);
            ImGui::TableNextColumn();
            ImGui::TextColored(role_col, "%s",
                p.type == EDbdActorType::Survivor ? "Survivor" : "Killer");
            ImGui::TableNextColumn();
            if (p.character_name[0])
                ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "%s", p.character_name);
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "--");
            ImGui::TableNextColumn();
            if (p.is_local)
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s (You)", p.name);
            else
                ImGui::Text("%s", p.name);
            ImGui::TableNextColumn();
            if (p.prestige >= 0)
                ImGui::Text("P%d", p.prestige);
            else
                ImGui::Text("-");
            ImGui::TableNextColumn();
            if (p.level >= 0)
                ImGui::Text("%d", p.level);
            else
                ImGui::Text("-");
            ImGui::TableNextColumn();
            if (p.perks_valid) {
                char perks_str[256];
                int n = 0;
                for (int pi = 0; pi < DBD_MAX_PERKS; pi++) {
                    if (pi > 0) n += snprintf(perks_str + n, sizeof(perks_str) - n, " ");
                    if (p.perk_names[pi][0])
                        n += snprintf(perks_str + n, sizeof(perks_str) - n, "%s", p.perk_names[pi]);
                    else if (p.perk_ids[pi] > 0)
                        n += snprintf(perks_str + n, sizeof(perks_str) - n, "#%d", p.perk_ids[pi]);
                    else
                        n += snprintf(perks_str + n, sizeof(perks_str) - n, "-");
                }
                ImGui::Text("%s", perks_str);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "--");
            }
        }

        ImGui::EndTable();
    }

    void render_esp(ImDrawList* draw_list, int screen_w, int screen_h) override {
        if (state_.valid && !state_.has_camera) {
            esp_.render_lobby_panel(draw_list, state_, screen_w, screen_h);
        }
        esp_.render(draw_list, state_, screen_w, screen_h);
    }

    void render_esp_controls() override {
        esp_.render_controls();
    }

    void render_debug_panel() override {
        auto ok = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        auto warn = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        auto fail = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        auto dim = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

        if (ImGui::BeginTable("##dbg", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("Value");

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "Base");
            ImGui::TableNextColumn(); ImGui::Text("0x%lX", state_.base_address);

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "GWorld");
            ImGui::TableNextColumn();
            ImGui::TextColored(state_.valid ? ok : fail, "%s", state_.valid ? "OK" : "FAIL");

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "GNames");
            ImGui::TableNextColumn();
            bool gn = (reader_ && !state_.objects.empty());
            ImGui::TextColored(gn ? ok : warn, "%s", gn ? "Resolved" : "Pending");

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "Camera");
            ImGui::TableNextColumn();
            if (state_.has_camera)
                ImGui::Text("FOV=%.0f  (%.0f, %.0f, %.0f)",
                    state_.camera.FOV,
                    state_.camera.Location.X, state_.camera.Location.Y, state_.camera.Location.Z);
            else
                ImGui::TextColored(fail, "NO");

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "Players");
            ImGui::TableNextColumn();
            int surv = 0, kill = 0;
            for (auto& p : state_.players) {
                if (p.type == EDbdActorType::Survivor) surv++;
                else kill++;
            }
            ImGui::Text("%d  (S:%d K:%d)", state_.player_count, surv, kill);

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "Objects");
            ImGui::TableNextColumn();
            ImGui::Text("%zu", state_.objects.size());

            ImGui::TableNextColumn(); ImGui::TextColored(dim, "Bones");
            ImGui::TableNextColumn();
            if (!state_.players.empty() && state_.players[0].bone_count > 0)
                ImGui::TextColored(ok, "%u bones", state_.players[0].bone_count);
            else
                ImGui::TextColored(warn, "Not found");

            ImGui::EndTable();
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

        n += snprintf(buf + n, sizeof(buf) - n, "\"players\":[");
        for (size_t i = 0; i < state_.players.size(); i++) {
            const auto& p = state_.players[i];
            if (!p.valid) continue;
            if (i > 0) n += snprintf(buf + n, sizeof(buf) - n, ",");

            n += snprintf(buf + n, sizeof(buf) - n,
                "{\"name\":\"%s\",\"character\":\"%s\",\"role\":\"%s\","
                "\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,"
                "\"hp\":%d,\"prestige\":%d,\"level\":%d,"
                "\"is_local\":%s,",
                p.name, p.character_name[0] ? p.character_name : "",
                p.type == EDbdActorType::Survivor ? "survivor" : "killer",
                p.position.X, p.position.Y, p.position.Z,
                p.health_states, p.prestige, p.level,
                p.is_local ? "true" : "false");

            n += snprintf(buf + n, sizeof(buf) - n, "\"perks\":[");
            for (int pi = 0; pi < DBD_MAX_PERKS; pi++) {
                if (pi > 0) n += snprintf(buf + n, sizeof(buf) - n, ",");
                if (p.perk_names[pi][0])
                    n += snprintf(buf + n, sizeof(buf) - n, "\"%s\"", p.perk_names[pi]);
                else
                    n += snprintf(buf + n, sizeof(buf) - n, "\"\"");
            }
            n += snprintf(buf + n, sizeof(buf) - n, "]}");
        }
        n += snprintf(buf + n, sizeof(buf) - n, "],\"objects\":[");

        for (size_t i = 0; i < state_.objects.size(); i++) {
            const auto& o = state_.objects[i];
            if (i > 0) n += snprintf(buf + n, sizeof(buf) - n, ",");
            n += snprintf(buf + n, sizeof(buf) - n,
                "{\"type\":\"%s\",\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,\"dist\":%.0f",
                DbdObjectTypeName(o.type), o.position.X, o.position.Y, o.position.Z, o.distance);

            if (o.type == EDbdObjectType::Generator)
                n += snprintf(buf + n, sizeof(buf) - n, ",\"progress\":%.1f,\"blocked\":%s",
                    o.gen_progress, o.gen_blocked ? "true" : "false");
            else if (o.type == EDbdObjectType::Pallet)
                n += snprintf(buf + n, sizeof(buf) - n, ",\"state\":%d", o.pallet_state);
            else if (o.type == EDbdObjectType::Totem)
                n += snprintf(buf + n, sizeof(buf) - n, ",\"state\":%d", o.totem_state);
            else if (o.type == EDbdObjectType::Hatch)
                n += snprintf(buf + n, sizeof(buf) - n, ",\"state\":%d", o.hatch_state);
            else if (o.type == EDbdObjectType::Hook)
                n += snprintf(buf + n, sizeof(buf) - n, ",\"occupied\":%s,\"basement\":%s",
                    o.hook_occupied ? "true" : "false", o.hook_basement ? "true" : "false");
            else if (o.type == EDbdObjectType::Chest)
                n += snprintf(buf + n, sizeof(buf) - n, ",\"opened\":%s", o.chest_opened ? "true" : "false");
            else if (o.type == EDbdObjectType::EscapeDoor)
                n += snprintf(buf + n, sizeof(buf) - n, ",\"activated\":%s", o.escape_activated ? "true" : "false");

            n += snprintf(buf + n, sizeof(buf) - n, "}");
        }
        n += snprintf(buf + n, sizeof(buf) - n, "]}");
        return std::string(buf, n);
    }

    std::shared_ptr<GameModule> clone_for_render() override {
        auto snap = std::shared_ptr<DbdModule>(new DbdModule(state_));
        return snap;
    }

private:
    explicit DbdModule(const DbdWorldState& s) : state_(s) {}

    std::unique_ptr<DbdReader> reader_;
    DbdWorldState state_{};
    static inline DbdEspRenderer esp_{};
};

#endif
