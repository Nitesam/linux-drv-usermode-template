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

        if (!ImGui::BeginTable("DbdPlayerTable", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp))
            return;

        ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Role",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Prestige", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Level",    ImGuiTableColumnFlags_WidthFixed, 55);
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
        }

        ImGui::EndTable();
    }

    void render_esp(ImDrawList* draw_list, int screen_w, int screen_h) override {
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

private:
    std::unique_ptr<DbdReader> reader_;
    DbdWorldState state_{};
    static inline DbdEspRenderer esp_{};
};

#endif
